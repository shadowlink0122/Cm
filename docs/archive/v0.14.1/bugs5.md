# Cm コンパイラ バグ・制約事項 — 統合リファレンス

> CosmOS開発中に発見されたCmコンパイラ（v0.14.1、`--target=uefi` x86_64）のバグ・制約事項。
>
> 最終更新: 2026-02-19

---

## サマリー

| # | バグ名 | 重要度 | 状態 | カテゴリ |
|---|--------|--------|------|----------|
| 5 | `__asm__`出力変数のwhile条件不具合 | 重大 | ⚠️ 未修正 | ASM/最適化 |
| 7 | `must { __asm__() }` 制御フロー干渉 | 低 | ⚠️ 未修正 | ASM |
| 9 | スタックオフセット重複 | 重大 | ⚠️ 未修正 | コード生成 |
| 11 | インライン展開ASMレジスタ割当変更 | 重大 | ⚠️ 未修正 | インライン展開 |
| 12 | インライン展開時ret先不在 | 重大 | ⚠️ 未修正 | インライン展開 |
| 17 | `___chkstk_ms` 未定義シンボル | 中 | 🔧 回避済 | リンカ |

凡例: 🔧 CosmOS側で回避済、⚠️ 未修正（回避策のみ）

> [!IMPORTANT]
> 全バグは **UEFIターゲット (`--target=uefi`) 固有** であり、JITモード (`cm run`) では再現しない。

---

## 未修正バグ（open）

### Bug #5: `__asm__` 出力変数の while 条件不具合 — 重大

**カテゴリ**: ASM/最適化  
**発見日**: 2026-02-16  
**対象**: Cm v0.14.1 / `--target=uefi` (x86_64)

#### 症状

`__asm__`の出力制約 `${=r:var}` で更新された変数を `while(var != 0)` の条件で使用すると、変数が `0` に更新されてもループが停止しない。

#### 再現コード

```cm
//! platform: uefi

ulong efi_main(void* image_handle, void* system_table) {
    void* ih = image_handle;
    void* st = system_table;

    ulong addr = 0x1000;  // テスト用アドレス（事前に0を書き込み済みと仮定）
    ulong byte_val = 0;

    // アドレスの1バイトを読み取り
    __asm__(`
        movq ${r:addr}, %r10;
        xorq %rax, %rax;
        movzbl (%r10), %eax;
        movq %rax, ${=r:byte_val}
    `);

    // ❌ byte_val == 0 でもこのループに突入し、無限ループになる
    while (byte_val != 0) {
        addr = addr + 1;
        __asm__(`
            movq ${r:addr}, %r10;
            xorq %rax, %rax;
            movzbl (%r10), %eax;
            movq %rax, ${=r:byte_val}
        `);
    }

    while (true) { __asm__("hlt"); }
    return 0;
}
```

#### 再現手順

```bash
cm compile --target=uefi -o .tmp/build/bug5.o test_bug5.cm
lld-link /subsystem:efi_application /entry:efi_main /out:.tmp/build/BUG5.EFI .tmp/build/bug5.o
# QEMU実行 → byte_val == 0 でもwhileループが終了しないことを確認
```

#### 推定原因

定数畳み込みまたはSSA最適化パスが `__asm__` 出力変数の副作用を認識せず、while条件の再評価時に初期値を使い回す。

#### 回避策

ループ内で変数をスコープ宣言し、明示的な `break` で脱出する:

```cm
while (true) {
    ulong byte_val = 0;  // 毎ループで新規宣言
    __asm__(`... movq %rax, ${=r:byte_val}`);
    if (byte_val == 0) { break; }
    // ... 処理 ...
}
```

---

### Bug #7: `must { __asm__() }` の制御フロー干渉 — 低

**カテゴリ**: ASM  
**発見日**: 2026-02-16  
**対象**: Cm v0.14.1 / `--target=uefi` (x86_64)

#### 症状

`must { }` ブロックで `__asm__` をラップすると、制御フローに悪影響:

- ASM出力変数がスコープ外で正常に動作しない
- `while(true) { must { __asm__("hlt"); } }` がhaltループから脱出してしまう

#### 再現コード

```cm
//! platform: uefi

ulong efi_main(void* image_handle, void* system_table) {
    void* ih = image_handle;
    void* st = system_table;

    // ❌ must内のASMがhltループを脱出してしまう
    while (true) {
        must { __asm__("hlt"); }
    }
    return 0;
}
```

#### 再現手順

```bash
cm compile --target=uefi -o .tmp/build/bug7.o test_bug7.cm
lld-link /subsystem:efi_application /entry:efi_main /out:.tmp/build/BUG7.EFI .tmp/build/bug7.o
# QEMU実行 → hltで停止せず後続コードに到達
```

#### 回避策

```cm
// ✅ mustなしで直接使用
while (true) { __asm__("hlt"); }
```

---

### Bug #9: ローカル配列とポインタ変数のスタックオフセット重複 — 重大

**カテゴリ**: コード生成  
**発見日**: 2026-02-16  
**対象**: Cm v0.14.1 / `--target=uefi` (x86_64) **のみ**（JITでは再現しない）

#### 症状

ローカル配列のアドレスを `&arr` で取得してポインタ変数に格納すると、配列のスタック配置が1要素分ずれる。

#### 再現コード

```cm
//! platform: uefi
import ../kernel/drivers/serial;

ulong efi_main(void* image_handle, void* system_table) {
    void* ih = image_handle;
    void* st = system_table;
    SerialPort serial(0x3F8);

    // テストA: 配列のみ（正常動作する対照群）
    ulong[3] a;
    a[0] = 0xAAAA000000000000;
    a[1] = 0xBBBB000000000000;
    a[2] = 0xCCCC000000000000;
    serial.puts("a[0] = " as void*); serial.print_hex(a[0]); serial.println("" as void*);
    serial.puts("a[1] = " as void*); serial.print_hex(a[1]); serial.println("" as void*);
    serial.puts("a[2] = " as void*); serial.print_hex(a[2]); serial.println("" as void*);

    // テストB: 配列 + アドレス取得ポインタ変数（バグ再現）
    ulong[3] b;
    b[0] = 0x1111000000000000;
    b[1] = 0x2222000000000000;
    b[2] = 0x3333000000000000;
    void* b_ptr = &b as void*;  // ← この行を追加するとバグ発生
    serial.puts("b[0] = " as void*); serial.print_hex(b[0]); serial.println("" as void*);
    serial.puts("b[1] = " as void*); serial.print_hex(b[1]); serial.println("" as void*);
    serial.puts("b[2] = " as void*); serial.print_hex(b[2]); serial.println("" as void*);

    while (true) { __asm__("hlt"); }
    return 0;
}
```

#### 再現条件マトリクス

| 条件 | 結果 |
|------|------|
| `ulong[3] arr;` のみ（ポインタ変数なし） | ✅ 正常 |
| `ulong[3] arr;` + `void* p = &arr as void*;` | ❌ **バグ再現** |
| JITモード（`cm run`） | ✅ 正常（UEFIのみ再現） |

#### 期待される出力 vs 実際の出力

```diff
 a[0] = 0xAAAA000000000000   // テストA: 正常
 a[1] = 0xBBBB000000000000
 a[2] = 0xCCCC000000000000
-b[0] = 0x1111000000000000   // テストB: 期待
-b[1] = 0x2222000000000000
-b[2] = 0x3333000000000000
+b[0] = 0x2222000000000000   // テストB: ❌ 1つシフト
+b[1] = 0x3333000000000000
+b[2] = 0x3333000000000000   // 最終要素が重複
```

#### 推定原因

配列とポインタ変数に同一のスタックベースオフセットが割り当てられる。

#### 回避策

ローカル配列のアドレスを変数に格納しない。ASM内でスタック上にテーブルを直接構築するか、固定メモリアドレスに配列を配置する。

---

### Bug #11: インライン展開によるASMレジスタ割当変更 — 重大

**カテゴリ**: インライン展開  
**発見日**: 2026-02-16  
**対象**: Cm v0.14.1 / `--target=uefi` (x86_64)

#### 症状

`__asm__` 内で `%rdi`/`%rsi` 等のABIレジスタを直接参照し、関数パラメータがそのレジスタに配置されることを前提にすると、インライン展開時に不正動作。

#### 再現コード

```cm
//! platform: uefi

export void switch_to(ulong old_rsp_ptr, ulong new_rsp) {
    // ❌ System V ABIのrdi/rsiを直接使用 → インライン展開で破壊
    __asm__(`
        movq %rsp, (%rdi);
        movq %rsi, %rsp;
    `);
}

ulong efi_main(void* image_handle, void* system_table) {
    void* ih = image_handle;
    void* st = system_table;
    ulong old_rsp = 0;
    ulong new_rsp = 0x80000;
    switch_to(old_rsp, new_rsp);  // インライン展開→クラッシュ
    while (true) { __asm__("hlt"); }
    return 0;
}
```

#### 再現手順

```bash
cm compile --target=uefi -o .tmp/build/bug11.o test_bug11.cm
lld-link /subsystem:efi_application /entry:efi_main /out:.tmp/build/BUG11.EFI .tmp/build/bug11.o
# QEMU実行 → #GP/#UD例外が発生
```

#### 回避策

`${r:varname}` 入力変数構文でCmにレジスタ割当を任せる:

```cm
export void switch_to(ulong old_rsp_ptr, ulong new_rsp) {
    __asm__(`
        movq ${r:old_rsp_ptr}, %r10;   // ✅ Cmが適切なレジスタに配置
        movq ${r:new_rsp}, %r11;
        movq %rsp, (%r10);
        movq %r11, %rsp;
    `);
}
```

---

### Bug #12: インライン展開時のret先不在 — 重大

**カテゴリ**: インライン展開  
**発見日**: 2026-02-16  
**対象**: Cm v0.14.1 / `--target=uefi` (x86_64)

#### 症状

`__asm__` 内で `ret` 命令を使う関数がインライン展開されると、`call` 命令が省略されスタック上にreturn addressが存在しない。クラッシュ。

#### 再現コード

```cm
//! platform: uefi

export void context_switch(ulong old_rsp_ptr, ulong new_rsp) {
    __asm__(`
        movq ${r:old_rsp_ptr}, %r10;
        movq %rsp, (%r10);
        movq ${r:new_rsp}, %rsp;
        ret;   // ← インライン展開時、return addressがない
    `);
}

ulong efi_main(void* image_handle, void* system_table) {
    void* ih = image_handle;
    void* st = system_table;
    ulong old_rsp = 0;
    ulong new_rsp = 0x80000;
    context_switch(old_rsp, new_rsp);
    while (true) { __asm__("hlt"); }
    return 0;
}
```

#### 再現手順

```bash
cm compile --target=uefi -o .tmp/build/bug12.o test_bug12.cm
lld-link /subsystem:efi_application /entry:efi_main /out:.tmp/build/BUG12.EFI .tmp/build/bug12.o
# QEMU実行 → ret命令で不正アドレスにジャンプしてクラッシュ
```

#### 回避策

数値ラベルで明示的にreturn addressをpush:

```cm
__asm__(`
    leaq 1f(%rip), %rax;   // 復帰ラベルのアドレスを取得
    pushq %rax;
    movq ${r:old_rsp_ptr}, %r10;
    movq %rsp, (%r10);
    movq ${r:new_rsp}, %rsp;
    ret;                    // pushした復帰アドレスに戻る
1:
    nop
`);
```

> [!CAUTION]
> 名前付きラベル（`.Lswitch_resume:` 等）は複数回インライン展開で重複エラー。必ず数値ラベル (`1:`) を使用。

---

## 回避済バグ（workaround）

### Bug #17: `___chkstk_ms` 未定義シンボル — 中

**カテゴリ**: リンカ  
**発見日**: 2026-02-17  
**対象**: Cm v0.14.1 / `--target=uefi` (x86_64)

#### 症状

大規模な関数（4KB以上のスタックフレーム）をリンクすると、LLVMが自動挿入するスタックプローブ関数 `___chkstk_ms` が未定義シンボルとしてリンクエラーになる。

#### 再現手順

```bash
# 大規模ディスパッチ関数を含むカーネルをリンク
cm compile --target=uefi -o .tmp/build/kernel.o kernel/efi_main.cm
lld-link /subsystem:efi_application /entry:efi_main /out:.tmp/build/KERNEL.EFI .tmp/build/kernel.o
# → lld-link: error: undefined symbol: ___chkstk_ms
```

#### 回避策（必須）

`kernel/lib/chkstk.c` にno-opスタブを提供し、リンク時に含める:

```c
void ___chkstk_ms(void) { /* no-op: ベアメタルではスタックプローブ不要 */ }
```

```bash
clang -target x86_64-unknown-windows-msvc -c -o .tmp/build/chkstk.o kernel/lib/chkstk.c
lld-link /subsystem:efi_application /entry:efi_main \
    /out:.tmp/build/KERNEL.EFI .tmp/build/kernel.o .tmp/build/chkstk.o
```

> [!NOTE]
> これはLLVMの仕様上の動作であり、Cmコンパイラのバグではない。ベアメタル環境では常にこのスタブが必要。

---

## CosmOSアーキテクチャへの影響

Bug #11, #12 により、コンテキストスイッチ等の低レベルASM関数では以下の注意が必要:

1. **`${r:var}` 構文の使用必須**: ABIレジスタ (`%rdi`, `%rsi` 等) を直接参照しない
2. **`ret` 使用時の数値ラベル**: インライン展開でreturn addressを明示的にpush
3. **ループ内ASM出力変数**: Bug #5 のため、ループ内でスコープ宣言して `break` で脱出
4. **配列アドレス取得の制限**: Bug #9 のため、ローカル配列のアドレスをポインタ変数に格納しない

> [!IMPORTANT]
> これらの制約はCmコンパイラのUEFIバックエンド固有の問題であり、JITモード (`cm run`) では再現しない。
