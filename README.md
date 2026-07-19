# Cm (シーマイナー) プログラミング言語

**ステータス**: 🚧 設計・開発中

## 概要

Cm（シーマイナー）は、[Cb言語](https://github.com/shadowlink0122/Cb)の設計レベルからのリニューアルプロジェクトです。

**OSなどの低レイヤーなソフトウェアから、ウェブフロントエンドまで**を単一の言語で記述できる、次世代のプログラミング言語を目指しています。

### 特徴

- ⚡ **LLVMバックエンド**: LLVM IRによる高速なネイティブバイナリ生成
- 🌐 **対応プラットフォーム**: macOS (ARM64) / Ubuntu (x86_64) / WASM / JavaScript / UEFI / SystemVerilog (FPGA)
- 🕸️ **WebAssembly対応**: `--target=wasm`で直接WASMバイナリ生成
- 🎸 **JavaScriptバックエンド**: `--target=js`でJSコード生成、Node.jsで実行可能
- 🔌 **SystemVerilogバックエンド**: `--target=sv`でFPGA向けRTL生成（IO構造体宣言・モジュール階層化・interface/implメソッド合成・実機I/O属性・ピン制約生成・`#[test]`サイクル精度テストベンチ）
- 🧪 **サニタイザ**: `--sanitize=address/thread/memory/bounds/undefined`による実行時メモリ検査（境界外・use-after-free・データ競合・ゼロ除算・null参照を検出）
- 🖥️ **ベアメタル/UEFI対応**: `--target=uefi`でOS不要のUEFIアプリケーション生成
- 🚀 **C++風構文**: 馴染みやすい構文、モダンな言語機能
- ☄️ **インラインユニオン型**: `int | null` のように型を直接結合、null許容型を簡潔に記述
- 🧯 **Rust準拠エラーハンドリング**: 組み込み `Result<T, E>` / `Option<T>`・`?` 演算子・`[must_use]` 静的チェック
- 🎼 **演算子オーバーロード**: `impl T { operator ... }` でカスタム演算子定義、複合代入(`+=`等)自動対応
- 📝 **Rustスタイルフォーマット**: `{}`プレースホルダーによる柔軟な文字列フォーマット
- 🧬 **ジェネリクス**: 型パラメータによる汎用プログラミング

> **Note**: コード生成はLLVM IR・JS CodeGen・SV CodeGenの3系統です。
> 以前検討されていたRust/TypeScript/C++へのトランスパイルは今後行いません。

## コード例

```cpp
// 関数定義（C++風：戻り値型が先）
int add(int a, int b) {
    return a + b;
}

// フォーマット文字列（変数自動キャプチャ）
import std::io::println;
int main() {
    int x = 10;
    int y = 20;
    println("x = {x}, y = {y}");  // 変数を自動的にキャプチャ
    return 0;
}

// ジェネリクス（型パラメータは戻り値型の前に置く）
<T> T identity(T value) {
    return value;
}

// 構造体
struct Point {
    int x;
    int y;
};

// インターフェースと実装
interface Printable {
    void print();
}

impl Point for Printable {
    void print() {
        println("({self.x}, {self.y})");
    }
}

// 演算子オーバーロード
impl Point {
    operator Point +(Point other) {
        return Point{x: self.x + other.x, y: self.y + other.y};
    }
}

int main() {
    Point a = Point{x: 1, y: 2};
    Point b = Point{x: 3, y: 4};
    Point c = a + b;    // Point{4, 6}
    c += Point{x: 1, y: 1};  // Point{5, 7}
    return 0;
}

// スレッド
import native::thread::{spawn, join};

void* compute(void* arg) {
    return 42 as void*;
}

int main() {
    ulong t = spawn(compute as void*);
    int result = join(t);  // 42
    return 0;
}

// パターンマッチ
int getValue(Option<int> opt) {
    match (opt) {
        Option::Some(v) => { return v; }
        Option::None => { return 0; }
    }
}

// インラインユニオン型とnull型
typedef MaybeInt = int | null;

int main() {
    // typedef経由でのユニオン型
    MaybeInt x = null;
    MaybeInt y = 42 as MaybeInt;

    // インラインユニオン型（typedef不要）
    int | null a = null;
    int | string | null b = null;
    return 0;
}
```

## パイプライン

```
Cm Source (.cm)
    │
    ▼
Lexer → Parser → AST → TypeCheck → HIR → MIR
                                    │
                 ┌──────────────────┼──────────────────┐
                 ▼                  ▼                  ▼
           ┌───────────┐     ┌────────────┐     ┌────────────┐
           │  LLVM IR  │     │ JS CodeGen │     │ SV CodeGen │
           └─────┬─────┘     └─────┬──────┘     └─────┬──────┘
                 │                 │                  │
     ┌───────┬───┼───┬─────────┐   │                  │
     ▼       ▼   ▼   ▼         ▼   ▼                  ▼
┌─────────┐ ┌───────┐ ┌──────┐ ┌──────┐ ┌──────────┐ ┌───────────┐
│ x86_64  │ │ ARM64 │ │ WASM │ │ UEFI │ │output.js │ │ output.sv │
└────┬────┘ └───┬───┘ └──┬───┘ └──┬───┘ └────┬─────┘ └─────┬─────┘
     ▼          ▼        ▼        ▼          ▼             ▼
Linux/macOS   macOS   Browser  Firmware   Node.js   FPGA / Verilator
```

## エコシステム: gen (弦) — 将来構想

> 💡 Cb/Cmが音楽（コード名）に由来することから、「弦」をモチーフにした
> パッケージマネージャ・バージョン管理ツールを将来的に計画しています。
> **現在は未実装です。**

## CLI使用方法

```bash
# 実行（LLVM JITまたはインタプリタ）
cm run example.cm          # プログラムを実行
cm run example.cm -d       # デバッグログ付き
cm run example.cm --verbose # 詳細表示

# コンパイル（LLVMバックエンド）
cm compile example.cm                  # ネイティブにコンパイル
cm compile example.cm -o myprogram     # 出力ファイル名指定
cm compile example.cm -O3              # 最適化レベル3
cm compile example.cm --emit-llvm      # LLVM IR出力
cm compile example.cm --target=wasm    # WebAssembly出力
cm compile example.cm --target=js      # JavaScript出力
cm compile example.cm --target=uefi    # UEFIアプリケーション出力

# SystemVerilog出力（FPGA向けRTL）
cm compile circuit.cm --target=sv                     # SVコード生成
cm compile circuit.cm --target=sv --emit-constraints  # ピン制約(.cst/.tcl)も生成

# テスト実行（#[test]関数。//! platform: sv はiverilogシミュレーション、それ以外はJIT実行）
cm test example.cm

# フォーマット
cm fmt example.cm          # 整形
cm fmt --check example.cm  # 整形検証のみ

# 構文チェック
cm check example.cm        # 型チェックのみ

# ヘルプ
cm help                    # ヘルプ表示
```

### フォーマット文字列

変数を自動キャプチャする文字列補間と、Rustスタイルのフォーマット指定子をサポートしています：

```cm
import std::io::println;

int main() {
    int n = 255;
    double pi = 3.14159;
    string s = "left";

    // 基本的な補間（変数を自動キャプチャ）
    println("Value: {n}");             // Value: 255

    // 基数変換
    println("Hex: {n:x}");             // Hex: ff
    println("Binary: {n:b}");          // Binary: 11111111

    // 浮動小数点の精度
    println("Pi: {pi:.2}");            // Pi: 3.14

    // アライメント
    println("|{s:<10}|");              // |left      |
    println("|{s:>10}|");              // |      left|
    println("|{s:^10}|");              // |   left   |

    // ゼロ埋め
    println("{n:0>5}");                // 00255

    return 0;
}
```

## 開発言語

- **C++20** (Clang 17+推奨, GCC 13+)

## サポート環境

| OS | アーキテクチャ | ステータス |
|----|-------------|----------|
| **macOS 14+** | ARM64 (Apple Silicon) | ✅ 完全サポート |
| **Ubuntu 22.04** | x86_64 | ✅ 完全サポート |
| **UEFI** | x86_64 | ✅ サポート（no_std） |
| Windows | - | ❌ 未サポート |

> **Note**: macOS Intel (x86_64) でも動作する可能性がありますが、CIでの検証はARM64のみです。
> UEFI対応はクロスコンパイルで実現しています（macOS/Linux上でQEMU + OVMFにて動作確認）。

## CI/CD テストマトリクス

GitHub Actionsで全バックエンドの自動テストを実行しています（macOS ARM64 / Ubuntu x86_64）。

### テスト構成（3層 + 全バックエンドスイート）

- **unitテスト**: 単一ビルドオブジェクトのC++単体検証（lexer・エラー型・MIR最適化パス等）
- **regressionテスト**: コンパイルパイプラインの段階を通すgtest回帰（HIR/MIR lowering・コード生成・フォーマッタ等）
- **integrationテスト（バックエンドスイート）**: cmバイナリによるCmプログラムの実行検証 — JIT / LLVM Native / LLVM WASM (Wasmtime) / JavaScript (Node.js) / SystemVerilog (iverilogシミュレーション)
- **cm test E2E**: `#[test]` 関数のJIT/SVシミュレーション自動ディスパッチ検証

### ローカルテスト実行

```bash
# すべてのテスト（unit・regression・全バックエンドスイート・E2E）
make test

# 層別
make test-unit         # C++単体テスト
make test-regression   # C++回帰テスト

# バックエンドスイート別
make test-interpreter  # JIT
make test-llvm         # LLVMネイティブ
make test-llvm-wasm    # LLVM WASM
make test-js           # JavaScript
make test-sv           # SystemVerilog（iverilogシミュレーション）

# E2Eスイート
make test-sanitize     # サニタイザ（--sanitize）
make test-i18n         # メッセージi18n（英日出力）
make test-lint         # linter機能テスト

# 個別カテゴリ
./tests/unified_test_runner.sh -b jit -c basic
./tests/unified_test_runner.sh -b llvm -c generics
./tests/unified_test_runner.sh -b sv -c basic
```

## ドキュメント

### 入門
- [クイックスタート](docs/QUICKSTART.md) - インストールと最初のプログラム
- [チュートリアル（日本語）](docs/tutorials/ja/index.md) / [Tutorials (English)](docs/tutorials/en/index.md) - 言語機能を段階的に学ぶ
- [実装済み機能一覧](docs/FEATURES.md)

### バックエンド別チュートリアル
- [ネイティブ/UEFI](docs/tutorials/ja/compiler/native/index.md) - LLVMネイティブコンパイルとUEFIアプリケーション
- [SystemVerilog](docs/tutorials/ja/compiler/sv/index.md) - FPGA向けRTL生成（実機I/O・モジュール階層・回路検証フレームワーク）
- [WASM](docs/tutorials/ja/compiler/wasm/index.md) / [JavaScript](docs/tutorials/ja/compiler/js/index.md)

### 開発者向け
- [開発環境ガイド](docs/DEVELOPMENT.md) - ビルド・テスト・lint
- [プロジェクト構造](docs/PROJECT_STRUCTURE.md)
- [言語仕様](docs/design/CANONICAL_SPEC.md) / [文法定義](docs/design/cm_grammar.md)
- [バックエンド対応状況](docs/design/backend_support_matrix.md) - 機能×バックエンドのサポート表
- [コンパイラ内部](docs/tutorials/ja/internals/index.md) - アーキテクチャ・最適化
- [リリースノート](docs/releases/index.md)

## 関連プロジェクト

- [Cb言語](https://github.com/shadowlink0122/Cb) - 本プロジェクトの前身

## ライセンス

検討中

---

© 2025-2026 Cm言語プロジェクト