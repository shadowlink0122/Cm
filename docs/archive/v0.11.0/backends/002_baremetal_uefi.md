# ベアメタル/UEFI対応設計

## 概要
no_stdモードとUEFIターゲットを完成させ、OS開発を可能にする。

## 現状分析

### 実装済み
- `BuildTarget::Baremetal` 定義
- `TargetConfig`に`noStd/noMain`フラグ
- ARM Cortex-M設定 (`getBaremetalARM()`)
- `setupNoStd()` - `__cm_panic/alloc/dealloc`宣言

### 未実装
- CLIでの`--target=uefi`解析
- UEFI x86_64ターゲット設定
- `#[no_std]`属性パース
- PE/COFF出力設定

---

## 阻害要因詳細

### 1. ランタイム関数のlibc依存
```cpp
// runtime_alloc.c
void* cm_alloc(size_t size) {
    return malloc(size);  // ❌ libc依存
}
```

**解決案**: no_stdモードではコンパイルエラーにする

### 2. println系関数
```cpp
// 現在: cm_println_string -> printf
// 必要: 抽象化レイヤー
void cm_println_string(const char* s) {
    #ifdef CM_NO_STD
    // ユーザー提供の出力関数を呼び出し
    __cm_write(s, strlen(s));
    #else
    puts(s);
    #endif
}
```

### 3. エントリポイント
```cpp
// 現在: main() のみ
// UEFI: efi_main(EFI_HANDLE, EFI_SYSTEM_TABLE*) が必要
```

---

## 実装計画

### Phase 1: CLIとターゲット設定
1. `--target=uefi`オプション追加
2. `TargetConfig::getUEFI()`メソッド追加
3. `x86_64-unknown-uefi`トリプル設定

```cpp
static TargetConfig getUEFI() {
    return {
        .target = BuildTarget::Baremetal,
        .triple = "x86_64-unknown-uefi",
        .cpu = "x86-64",
        .features = "",
        .dataLayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128",
        .noStd = true,
        .noMain = true,
        .optLevel = 2
    };
}
```

### Phase 2: no_std属性
```cm
#[no_std]  // libc関数使用禁止
fn _start() -> ! {
    loop {}
}
```

### Phase 3: UEFIライブラリ
```cm
// std/uefi.cm
typedef EFI_HANDLE = void*;

struct EFI_SYSTEM_TABLE {
    // ... UEFI仕様に準拠
}

fn efi_main(handle: EFI_HANDLE, st: *EFI_SYSTEM_TABLE) -> uint;
```

---

## テスト計画
1. 空のUEFIプログラムがPE/COFF形式で出力されること
2. QEMUでUEFI Hello Worldが動作すること
