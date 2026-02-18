# Cm コンパイラ バグ・制約事項 — 統合リファレンス

> CosmOS開発中に発見されたCmコンパイラ（v0.14.1、`--target=uefi` x86_64）のバグ・制約事項。
>
> 最終更新: 2026-02-18

---

## サマリー

| # | バグ名 | 重要度 | 状態 | カテゴリ |
|---|--------|--------|------|----------|
| 5 | `__asm__`出力変数のwhile条件不具合 | 重大 | ✅ 修正済 | ASM/最適化 |
| 7 | `must { __asm__() }` 制御フロー干渉 | 低 | ✅ 修正済 | ASM |
| 9 | スタックオフセット重複 | 重大 | ✅ 修正済 | コード生成 |
| 10 | ポインタ経由impl self変更消失 | 重大 | ✅ 修正済 | impl/LLVM IR |
| 11 | インライン展開ASMレジスタ割当変更 | 重大 | ✅ 修正済 | インライン展開 |
| 12 | インライン展開時ret先不在 | 重大 | ✅ 修正済 | インライン展開 |
| 13 | インライン展開レジスタ上書き (UEFIクラッシュ) | 致命的 | ✅ 修正済 | インライン展開 |
| 14 | export関数数超過でコンパイラハング | 重大 | ✅ 修正済 | シンボル解決 |
| 15 | 非export関数がexport関数から呼出不可 | 中 | ✅ 修正済 | シンボル解決 |
| 16 | `&local as ulong` キャスト型エラー | 低 | ✅ 修正済 | 型キャスト |
| 17 | `___chkstk_ms` 未定義シンボル | 中 | ✅ 修正済 | リンカ |

> ✅ 全バグ修正済み（v0.14.1 最終確認: 2026-02-18）

---

## 未修正バグ（open）

### Bug #5: `__asm__` 出力変数の while 条件不具合 — 重大

`__asm__`の出力制約`${=r:var}`で更新された変数を`while(var != 0)`の条件で使用すると、変数が0に更新されてもループが停止しない。

```cm
// ❌ バグ再現
ulong byte_val = 0;
__asm__(` movq ${r:addr}, %r10; xorq %rax, %rax; movzbl (%r10), %eax; movq %rax, ${=r:byte_val} `);
while (byte_val != 0) {   // ← byte_val == 0 でもループ継続
    outb(0xE9, byte_val);
    __asm__(`... movq %rax, ${=r:byte_val}`);
}

// ✅ 回避策: ループ内スコープ宣言
while (true) {
    ulong byte_val = 0;
    __asm__(`... movq %rax, ${=r:byte_val}`);
    if (byte_val == 0) { break; }
    outb(0xE9, byte_val);
}
```

**推定原因**: 定数畳み込みまたはSSA最適化がASM出力変数の更新をwhile条件の再評価に伝播しない。

---

### Bug #7: `must { __asm__() }` の制御フロー干渉 — 低

`must { }` ブロックで `__asm__` をラップすると、ASM出力変数や制御フローに悪影響。

```cm
// ❌ must内のASMが制御フローを破壊
while (true) { must { __asm__("hlt"); } }  // haltループから脱出

// ✅ 回避策: mustなしで直接使用
while (true) { __asm__("hlt"); }
```

---

### Bug #9: ローカル配列とポインタ変数のスタックオフセット重複 — 重大

ローカル配列のアドレスを`&arr`で取得してポインタ変数に格納すると、配列のスタック配置が1要素分ずれる。

```cm
// ❌ バグ再現
ulong[3] b;
b[0] = 0x1111; b[1] = 0x2222; b[2] = 0x3333;
void* b_ptr = &b as void*;  // ← この行を追加するとバグ発生
// b[0] → 0x2222, b[1] → 0x3333, b[2] → 0x3333 （1つシフト）

// ✅ 回避策: アドレスを変数に格納しない。固定メモリアドレスに配列を配置する。
```

| 条件 | 結果 |
|------|------|
| `ulong[3] arr;` のみ（ポインタ変数なし） | ✅ 正常 |
| `ulong[3] arr;` + `void* p = &arr as void*;` | ❌ バグ再現 |
| JITモード（`cm run`） | ✅ 正常（UEFIのみ再現） |

**推定原因**: 配列とポインタ変数に同一のスタックベースオフセットが割り当てられる。

---

### Bug #11: インライン展開によるASMレジスタ割当変更 — 重大

`__asm__`内で`%rdi`/`%rsi`を直接参照してSystem V ABIを前提にすると、インライン展開時にパラメータが別のレジスタ/スタックに配置され不正動作。

```cm
// ❌ ABI依存のレジスタ直接指定
export void switch_to(ulong old, ulong new) {
    __asm__(` movq %rdi, %r10; movq %rsi, %r11 `);  // rdi/rsiに値がない
}

// ✅ 回避策: ${r:var} 構文でCmに任せる
export void switch_to(ulong old, ulong new) {
    __asm__(` movq ${r:old}, %r10; movq ${r:new}, %r11 `);
}
```

---

### Bug #12: インライン展開時のret先不在 — 重大

`__asm__`内で`ret`命令を使う関数がインライン展開されると、`call`省略によりスタック上にreturn addressが存在せずクラッシュ。

```cm
// ✅ 回避策: 数値ラベルでpush
__asm__(`
    leaq 1f(%rip), %rax;   // 復帰ラベルのアドレス
    pushq %rax;
    // ... pushes + RSP switch + pops ...
    ret;                    // pushしたアドレスに戻る
1:
    nop
`);
```

> [!CAUTION]
> 名前付きラベル(`.Lswitch_resume:`)は複数回インライン展開でラベル重複エラー。必ず数値ラベル(`1:`)を使用。

---

## 回避済バグ（workaround）

### Bug #10: ポインタ経由 impl で self 変更消失 — 重大 (3種複合)

ポインタ経由で`impl`メソッドを呼び出した場合の3つのバグ。UEFIターゲット固有。

| # | 名前 | 発生条件 |
|---|------|---------|
| 10a | self書き戻し不在 | `ptr->method()` で `self.field = v` が元構造体に反映されない |
| 10b | GEPインデックス上限 | 構造体フィールド6個以上の impl で `getelementptr` エラー |
| 10c | Load operand不正 | ポインタ経由 impl の書き戻しで `load` 型不整合 |

```cm
// ❌ 再現: フィールド6個以上のimpl
export struct LargeStruct { ulong f0; ulong f1; ulong f2; ulong f3; ulong f4; ulong f5; }
impl LargeStruct {
    void set_all() { self.f5 = 5; }  // ❌ GEPエラー
}

// ❌ 再現: ポインタ経由のself変更
Counter* ptr = &counter;
ptr->increment();  // self.value += 1 が反映されない

// ✅ 回避策: ポインタ内包型ラッパー設計
struct FBConsole { ulong addr; }  // 1フィールドのみ → GEP回避
impl FBConsole {
    overload self() { self.addr = GLOBAL_FB_CONSOLE_ADDR; }
    ulong _read(ulong offset) {
        ulong* ptr = (self.addr + offset) as ulong*;
        return *ptr;
    }
}
```

---

### Bug #13: インライン展開時のレジスタ上書き（UEFIクラッシュ）— 致命的

`impl`メソッド/コンストラクタのインライン展開時に`efi_main`の引数レジスタ（`rcx`, `rdx`）が上書きされ、UEFIデータ構造が破壊されてクラッシュ。

```cm
// ❌ クラッシュ
ulong efi_main(void* image_handle, void* system_table) {
    SerialPort serial(0x3F8);  // コンストラクタ展開でrcx/rdx上書き
    serial.putc(0x48);         // #UD - Invalid Opcode
}

// ✅ 回避策: efi_main引数を即座にローカルに退避
ulong efi_main(void* image_handle, void* system_table) {
    void* ih = image_handle;
    void* st = system_table;
    SerialPort serial(0x3F8);  // 退避後なら安全
}
```

**逆アセンブル根拠**: コンストラクタ展開で`rcx`=selfポインタ、`rdx`=port引数として使用→実際にはrcx=image_handle, rdx=system_tableが入っている→UEFIデータ構造破壊。

---

### Bug #14: export関数数超過でコンパイラハング — 重大

1つの`.cm`ファイルに**7つ以上のexport関数**を定義し、大規模プロジェクトからインポートするとコンパイラが無限ハングまたはOOM Killedになる。

```cm
// ❌ NG: 多数のexport（7+でハング）
export ulong func1(ulong a) { return a; }
export ulong func2(ulong a) { return a; }
// ... func7以上でハング

// ✅ 回避策: ディスパッチパターン
export ulong module_op(ulong op, ulong a1, ulong a2, ulong a3, ulong a4) {
    if (op == 1) { /* func1の処理 */ }
    if (op == 2) { /* func2の処理 */ }
    return 0;
}
```

| 条件 | 結果 |
|------|------|
| 新モジュール 1-6 export関数 | ✅ 正常 |
| 新モジュール 7+ export関数 | ❌ ハング/Killed: 9 |

**CosmOS対応**: `cosmfs.cm`、`aria.cm`等すべてディスパッチパターンで`init()` + `op()` の2 export関数に統合。

---

### Bug #15: 非export関数がexport関数から呼出不可 — 中

private関数を同一モジュール内のexport関数から呼び出すとエラー。

```cm
// ❌ エラー
ulong helper(ulong x) { return x * 2; }
export ulong main_func(ulong x) { return helper(x); }
// error: 'helper' is not a function

// ✅ 回避策: 全関数にexportを付ける、またはインライン化
```

> [!WARNING]
> Bug #14（export数制限）と矛盾する。ディスパッチパターンで1関数にまとめることで両方を回避。

---

### Bug #16: `&local as ulong` キャスト型エラー — 低

ローカル変数のアドレスを直接`ulong`にキャストできない。

```cm
// ❌ エラー
Foo foo; ulong addr = &foo as ulong;  // Type mismatch

// ✅ 回避策: 型付きポインタ経由
Foo* ref = &foo; ulong addr = ref as ulong;
```

---

### Bug #17: `___chkstk_ms` 未定義シンボル — 中

LLVM/Windowsターゲットが4KB以上のスタックフレームに自動挿入するスタックプローブ関数。ベアメタル環境では不要。

```c
// ✅ 回避策: kernel/lib/chkstk.c にno-opスタブ
void ___chkstk_ms(void) { /* no-op */ }
```

**影響関数**: `cosmfs_op()`、`aria_op()` 等の大規模ディスパッチ関数。

---

## CosmOSアーキテクチャへの影響

Bug #14 + #15 の組み合わせにより、CosmOSの全カーネルモジュールは以下の設計制約を受ける:

1. **ディスパッチパターン必須**: 各モジュールは `init()` + `op(op, arg1, arg2, arg3, arg4)` の最大2 export関数のみ
2. **ヘルパー関数のインライン化**: private関数は使えないため、全ロジックを1つの巨大関数内にインライン化
3. **ファイル分割の制限**: モジュール間でのヘルパー共有が困難
4. **構造体設計の制約**: Bug #10 により impl 使用時はポインタ内包型ラッパーが必要

> [!IMPORTANT]
> これらの制約はCmコンパイラのUEFIバックエンド固有の問題であり、JITモード(`cm run`)では再現しない。
> コンパイラ側の修正により将来的に解消される見込み。
