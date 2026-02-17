# Cm コンパイラ v0.14.1 — 未修正バグ・制約事項

> CosmOS開発中に発見された、現時点で未修正のバグ・制約をまとめたドキュメント。
> 対象: Cm v0.14.1、`--target=uefi` (x86_64)、macOS環境
>
> 修正済みバグは [cm_compiler_bugs_resolved.md](./cm_compiler_bugs_resolved.md) を参照。

## 一覧

| # | バグ | 重要度 | 回避策 |
|---|------|--------|--------|
| 1 | 3引数関数でのポインタ破損 | 高 | 固定アドレス経由 |
| 5 | `__asm__`出力変数のwhile条件不具合 | 重大 | ~~ループ内スコープ宣言~~ **修正済み** |
| 7 | `must { __asm__() }` の制御フロー干渉 | 低 | mustなしで直接使用 |
| 10b | ポインタ経由implメソッドでself変更が消失 (UEFI) | 重大 | フリー関数パターン |
| 11 | インライン展開によるASMレジスタ割当変更 | 重大 | `${r:var}` 構文 |
| 12 | インライン展開時のret先不在 | 重大 | 数値ラベルでpush |

---

## 1. 3引数関数でのポインタ破損

**発見日**: 2026-02-16  
**重要度**: 高

### 症状
構造体ポインタを3番目の引数として渡すと、呼び出し先で値が破損する。

```cm
// 3番目の引数 pmm が 0x0D に化ける
sched_spawn(entry_fn, pmm)  // 2引数 → task_create(count, entry_fn, pmm) を呼ぶ
```

### 再現条件
- 関数Aが2引数で受け取り、内部で関数Bを3引数で呼ぶ
- 3番目の引数が構造体ポインタ（`PMM*`など）
- UEFI（Windows x64 ABI: RCX, RDX, R8, R9）環境

### 回避策
ポインタを固定メモリアドレスに保存し、関数内部で読み取る方式に変更。

---

## 7. `must { __asm__() }` の制御フロー干渉

**発見日**: 2026-02-16  
**重要度**: 低

### 症状
`must { }` ブロックで `__asm__` をラップすると、ASM出力変数や制御フローに悪影響。

- `must`内の`__asm__`出力変数がスコープ外で正常に動作しない
- `while(true) { must { __asm__("hlt"); } }` がhaltループから脱出する

### 回避策
```cm
// mustなしで直接使用
__asm__("hlt");
```

---

## 10b. ポインタ経由`impl`メソッドで`self`変更が反映されない (UEFI)

**発見日**: 2026-02-17  
**重要度**: 重大

### 症状
`impl`メソッドをポインタ経由（`ptr->method()`）で呼んだ場合、
メソッド内での`self`フィールド変更が元の構造体に書き戻されない。

JITモードのローカル変数経由（`obj.method()`）では修正済み（Bug #10）。

> [!IMPORTANT]
> バグの本質は「ネスト呼び出し」ではなく、**ポインタ経由の`impl`メソッドで`self`書き戻しが行われない**こと。

### 回避策
```cm
// NG: impl（ポインタ経由でself書き戻しが行われない）
impl FBConsole {
    void init(ulong fb_addr, ...) { self.fb_addr = fb_addr; }
}
FBConsole* con = addr as FBConsole*;
con->init(fb, ...);  // self.fb_addr = 0 のまま

// OK: フリー関数（ポインタ経由で確実に更新）
export void fb_init(FBConsole* con, ulong fb_addr, ...) {
    con->fb_addr = fb_addr;
}
```

---

## 11. インライン展開によるASMレジスタ割当変更

**発見日**: 2026-02-16  
**重要度**: 重大

### 症状
`__asm__` 内で `%rdi` や `%rsi` を直接参照すると、関数がインライン展開された場合に
パラメータが別のレジスタに配置され不正動作になる。

### 回避策
`${r:varname}` 入力変数構文を使用してCmの変数をASMレジスタにバインドする:

```cm
export void switch_to(ulong old_rsp_ptr, ulong new_rsp) {
    __asm__(`
        movq ${r:old_rsp_ptr}, %r10;
        movq ${r:new_rsp}, %r11;
    `);
}
```

---

## 12. インライン展開時のret先不在

**発見日**: 2026-02-16  
**重要度**: 重大

### 症状
`__asm__` 内で `ret` 命令を使う関数がインライン展開されると、`call` 命令が省略されるため
スタック上にreturn addressが存在しない。

### 回避策
数値ラベル（`1:` / `1f`）を使って明示的に復帰アドレスをpush:

```cm
__asm__(`
    leaq 1f(%rip), %rax;
    pushq %rax;
    // ... pushes + RSP switch + pops ...
    ret;
1:
    nop
`);
```

> [!CAUTION]
> 名前付きラベルは複数回インライン展開された場合に重複エラーになる。
> 必ず数値ラベル（`1:`）を使用すること。

---

## 影響範囲

| 項目 | 値 |
|-----|-----|
| **Cmバージョン** | v0.14.1（2026-02-17時点） |
| **ターゲット** | `--target=uefi` (x86_64 PE/COFF) |
| **リンカ** | lld-link |
| **OS** | macOS (Apple Silicon) |
| **影響** | 3引数ポインタ破損、ASM制御フロー干渉、FBConsole impl化不可、コンテキストスイッチ不正動作 |
