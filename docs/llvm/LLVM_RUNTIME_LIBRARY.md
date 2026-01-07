[English](LLVM_RUNTIME_LIBRARY.en.html)

# LLVM ランタイムライブラリ

## 概要

Cm言語のLLVMバックエンドは、文字列処理やフォーマット出力のためのCランタイムライブラリを使用します。このライブラリは `src/codegen/llvm/runtime.c` に実装されており、コンパイル済みのプログラムとリンクされます。

## ファイル位置

- **ソースファイル**: `src/codegen/llvm/runtime.c`
- **コンパイル済みオブジェクト**: `.tmp/cm_runtime.o`

## ランタイムの再ビルド

ランタイムライブラリを修正した場合は、以下のコマンドで再ビルドします：

```bash
clang -c src/codegen/llvm/runtime.c -o .tmp/cm_runtime.o
```

## 関数一覧

### 文字列出力関数

| 関数名 | シグネチャ | 説明 |
|--------|-----------|------|
| `cm_print_string` | `void (const char*)` | 文字列を出力（改行なし） |
| `cm_println_string` | `void (const char*)` | 文字列を出力（改行あり、エスケープ処理含む） |

### 整数出力関数

| 関数名 | シグネチャ | 説明 |
|--------|-----------|------|
| `cm_print_int` | `void (int)` | 符号付き整数を出力（改行なし） |
| `cm_println_int` | `void (int)` | 符号付き整数を出力（改行あり） |
| `cm_print_uint` | `void (unsigned int)` | 符号なし整数を出力（改行なし） |
| `cm_println_uint` | `void (unsigned int)` | 符号なし整数を出力（改行あり） |

### 浮動小数点出力関数

| 関数名 | シグネチャ | 説明 |
|--------|-----------|------|
| `cm_print_double` | `void (double)` | 浮動小数点数を出力（改行なし） |
| `cm_println_double` | `void (double)` | 浮動小数点数を出力（改行あり） |

### ブール値出力関数

| 関数名 | シグネチャ | 説明 |
|--------|-----------|------|
| `cm_print_bool` | `void (char, char)` | ブール値を出力（第2引数: 改行フラグ） |

### 文字列フォーマット関数

| 関数名 | シグネチャ | 説明 |
|--------|-----------|------|
| `cm_format_int` | `char* (int)` | 符号付き整数を文字列に変換 |
| `cm_format_uint` | `char* (unsigned int)` | 符号なし整数を文字列に変換 |
| `cm_format_double` | `char* (double)` | 浮動小数点数を文字列に変換 |
| `cm_format_double_precision` | `char* (double, int)` | 指定精度で浮動小数点数を変換 |
| `cm_format_double_exp` | `char* (double)` | 指数表記（小文字e）で変換 |
| `cm_format_double_EXP` | `char* (double)` | 指数表記（大文字E）で変換 |
| `cm_format_bool` | `char* (char)` | ブール値を "true"/"false" に変換 |
| `cm_format_char` | `char* (char)` | 文字を1文字の文字列に変換 |

### 整数フォーマット関数（基数変換）

| 関数名 | シグネチャ | 説明 |
|--------|-----------|------|
| `cm_format_int_hex` | `char* (int)` | 16進数（小文字）に変換 |
| `cm_format_int_HEX` | `char* (int)` | 16進数（大文字）に変換 |
| `cm_format_int_binary` | `char* (int)` | 2進数に変換 |
| `cm_format_int_octal` | `char* (int)` | 8進数に変換 |

### フォーマット文字列置換関数

| 関数名 | シグネチャ | 説明 |
|--------|-----------|------|
| `cm_format_replace` | `char* (const char*, const char*)` | `{...}` を文字列で置換 |
| `cm_format_replace_int` | `char* (const char*, int)` | フォーマット指定子付きで整数を置換 |
| `cm_format_replace_uint` | `char* (const char*, unsigned int)` | フォーマット指定子付きで符号なし整数を置換 |
| `cm_format_replace_double` | `char* (const char*, double)` | フォーマット指定子付きで浮動小数点数を置換 |
| `cm_format_replace_string` | `char* (const char*, const char*)` | フォーマット指定子付きで文字列を置換 |

### 文字列操作関数

| 関数名 | シグネチャ | 説明 |
|--------|-----------|------|
| `cm_string_concat` | `char* (const char*, const char*)` | 2つの文字列を連結 |
| `cm_unescape_braces` | `char* (const char*)` | `{{` → `{`, `}}` → `}` に変換 |

## サポートされるフォーマット指定子

### 整数フォーマット

| 指定子 | 説明 | 例 |
|--------|------|-----|
| `{}` | 10進数 | `42` |
| `{:x}` | 16進数（小文字） | `2a` |
| `{:X}` | 16進数（大文字） | `2A` |
| `{:b}` | 2進数 | `101010` |
| `{:o}` | 8進数 | `52` |
| `{:<10}` | 左揃え（幅10） | `42        ` |
| `{:>10}` | 右揃え（幅10） | `        42` |
| `{:^10}` | 中央揃え（幅10） | `    42    ` |
| `{:0>5}` | ゼロ埋め（幅5） | `00042` |

### 浮動小数点フォーマット

| 指定子 | 説明 | 例 |
|--------|------|-----|
| `{}` | デフォルト | `3.14159` |
| `{:.2}` | 小数点以下2桁 | `3.14` |
| `{:.4}` | 小数点以下4桁 | `3.1416` |
| `{:e}` | 指数表記（小文字） | `3.141593e+00` |
| `{:E}` | 指数表記（大文字） | `3.141593E+00` |

### 文字列フォーマット

| 指定子 | 説明 | 例 |
|--------|------|-----|
| `{}` | そのまま | `hello` |
| `{:<10}` | 左揃え（幅10） | `hello     ` |
| `{:>10}` | 右揃え（幅10） | `     hello` |
| `{:^10}` | 中央揃え（幅10） | `  hello   ` |

## エスケープ処理

`cm_println_string` 関数は、出力前に `{{` を `{` に、`}}` を `}` に変換します。これにより、リテラルの波括弧を出力できます：

```cm
println("Literal braces: {{ and }}");
// 出力: Literal braces: { and }
```

## 使用例

### Cmコード

```cm
import std::io::println;

int main() {
    int n = 255;
    println("Decimal: {}", n);
    println("Hex: {:x}", n);
    println("Binary: {:b}", n);
    
    double pi = 3.14159;
    println("Pi: {:.2}", pi);
    
    return 0;
}
```

### 生成されるLLVM IR（概念）

```llvm
; 整数フォーマット付き置換
%format1 = call ptr @cm_format_replace_int(ptr @str.decimal, i32 %n)
call void @cm_println_string(ptr %format1)

%format2 = call ptr @cm_format_replace_int(ptr @str.hex, i32 %n)
call void @cm_println_string(ptr %format2)

; 浮動小数点フォーマット付き置換
%format3 = call ptr @cm_format_replace_double(ptr @str.pi, double %pi)
call void @cm_println_string(ptr %format3)
```

## メモリ管理

> ⚠️ **注意**: フォーマット関数 (`cm_format_*`) と置換関数 (`cm_format_replace_*`) はヒープメモリを割り当てます。現在の実装では、このメモリは自動的に解放されません。将来的にはガベージコレクションまたはアリーナアロケータの導入を予定しています。

## 関連ドキュメント

- [LLVM バックエンド実装ガイド](./llvm_backend_implementation.html)
- [フォーマット文字列のLLVM実装](./STRING_INTERPOLATION_LLVM.html)
- [FFI設計](./design/ffi.html)