# Cm 設計ドキュメント

このディレクトリには、Cm言語処理系の設計に関するドキュメントが含まれています。

## ドキュメント一覧

### 全体設計

| ドキュメント | 説明 |
|-------------|------|
| [architecture.md](architecture.md) | コンパイラパイプライン全体設計 |

### 中間表現 (IR)

| ドキュメント | 説明 |
|-------------|------|
| [hir.md](hir.md) | HIR: 高レベル中間表現 |
| [mir.md](mir.md) | MIR: 中レベル中間表現（SSA、CFG） |
| [lir.md](lir.md) | LIR: 低レベル中間表現 |

### コード生成

| ドキュメント | 説明 |
|-------------|------|
| [codegen/README.md](codegen/README.md) | Codegenアーキテクチャ |
| [codegen/rust.md](codegen/rust.md) | Rust変換規則 |
| [codegen/typescript.md](codegen/typescript.md) | TypeScript変換、React連携 |
| [codegen/wasm.md](codegen/wasm.md) | WASM生成 |
| [interpreter.md](interpreter.md) | インタプリタ/JIT設計 |

### プラットフォーム

| ドキュメント | 説明 |
|-------------|------|
| [ffi.md](ffi.md) | C/Rust FFI |
| [baremetal.md](baremetal.md) | Bare-metal/OS開発 |
| [wasm_runtime.md](wasm_runtime.md) | WASM環境、React/Vue連携 |

### ツール・品質

| ドキュメント | 説明 |
|-------------|------|
| [backends.md](backends.md) | マルチバックエンド設計 |
| [package_manager.md](package_manager.md) | gen パッケージマネージャ |
| [debug.md](debug.md) | デバッグモード |
| [testing.md](testing.md) | テスト戦略 |
| [technical_challenges.md](technical_challenges.md) | 技術的課題と解決策 |
| [async.md](async.md) | 非同期処理 (Future型) |
| [platform.md](platform.md) | プラットフォーム・ターゲット設計 |

## 設計原則

1. **クロスバックエンド一致**: インタプリタとコンパイラで同一結果
2. **マルチプラットフォーム**: Windows/macOS/Linux対応
3. **拡張性**: 新バックエンドの追加容易
4. **良いエラーメッセージ**: ソース位置情報の保持
