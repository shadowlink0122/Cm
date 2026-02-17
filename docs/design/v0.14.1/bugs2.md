# FBConsoleが`impl`パターンを使用できない理由

> Cm v0.14.1 / `--target=uefi` (x86_64) / 2026-02-17確認
>
> 関連: [cm_compiler_bugs_open.md](./cm_compiler_bugs_open.md) Bug #10

---

## 背景

CosmOSのFBConsole（フレームバッファテキストコンソール）は、グローバル固定アドレスに
配置された構造体をポインタ経由で操作する。本来であれば`impl`パターンで
メソッド呼び出し(`con->println("Hello")`)を使用したいが、Cmコンパイラの
UEFIターゲットにおけるimplメソッド生成に3つのバグがあり、現状はフリー関数パターンを
使用している。

```cm
// 理想的なAPI（現状はUEFIで使用不可）
FBConsole* con = fb_get_global_console();
con->clear();
con->set_color(0x0000FF00);
con->println("Hello" as void*);

// 現在のフリー関数API（回避策）
FBConsole* con = fb_get_global_console();
fb_clear(con);
fb_set_color(con, 0x0000FF00);
fb_println(con, "Hello" as void*);
```

---

## 3つのバグ概要

| # | エラー名 | 発生条件 | JIT | UEFI |
|---|---------|---------|-----|------|
| 1 | self書き戻し不在 | ポインタ経由でimplメソッド呼び出し | ✅ 修正済 | ❌ 未修正 |
| 2 | GEPインデックス上限 | 構造体フィールド6個以上のimplメソッド | ✅ 正常 | ❌ エラー |
| 3 | Load operand不正 | ポインタ経由のimplメソッド呼び出し | ✅ 正常 | ❌ エラー |

> [!IMPORTANT]
> 3つのバグはそれぞれ独立している。1を回避しても2・3が発生し、
> 2を回避しても3が発生する。UEFIターゲットのimpl LLVM IR生成に
> 根本的な問題がある。

---

## バグ1: self書き戻し不在

### 症状

`impl`メソッド内で`self.field = value`を実行しても、ポインタ経由で
呼び出した場合に元の構造体に反映されない。

### 再現コード

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

    // ローカル変数経由: 正常（JIT修正で動作）
    Counter local_counter;
    local_counter.value = 0;
    local_counter.increment();
    serial.puts("local: " as void*);
    serial.print_hex(local_counter.get_value() as ulong);
    serial.println("" as void*);  // 期待: 0x1 ✅

    // ポインタ経由: バグ
    Counter ptr_counter;
    ptr_counter.value = 0;
    Counter* ptr = &ptr_counter as Counter*;
    ptr->increment();              // ← self.value += 1 が反映されない
    serial.puts("pointer: " as void*);
    serial.print_hex(ptr_counter.get_value() as ulong);
    serial.println("" as void*);  // 期待: 0x1, 実際: 0x0 ❌

    while (true) { __asm__("hlt"); }
    return 0;
}
```

### デバッグトレース結果

FBConsoleをimplに変換した際のシリアル出力:

```
[DEBUG] gcon->fb_addr = 0x0000000000000000   ← init()のself書き込みが消失
[DEBUG] gcon->width   = 0x0000000000000000   ← 全フィールドが未初期化
```

`con->init(fb, w, h, s)` 内で `self.fb_addr = fb_addr` を実行しても、
呼び出し元の構造体には反映されず、全フィールドが0のまま。
結果として `fb_addr == 0` による早期リターンで全描画が無効になる。

---

## バグ2: GEPインデックス上限エラー

### 症状

構造体のフィールドが6個以上ある場合、implメソッドからフィールドインデックス5以上に
アクセスすると`getelementptr`命令が不正になる。

### 再現コード

```cm
//! platform: uefi

export struct LargeStruct {
    ulong field0;    // インデックス 0
    ulong field1;    // インデックス 1
    ulong field2;    // インデックス 2
    ulong field3;    // インデックス 3
    ulong field4;    // インデックス 4
    ulong field5;    // インデックス 5 ← ここからエラー
    ulong field6;
    ulong field7;
}

impl LargeStruct {
    void set_all() {
        self.field0 = 0;
        self.field1 = 1;
        self.field2 = 2;
        self.field3 = 3;
        self.field4 = 4;
        self.field5 = 5;  // ← GEPエラー
        self.field6 = 6;  // ← GEPエラー
        self.field7 = 7;  // ← GEPエラー
    }
}

ulong efi_main(void* image_handle, void* system_table) {
    LargeStruct s;
    s.set_all();
    return 0;
}
```

### エラーメッセージ

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
cm compile --target=uefi -o .tmp/build/gep_test.o gep_test.cm
# → LLVM module verification failed
```

### FBConsoleへの影響

FBConsoleは8フィールド。インデックス0-4はアクセス可能だが、
`cursor_y`(5)、`fg_color`(6)、`bg_color`(7)はGEPエラーになる。

```cm
export struct FBConsole {
    ulong fb_addr;     // 0 ✅
    ulong width;       // 1 ✅
    ulong height;      // 2 ✅
    ulong stride;      // 3 ✅
    ulong cursor_x;    // 4 ✅
    ulong cursor_y;    // 5 ❌ GEPエラー
    ulong fg_color;    // 6 ❌ GEPエラー
    ulong bg_color;    // 7 ❌ GEPエラー
}
```

### 試みた回避策: フィールドパック

width/heightを1つの`ulong`にビットパックし、5フィールドに削減:

```cm
export struct FBConsole {
    ulong fb_addr;    // 0
    ulong screen;     // 1: width(下位32bit) | height(上位32bit)
    ulong stride;     // 2
    ulong cursor;     // 3: cursor_x(下位32bit) | cursor_y(上位32bit)
    ulong colors;     // 4: fg_color(下位32bit) | bg_color(上位32bit)
}
```

→ **GEPエラーは解消**。しかしバグ3が発生。

---

## バグ3: Load operand不正エラー

### 症状

GEPエラーを回避してフィールド数を5個以下にしても、ポインタ経由の
implメソッドの書き戻し処理で`load`命令のオペランド型が不整合になる。

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

### エラーメッセージ

```
LLVM module verification failed:
Load operand must be a pointer.
  %0 = load ptr, i64 %load, align 8
```

### 推定原因

implメソッドのself書き戻し処理で、コンパイラが生成するLLVM IRの`load`命令が
ポインタではなく整数値(`i64`)をオペランドとして使用している。
JITバックエンドでは正しいポインタ型が生成されるが、UEFIバックエンドでは
型情報が欠落または不正変換されている。

---

## 検証マトリクス

実際にCosmOSで試行した結果:

| 試行 | 構造体 | 呼び出し | 結果 |
|------|--------|---------|------|
| A | 8フィールド + impl | `con->method()` | ❌ GEPエラー(バグ2) |
| B | 5フィールド(パック) + impl | `con->method()` | ❌ Load operand(バグ3) |
| C | 5フィールド + impl | `fb_init_global`内で`con->init()` | ❌ Load operand(バグ3) |
| D | 8フィールド + フリー関数 | `fb_clear(con)` | ✅ 正常 |

---

## 回避策: フリー関数パターン

```cm
// NG: impl（UEFIでコンパイルエラーまたはself書き戻し不在）
impl FBConsole {
    void init(ulong fb_addr, ...) { self.fb_addr = fb_addr; }
    void clear() { ... self.cursor_x = 0; ... }
    void puts(void* data) { ... self.putc(byte_val); ... }
}
FBConsole* con = addr as FBConsole*;
con->init(fb, ...);   // ❌ LLVMエラーまたはself未反映

// OK: フリー関数（ポインタ経由で確実に更新）
export void fb_init(FBConsole* con, ulong fb_addr, ...) {
    con->fb_addr = fb_addr;   // ✅ 直接ポインタ書き込み
}
export void fb_clear(FBConsole* con) {
    ... con->cursor_x = 0; ... // ✅ 直接ポインタ書き込み
}
```

---

## implが安全に使えるケースと使えないケース

### ✅ 安全に使えるケース

| 条件 | 例 |
|------|-----|
| ローカル変数上の構造体 | `Counter a; a.increment();` |
| selfフィールドを**変更しない**メソッド | getter、I/O操作のみ等 |
| JITモードでの実行 | `cm run test.cm` |

### ❌ 使えないケース

| 条件 | 例 |
|------|-----|
| ポインタ経由 + self変更 | `ptr->init(...)` でself.fieldに代入 |
| 構造体フィールド6個以上 | selfの全フィールドにアクセスする場合 |
| UEFIターゲット全般 | コンパイルエラーのリスクあり |

### CosmOSモジュール別対応状況

| モジュール | パターン | 理由 |
|-----------|---------|------|
| FBConsole | フリー関数 | 固定アドレスのポインタ経由（本バグ）|
| SerialPort | `impl` ✅ | selfフィールド変更なし（I/Oポートのみ）|
| DebugPort | `impl` ✅ | selfフィールド変更なし |
| TestRunner | `impl` ✅ | スタック上ローカル変数 |
| PMM | `impl` ✅ | ビットマップ操作は直接メモリ変更 |
| Heap | `impl` ✅ | ネストなし |

---

## 修正に必要な対応（コンパイラ側）

1. **self書き戻しのLLVM IR生成修正**: UEFIバックエンドで、ポインタ経由の
   implメソッド呼び出し時にselfのコピーバックが正しく生成されるようにする
2. **GEP構造体型登録の修正**: implメソッド内での構造体フィールド数が
   正しくLLVM型定義に反映されるようにする
3. **Load/Store型整合性の修正**: self書き戻しの`load`命令がポインタ型を
   正しく使用するようにする
