[English](README.en.html)

# LLVMバックエンドドキュメント

Cm言語のLLVMバックエンド実装に関するドキュメントです。

## 📚 ドキュメント一覧

| ドキュメント | 説明 |
|------------|------|
| [llvm_backend_implementation.md](llvm_backend_implementation.html) | LLVM実装の詳細とアーキテクチャ |
| [LLVM_OPTIMIZATION.md](LLVM_OPTIMIZATION.html) | 最適化パイプラインとパス |
| [LLVM_RUNTIME_LIBRARY.md](LLVM_RUNTIME_LIBRARY.html) | ランタイムライブラリのドキュメント |
| [llvm_migration_plan.md](llvm_migration_plan.html) | LLVMへの移行計画 |
| [llvm_multiplatform.md](llvm_multiplatform.html) | マルチプラットフォーム対応 |
| [llvm_implementation_summary.md](llvm_implementation_summary.html) | 実装サマリー |

## 🏗️ アーキテクチャ概要

```
Cm Source → Lexer → Parser → AST → HIR → MIR → LLVM IR → Binary
                                              ↑
                                        LLVMCodegen
                                              ↓
                                    Native / WASM
```

## 🚀 主要機能

### 実装済み

- ✅ プリミティブ型のコード生成
- ✅ 構造体のコード生成
- ✅ 関数定義・呼び出し
- ✅ 制御構文（if/while/for/switch）
- ✅ ジェネリクス（モノモーフィゼーション）
- ✅ 配列とポインタ
- ✅ operator実装
- ✅ ランタイムライブラリ（println等）
- ✅ 最適化パス（DCE, constant folding等）
- ✅ WASMターゲット

### 未実装

- ⚠️ 完全なインターフェース動的ディスパッチ
- ⚠️ 複雑なライフタイム管理
- ⚠️ モジュールシステム

## 📖 重要なトピック

### 型システム

LLVM IRへの型マッピング:

| Cm型 | LLVM型 |
|------|--------|
| `int` | `i32` |
| `long` | `i64` |
| `float` | `float` |
| `double` | `double` |
| `bool` | `i8` (0/1) |
| `string` | `i8*` |
| 構造体 | `%StructName` |
| ポインタ | `T*` |
| 配列 | `[N x T]` |

### ランタイムライブラリ

主要な関数:

- `cm_println` - 文字列出力
- `cm_print_int` - 整数出力
- `cm_print_double` - 浮動小数点数出力
- `cm_malloc` / `cm_free` - メモリ管理
- `cm_format_*` - フォーマット関数

詳細は [LLVM_RUNTIME_LIBRARY.md](LLVM_RUNTIME_LIBRARY.html) を参照。

### 最適化

LLVMの最適化パス:

1. **関数レベル最適化**
   - Dead Code Elimination (DCE)
   - Constant Folding
   - Instruction Combining

2. **モジュールレベル最適化**
   - Inlining
   - Global Dead Code Elimination
   - Tail Call Optimization

詳細は [LLVM_OPTIMIZATION.md](LLVM_OPTIMIZATION.html) を参照。

## 🔧 開発ガイド

### ビルド方法

```bash
# LLVM有効でビルド
cmake -B build -DCM_USE_LLVM=ON
cmake --build build

# テスト実行
make test-llvm-all
```

### デバッグ

```bash
# LLVM IRを出力
./build/bin/cm compile --emit-llvm test.cm

# アセンブリを出力
./build/bin/cm compile --emit-asm test.cm

# 最適化なし
./build/bin/cm compile --no-optimize test.cm
```

### 新機能追加の手順

1. `src/codegen/llvm_codegen.cpp` にコード生成を追加
2. `runtime/cm_runtime.c` にランタイム関数を追加（必要なら）
3. `tests/test_programs/` にテストを追加
4. `make test-llvm-all` で検証

## 📊 パフォーマンス

### ベンチマーク結果

```
fibonacci(40):
  インタプリタ: 12.5秒
  LLVM -O0:     2.1秒
  LLVM -O3:     0.8秒
  C++ -O3:      0.7秒
```

### コンパイル速度

```
小規模プロジェクト (< 1000 LOC):    ~0.2秒
中規模プロジェクト (< 10000 LOC):   ~1.5秒
大規模プロジェクト (< 100000 LOC):  ~15秒
```

## 🐛 既知の問題

- typedef型ポインタのコンパイルが不完全
- with自動実装のWASMサポートなし
- 複雑なジェネリック制約の処理

詳細は [../implementation/known_limitations.md](../implementation/known_limitations.html) を参照。

## 🔗 関連リンク

- [LLVM公式ドキュメント](https://llvm.org/docs/)
- [LLVM Language Reference](https://llvm.org/docs/LangRef.html)
- [Kaleidoscope Tutorial](https://llvm.org/docs/tutorial/)

---

**更新日:** 2025-12-15