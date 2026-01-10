[English](target_attribute_redesign.en.html)

# ターゲット属性の再設計提案

## 問題点
現在の `#[target(js)]` のような関数レベルの属性は以下の問題がある：
- 関数単位では粒度が細かすぎる
- ファイル全体の特性を示せない
- エディタでの解析が困難

## 新しい設計

### 1. ファイルレベル属性（推奨）

```cm
#![target(wasm)]        // ファイル全体がWASM専用
#![no_std]              // 標準ライブラリなし
#![no_main]             // mainなし（ライブラリ/カーネル）

// 以下のコード全体に適用される
struct WasmSpecificType {
    // ...
}
```

### 2. モジュールレベル条件コンパイル

```cm
// platform.cm
#[cfg(target = "native")]
module native {
    export void platform_init() {
        // OS固有の初期化
    }
}

#[cfg(target = "wasm")]
module wasm {
    export void platform_init() {
        // WASM固有の初期化
    }
}

#[cfg(target = "no_std")]
module bare {
    export void platform_init() {
        // ベアメタル初期化
    }
}
```

### 3. 条件付きエクスポート

```cm
// 全プラットフォーム共通
export void common_function() { }

// 特定プラットフォームのみ
#[cfg(target = "js")]
export void js_only_function() { }

// 複数プラットフォーム
#[cfg(any(target = "native", target = "wasm"))]
export void native_or_wasm() { }
```

### 4. UEFI専用の例

```cm
#![no_std]
#![no_main]
#![target(uefi)]

// UEFIのABI
#[no_mangle]
#[calling_convention("efiapi")]
extern "C" EFI_STATUS efi_main(
    EFI_HANDLE ImageHandle,
    *EFI_SYSTEM_TABLE SystemTable
) {
    // UEFI Hello World
    SystemTable->ConOut->OutputString(
        SystemTable->ConOut,
        u"Hello from Cm!\r\n"
    );

    return EFI_SUCCESS;
}
```

## 実装順序

### Phase 1: 基本属性（1-2週間）
- `#![no_std]` の実装
- `#![no_main]` の実装
- `#[no_mangle]` の実装

### Phase 2: 条件コンパイル（2-4週間）
- `#[cfg()]` 属性の実装
- `any()`, `all()`, `not()` の実装
- ターゲット判定ロジック

### Phase 3: ABI制御（1ヶ月）
- `#[calling_convention()]` の実装
- UEFI/stdcall/fastcallサポート
- volatile型の実装

## エディタサポート

### Language Server Protocol (LSP)での実装
```json
{
    "file_attributes": {
        "target": "wasm",
        "no_std": true
    },
    "diagnostics": [
        {
            "message": "malloc is not available in no_std mode",
            "severity": "error",
            "range": {...}
        }
    ]
}
```

### シンタックスハイライト
```cm
// グレーアウトされる（現在のターゲットがwasmでない場合）
#[cfg(target = "wasm")]
void wasm_only() {
    // この部分もグレーアウト
}
```

## 利点

1. **明確性**: ファイル全体の特性が一目でわかる
2. **効率性**: コンパイラが不要なコードを早期に除外
3. **保守性**: プラットフォーム固有コードの分離が容易
4. **エディタ対応**: LSPで適切なエラー検出が可能

## 互換性

既存のコードとの互換性を保つため：
- 関数レベルの `#[target()]` は非推奨だが動作
- 移行期間中は警告を表示
- マイグレーションツールを提供