# Bug #10: ポインタ経由`impl`メソッドで`self`変更が構造体に反映されない（重大）

> 詳細調査レポート — Cm v0.14.1、`--target=uefi` (x86_64)
>
> バグ一覧: [cm_compiler_bugs_open.md](./cm_compiler_bugs_open.md)

**発見日**: 2026-02-17  
**再現確認**: Cm v0.14.1（2026-02-17）  
**追加調査日**: 2026-02-17  

---

## 概要

`impl`メソッドをポインタ経由（`ptr->method()`）で呼んだ場合、
メソッド内での`self`フィールド変更が元の構造体に書き戻されない。

JITモードのローカル変数経由（`obj.method()`）では**修正済み**。  
**UEFIターゲットでは以下の3つの問題が残存**:

| # | エラー | 条件 |
|---|--------|------|
| 1 | self書き戻し不在 | `ptr->init()` での `self.field = value` が未反映 |
| 2 | GEPインデックス上限 | 構造体フィールド6個以上で `Invalid indices for GEP` |
| 3 | Load operand不正 | 5フィールド以下でも `Load operand must be a pointer` |

---

## 再現条件

| 環境 | impl（ローカル変数） | impl（ポインタ経由） | フリー関数 |
|------|---------------------|---------------------|-----------|
| **JIT** (`cm run`) | ✅ 正常 | ✅ 正常 | ✅ 正常 |
| **UEFI** (`--target=uefi`) | ✅ 正常 | ❌ **バグ** | ✅ 正常 |

---

## 問題1: self書き戻し不在

### 症状
`impl`メソッド内で `self.field = value` を実行しても、
ポインタ経由で呼び出した場合に元の構造体に反映されない。

### デバッグトレース結果（UEFIターゲット）

```
[DEBUG] gcon->fb_addr = 0x0000000000000000   ← init()のself書き込みが消失
[DEBUG] gcon->width   = 0x0000000000000000   ← 全フィールドが未初期化
[DEBUG] gcon->cursor_x = 0x0000000000000000
[DEBUG] after clear: cursor_x = 0x0000000000000000  ← fb==0で早期リターン
[DEBUG] after puts: cursor_x = 0x0000000000000000, cursor_y = 0x0000000000000000
```

### 最小再現コード

```cm
//! platform: uefi
import ../kernel/drivers/serial;

struct Counter { int value; }

impl Counter {
    void increment() { self.value = self.value + 1; }
    int get_value() { return self.value; }
}

ulong efi_main(void* image_handle, void* system_table) {
    SerialPort serial(0x3F8);

    // ローカル変数経由: 正常
    Counter local_counter;
    local_counter.value = 0;
    local_counter.increment();
    serial.puts("local: " as void*);
    serial.print_hex(local_counter.get_value() as ulong);
    serial.println("" as void*);  // 期待: 0x1

    // ポインタ経由: バグ
    Counter ptr_counter;
    ptr_counter.value = 0;
    Counter* ptr = &ptr_counter as Counter*;
    ptr->increment();              // ← self.value += 1 が反映されない
    serial.puts("pointer: " as void*);
    serial.print_hex(ptr_counter.get_value() as ulong);
    serial.println("" as void*);  // 期待: 0x1, 実際: 0x0

    while (true) { __asm__("hlt"); }
    return 0;
}
```

### 再現手順

```bash
cm compile --target=uefi -o .tmp/build/bug10.o .tmp/bug10_test.cm
lld-link /subsystem:efi_application /entry:efi_main /out:.tmp/build/BUG10.EFI .tmp/build/bug10.o
# .tmp/esp/EFI/BOOT/BOOTX64.EFI にコピーしてQEMU起動
```

---

## 問題2: GEPインデックス上限エラー

### 症状
構造体のフィールドが6個以上ある場合、implメソッドからフィールドインデックス5以上にアクセスすると
LLVM verification errorが発生する。

### 再現コード

```cm
//! platform: uefi

export struct LargeStruct {
    ulong field0;
    ulong field1;
    ulong field2;
    ulong field3;
    ulong field4;
    ulong field5;     // ← インデックス5
    ulong field6;     // ← インデックス6
    ulong field7;     // ← インデックス7
}

impl LargeStruct {
    void set_all() {
        self.field0 = 0;
        self.field1 = 1;
        self.field2 = 2;
        self.field3 = 3;
        self.field4 = 4;
        self.field5 = 5;  // ← ここでGEPエラー
        self.field6 = 6;
        self.field7 = 7;
    }
}

ulong efi_main(void* image_handle, void* system_table) {
    LargeStruct s;
    s.set_all();
    return 0;
}
```

### エラー出力

```
LLVM module verification failed:
Invalid indices for GEP pointer type!
  %field_ptr8 = getelementptr %LargeStruct, ptr %arg0, i32 0, i32 5
Invalid indices for GEP pointer type!
  %field_ptr9 = getelementptr %LargeStruct, ptr %arg0, i32 0, i32 6
Invalid indices for GEP pointer type!
  %field_ptr10 = getelementptr %LargeStruct, ptr %arg0, i32 0, i32 7
```

### 再現手順

```bash
cm compile --target=uefi -o .tmp/build/gep_test.o .tmp/gep_test.cm
# → LLVM module verification failed
```

> [!NOTE]
> フリー関数パターン（`void set_all(LargeStruct* s) { s->field5 = 5; }`）では
> フィールド数に関わらず正常にコンパイルされる。

---

## 問題3: Load operand不正エラー

### 症状
問題2を回避するためにフィールド数を5個以下にパックしても、
implメソッドのself書き戻しで`load`命令のオペランド型不整合が発生する。

### 再現コード

```cm
//! platform: uefi

export struct SmallStruct {
    ulong field0;
    ulong field1;
    ulong field2;
    ulong field3;
    ulong field4;
}

impl SmallStruct {
    void init(ulong a, ulong b) {
        self.field0 = a;
        self.field1 = b;
    }
}

export void setup_global() {
    SmallStruct* ptr = 0xA000 as SmallStruct*;
    ptr->init(42, 100);   // ← ポインタ経由のimpl呼び出し
}

ulong efi_main(void* image_handle, void* system_table) {
    setup_global();
    return 0;
}
```

### エラー出力

```
LLVM module verification failed:
Load operand must be a pointer.
  %0 = load ptr, i64 %load, align 8
```

> [!IMPORTANT]
> 問題2（GEP上限）と問題3（Load operand）は独立したバグ。
> フィールド数に関わらず、**UEFIターゲットでのimplメソッド+ポインタ経由呼び出し**は
> LLVM IR生成に根本的な問題がある。

---

## 推定原因

implメソッドのLLVMバックエンド生成で、`self`パラメータの扱いがターゲット依存:

- **JIT**: `self`はコピーを受け取り、メソッド終了時にコピーバック（v0.14.1で修正済み）
- **UEFIターゲット**: `self`のコピーバック生成で以下が不正:
  - self構造体型の登録がフィールド5個分にしか対応していない（GEPエラー）
  - コピーバックの`load`/`store`で型情報が`i64`等の整数値になっている（Load operandエラー）

---

## 回避策

```cm
// NG: impl（UEFIでコンパイルエラーまたはself書き戻し不在）
impl FBConsole {
    void init(ulong fb_addr, ...) { self.fb_addr = fb_addr; }
}
FBConsole* con = addr as FBConsole*;
con->init(fb, ...);   // UEFIではLLVMエラーまたはself未反映

// OK: フリー関数（ポインタ経由で確実に更新）
export void fb_init(FBConsole* con, ulong fb_addr, ...) {
    con->fb_addr = fb_addr;   // 直接ポインタ書き込み
}
```

---

## CosmOSでの対応状況

| モジュール | パターン | 理由 |
|-----------|---------|------|
| TestRunner | `impl` ✅ | スタック上ローカル変数（JIT修正済みと同等） |
| FBConsole | フリー関数 | 固定アドレスのポインタ経由（本バグ） |
| PMM | `impl` ✅ | ビットマップ操作はポインタ経由でメモリ直接変更 |
| Heap | `impl` ✅ | ネストなし |
| SerialPort | `impl` ✅ | selfフィールド変更なし（I/Oポートのみ） |

### implが安全に使えるケース
- ローカル変数上の構造体に対してメソッドを呼ぶ場合
- selfフィールドを変更しないメソッド（getter、I/Oのみ等）

### implが使えないケース
- ポインタ経由での呼び出し（`ptr->method()`）で`self`を変更する場合
- UEFIターゲットでの任意のimplメソッド（コンパイル自体が失敗する可能性）
