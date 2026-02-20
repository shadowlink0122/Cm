[English](uefi_hello_world_roadmap.en.html)

# UEFI Hello World 実現ロードマップ

## 目標
CmでUEFIアプリケーション（Hello World）を実装し、実機/QEMUで動作させる。

## 現状と必要機能の差分

### ✅ 既に実装済み
- 基本的な型システム
- 構造体定義
- `extern "C"` 関数宣言
- ポインタ操作
- 関数ポインタ（基本）

### ❌ 実装が必要な言語機能

#### 優先度：高（UEFI必須）
1. **ワイド文字列リテラル**
   ```cm
   u"Hello, UEFI!\r\n"  // UTF-16文字列
   ```

2. **関数ポインタの完全サポート**
   ```cm
   typedef EFI_STATUS (*EFI_TEXT_OUTPUT_STRING)(
       *EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL This,
       *uint16 String
   );
   ```

3. **calling convention**
   ```cm
   #[calling_convention("ms_abi")]  // x86_64 UEFI
   ```

4. **no_mangle属性**
   ```cm
   #[no_mangle]
   extern "C" EFI_STATUS efi_main(...) { }
   ```

#### 優先度：中（品質向上）
5. **volatile型**
   ```cm
   volatile uint32* reg = 0xFED00000;
   ```

6. **packed構造体**
   ```cm
   #[repr(C, packed)]
   struct EFI_TABLE_HEADER {
       uint64 Signature;
       uint32 Revision;
       // ...
   }
   ```

## 最小実装での UEFI Hello World

現在のCmで可能な最小実装：

```cm
// hello_uefi.cm
// 注意: 疑似コード（現在のCmでは完全には動作しない）

typedef uint64 EFI_STATUS;
typedef void* EFI_HANDLE;

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    void* Reset;                // 未使用
    void* OutputString;          // 関数ポインタ（要改善）
    // 他のメンバーは省略
};

struct EFI_SYSTEM_TABLE {
    char Signature[8];
    uint32 Revision;
    uint32 HeaderSize;
    uint32 CRC32;
    uint32 Reserved;
    void* FirmwareVendor;
    uint32 FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    void* ConIn;
    EFI_HANDLE ConsoleOutHandle;
    *EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL ConOut;
};

// エントリポイント（要: #[no_mangle]）
extern "C" EFI_STATUS efi_main(
    EFI_HANDLE ImageHandle,
    *EFI_SYSTEM_TABLE SystemTable
) {
    // 問題1: ワイド文字列リテラルが未実装
    // 問題2: 関数ポインタ経由の呼び出しが不完全
    // 問題3: calling conventionが指定できない

    // ワークアラウンド: バイト配列で文字列を定義
    uint16 hello[] = {
        72, 101, 108, 108, 111, 44, 32,  // "Hello, "
        85, 69, 70, 73, 33, 13, 10, 0    // "UEFI!\r\n\0"
    };

    // 本来はこう書きたい:
    // SystemTable->ConOut->OutputString(SystemTable->ConOut, u"Hello, UEFI!\r\n");

    return 0; // EFI_SUCCESS
}
```

## 実装計画

### Step 1: 言語機能の追加（2-3週間）
1. ワイド文字列リテラル（`u"..."`)
2. `#[no_mangle]` 属性
3. 関数ポインタの改善

### Step 2: CodeGen対応（1-2週間）
1. UEFIターゲット追加（`--target=uefi`)
2. PE32+形式の生成
3. calling convention対応

### Step 3: ランタイム（1週間）
1. UEFIエントリポイント生成
2. 基本的なUEFIヘッダファイル（.cm）

### Step 4: テストと検証（1週間）
1. QEMUでの動作確認
2. 実機での動作確認（オプション）

## 代替案：段階的アプローチ

### Option 1: Cインターフェース経由
```c
// uefi_wrapper.c
#include <efi.h>
#include <efilib.h>

EFI_STATUS cm_print_string(EFI_SYSTEM_TABLE *SystemTable, const CHAR16 *str) {
    return SystemTable->ConOut->OutputString(SystemTable->ConOut, str);
}
```

```cm
// main.cm
extern "C" EFI_STATUS cm_print_string(*EFI_SYSTEM_TABLE st, *uint16 str);

extern "C" EFI_STATUS efi_main(EFI_HANDLE h, *EFI_SYSTEM_TABLE st) {
    // Cラッパー経由で呼び出し
    return cm_print_string(st, /* 文字列 */);
}
```

### Option 2: アセンブリブリッジ
最小限のアセンブリでエントリポイントを実装し、Cm関数を呼び出す。

## 推奨事項

1. **短期的には**: Cラッパー経由でUEFI機能を利用
2. **中期的には**: 必要な言語機能を段階的に実装
3. **長期的には**: 完全なno_stdサポートとUEFIバインディング

## まとめ

現在のCmでは直接的なUEFI Hello Worldは困難だが、以下の追加で実現可能：
- ワイド文字列リテラル
- `#[no_mangle]` 属性
- calling convention指定
- 改善された関数ポインタサポート

これらは他のベアメタル開発にも有用なため、優先的に実装する価値がある。