# バックエンド設計

## 概要

Cm言語はLLVMバックエンドを使用してネイティブコードを生成します。

> **2024年12月より、LLVMバックエンドが唯一のコード生成方式です。**
> 以前検討されていたRust/TypeScript/C++へのトランスパイルは廃止されました。

## アーキテクチャ

```
┌─────────────────────────────────────────────────────────────┐
│                   Cm Compiler Backend                       │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   MIR (Mid-level IR)                                        │
│         │                                                   │
│         ▼                                                   │
│   ┌─────────────┐                                           │
│   │  LLVM IR    │  唯一のコード生成バックエンド              │
│   │  Generator  │                                           │
│   └─────┬───────┘                                           │
│         │                                                   │
│         ├─────────────────────┬─────────────────────┐       │
│         ▼                     ▼                     ▼       │
│     ┌───────┐            ┌─────────┐           ┌────────┐   │
│     │x86_64 │            │  ARM64  │           │  WASM  │   │
│     └───┬───┘            └────┬────┘           └───┬────┘   │
│         │                     │                    │        │
│         ▼                     ▼                    ▼        │
│   Linux/Windows/macOS    macOS/Linux/組み込み   Browser     │
│                                                             │
│                     ┌─────────────┐                         │
│                     │ Interpreter │ ← 開発・デバッグ用      │
│                     └─────────────┘                         │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

## 1. LLVMバックエンド（メインバックエンド）

### 概要

MIRをLLVM IRに変換し、LLVMの最適化パイプラインを通じてネイティブコードを生成します。

### サポートされるターゲット

| ターゲット | Triple | 用途 |
|-----------|--------|------|
| x86_64 Linux | x86_64-unknown-linux-gnu | デスクトップ、サーバー |
| x86_64 macOS | x86_64-apple-darwin | Intel Mac |
| ARM64 macOS | aarch64-apple-darwin | Apple Silicon |
| ARM64 Linux | aarch64-unknown-linux-gnu | ARM サーバー、Raspberry Pi |
| WebAssembly | wasm32-unknown-unknown | ブラウザ |

### 型マッピング

| Cm | LLVM IR |
|----|---------|
| `int` | `i64` |
| `uint` | `i64` (unsigned扱い) |
| `short` | `i16` |
| `tiny` | `i8` |
| `float` | `float` |
| `double` | `double` |
| `bool` | `i1` |
| `char` | `i32` |
| `string` | `i8*` |

### 使用方法

```bash
# ネイティブ実行ファイル生成
cm compile program.cm -o program

# 最適化レベル指定
cm compile program.cm -O3 -o program

# LLVM IR出力
cm compile program.cm --emit=llvm-ir -o program.ll

# WebAssembly出力
cm compile program.cm --target=wasm32 -o program.wasm
```

### 詳細ドキュメント

- [LLVM バックエンド実装ガイド](../llvm_backend_implementation.md)
- [LLVM ランタイムライブラリ](../LLVM_RUNTIME_LIBRARY.md)
- [LLVM 最適化パイプライン](../LLVM_OPTIMIZATION.md)

---

## 2. インタープリター

### 概要

MIRを直接解釈実行し、高速な開発サイクルを実現します。

### 用途

- **REPL**: 対話的な実行環境
- **テスト**: コンパイルなしの高速テスト
- **開発**: 即時実行によるデバッグ

### 使用方法

```bash
# インタプリタで実行
cm run program.cm
```

---

## 廃止されたバックエンド

以下のバックエンドは2024年12月に廃止されました。
ソースコードは参考のため `src/backends/` に保持されていますが、今後のメンテナンスや機能追加は行われません。

### Rustトランスパイラ（廃止）

- **理由**: LLVMバックエンドでネイティブコード生成が可能になったため
- **ソースコード**: `src/backends/rust/` （参考のため保持）

### TypeScriptトランスパイラ（廃止）

- **理由**: LLVM経由でのWebAssembly出力に置き換え
- **ソースコード**: `src/backends/typescript/` （参考のため保持）

### なぜLLVMに統一したか

| 項目 | 複数バックエンド | LLVM統一 |
|-----|----------------|---------|
| 保守コスト | 高（3箇所修正） | 低（1箇所修正） |
| 動作一貫性 | 不一致リスク | 完全一致 |
| 最適化 | 言語依存 | LLVM統一最適化 |
| ターゲット | 限定的 | 100+ターゲット |
| デバッグ | バラバラ | DWARF統一 |

詳細な比較は [backend_comparison.md](../backend_comparison.md) を参照してください。
