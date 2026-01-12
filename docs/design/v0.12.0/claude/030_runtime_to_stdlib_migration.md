# cm_* ランタイム関数の標準ライブラリ化設計

作成日: 2026-01-11
対象バージョン: v0.11.0
ステータス: 設計提案

## 概要

現在Cで実装されているランタイム関数（`cm_print_string`、`cm_format_int`等）を、Cm言語自体で実装された標準ライブラリに移行する設計案です。これにより、生成されたコードから不自然な`cm_*`プレフィックスの関数呼び出しを排除し、より自然な標準ライブラリAPIを提供します。

## 現状の問題点

### 1. 不自然な関数名
```llvm
; 現在の生成コード
call void @cm_print_string(i8* %str)
call i8* @cm_format_int(i32 %value)
```

### 2. ランタイムとライブラリの境界が曖昧
- C実装のランタイム：低レベル機能
- Cm実装の標準ライブラリ：高レベル機能
- この境界が不明確

### 3. ドッグフーディングの不足
- Cm言語の標準ライブラリがC言語で実装されている矛盾

## 提案する設計

### 設計方針

1. **最小限のランタイム**
   - システムコールラッパーのみC実装
   - メモリ管理、I/Oの最低限の機能

2. **Cmで実装された標準ライブラリ**
   - すべての高レベル機能をCmで実装
   - 型安全で読みやすいAPI

3. **段階的な移行**
   - 既存の互換性を保ちながら移行

## アーキテクチャ

```
┌─────────────────────────────────────────┐
│         Cmユーザーコード                 │
└────────────────┬────────────────────────┘
                 │
┌────────────────▼────────────────────────┐
│      Cm標準ライブラリ (std)             │
│  - std::io::print, println              │
│  - std::fmt::format                     │
│  - std::string::String                  │
│  - std::mem::alloc, dealloc             │
└────────────────┬────────────────────────┘
                 │
┌────────────────▼────────────────────────┐
│    最小ランタイム (intrinsics)          │
│  - __syscall_write                      │
│  - __syscall_read                       │
│  - __syscall_malloc                     │
│  - __syscall_free                       │
└─────────────────────────────────────────┘
```

## 実装計画

### Phase 1: Intrinsics層の定義（1週間）

```cm
// std/intrinsics.cm
module std::intrinsics;

// システムコールラッパー（C実装を呼び出す）
extern "C" {
    int __syscall_write(int fd, void* buf, int count);
    int __syscall_read(int fd, void* buf, int count);
    void* __syscall_malloc(int size);
    void __syscall_free(void* ptr);
}

// Cm向けのラッパー
export int write(int fd, void* buf, int count) {
    return __syscall_write(fd, buf, count);
}

export void* malloc(int size) {
    return __syscall_malloc(size);
}

export void free(void* ptr) {
    __syscall_free(ptr);
}
```

### Phase 2: I/O層の実装（2週間）

```cm
// std/io/writer.cm
module std::io;

import std::intrinsics;
import std::string::String;

// Writerトレイト
export interface Writer {
    int write(byte[] data);
    int write_str(string s);
}

// 標準出力
export struct Stdout {}

impl Stdout for Writer {
    int write(byte[] data) {
        return intrinsics::write(1, data.ptr, data.len);
    }

    int write_str(string s) {
        return self.write(s.as_bytes());
    }
}

// グローバル関数
export void print(string s) {
    Stdout stdout;
    stdout.write_str(s);
}

export void println(string s) {
    Stdout stdout;
    stdout.write_str(s);
    stdout.write_str("\n");
}
```

### Phase 3: フォーマット層の実装（3週間）

```cm
// std/fmt/format.cm
module std::fmt;

import std::string::String;
import std::io;

// Displayトレイト
export interface Display {
    String to_string();
}

// 基本型への実装
impl int for Display {
    String to_string() {
        // 整数→文字列変換ロジック
        String result;
        int value = self;

        if (value == 0) {
            return "0";
        }

        bool negative = value < 0;
        if (negative) {
            value = -value;
        }

        char[32] buffer;
        int i = 31;

        while (value > 0) {
            buffer[i] = '0' + (value % 10);
            value = value / 10;
            i--;
        }

        if (negative) {
            buffer[i] = '-';
            i--;
        }

        return String::from_chars(&buffer[i+1], 31 - i);
    }
}

impl double for Display {
    String to_string() {
        // 浮動小数点→文字列変換
        // snprintf相当の実装
    }
}

// フォーマット関数
export String format(string fmt, Display... args) {
    String result;
    int arg_index = 0;
    int i = 0;

    while (i < fmt.len) {
        if (fmt[i] == '{' && fmt[i+1] == '}') {
            if (arg_index < args.len) {
                result.append(args[arg_index].to_string());
                arg_index++;
            }
            i += 2;
        } else {
            result.push(fmt[i]);
            i++;
        }
    }

    return result;
}
```

### Phase 4: コンパイラ側の変更（2週間）

#### 4.1 MIR生成の変更

```cpp
// src/mir/lowering/expr_call.cpp

// 変更前
if (func_name == "println") {
    // cm_println_* を生成
    if (arg_type == "int") {
        return create_call("cm_println_int", args);
    }
}

// 変更後
if (func_name == "println") {
    // std::io::println を呼び出し
    return create_call("std::io::println", args);
}
```

#### 4.2 LLVM IR生成の変更

```cpp
// src/codegen/llvm/core/mir_to_llvm.cpp

// 変更前
if (name == "cm_print_string") {
    // 外部関数として宣言
}

// 変更後
if (name == "std::io::print") {
    // Cmモジュールの関数として処理
}
```

## 移行戦略

### 1. 後方互換性の維持

```cm
// 移行期間中の互換レイヤー
module std::compat;

// 旧APIを新APIにマップ
export void cm_print_string(cstring s) {
    std::io::print(string(s));
}

export void cm_println_string(cstring s) {
    std::io::println(string(s));
}
```

### 2. 段階的な切り替え

```cm
// コンパイラフラグで制御
#if CM_USE_NEW_STDLIB
    import std::io;
    io::println("Hello");
#else
    cm_println_string("Hello");
#endif
```

### 3. テストの並行実行

```bash
# 旧ランタイムでのテスト
make test_legacy

# 新標準ライブラリでのテスト
make test_stdlib
```

## パフォーマンス考慮事項

### 1. インライン化
```cm
// 小さな関数は積極的にインライン化
#[inline]
export void print(string s) {
    // 直接システムコール
}
```

### 2. バッファリング
```cm
// バッファ付き出力
struct BufferedWriter {
    byte[4096] buffer;
    int pos;
}
```

### 3. 最適化レベル
- デバッグビルド：関数呼び出しを維持
- リリースビルド：積極的なインライン化

## 利点

1. **ドッグフーディング**
   - Cm言語自体で標準ライブラリを実装
   - 言語の完成度向上

2. **自然なAPI**
   - `std::io::println` vs `cm_println_string`
   - より直感的な名前空間

3. **型安全性**
   - ジェネリックDisplayトレイト
   - コンパイル時の型チェック

4. **保守性**
   - Cmコードは読みやすい
   - デバッグが容易

5. **拡張性**
   - ユーザー定義型へのDisplay実装
   - カスタムフォーマッタ

## リスクと対策

### 1. ブートストラップ問題
**リスク**: 標準ライブラリをコンパイルするのに標準ライブラリが必要

**対策**:
- Stage 0: Cランタイムでコンパイラをビルド
- Stage 1: そのコンパイラで標準ライブラリをビルド
- Stage 2: 新標準ライブラリでコンパイラを再ビルド

### 2. パフォーマンス劣化
**リスク**: C実装より遅くなる可能性

**対策**:
- クリティカルパスはintrinsicsに
- LLVMの最適化に依存
- ベンチマークで継続的に測定

### 3. デバッグの複雑化
**リスク**: 標準ライブラリのバグがデバッグを困難に

**対策**:
- 十分なテストカバレッジ
- デバッグシンボルの維持
- ログ機能の充実

## まとめ

このアプローチにより：

1. **生成コードが自然に**: `cm_*`関数が消え、標準的なライブラリ呼び出しに
2. **言語の成熟度向上**: 自己ホスティングに一歩近づく
3. **開発者体験の向上**: Cmで書かれた読みやすい標準ライブラリ

実装は段階的に進め、各フェーズで動作確認とパフォーマンス測定を行いながら進めます。

---

**作成者:** Claude Code
**ステータス:** 設計提案
**次のステップ:** Phase 1のintrinsics層実装から開始