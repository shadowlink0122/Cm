[English](README.en.html)

# Cm 設計ドキュメント

このディレクトリには、Cm言語処理系の設計に関するドキュメントが含まれています。

## ドキュメント一覧

### 全体設計

| ドキュメント | 説明 |
|-------------|------|
| [architecture.md](architecture.html) | コンパイラパイプライン全体設計 |

### 中間表現 (IR)

| ドキュメント | 説明 |
|-------------|------|
| [hir.md](hir.html) | HIR: 高レベル中間表現 |
| [mir.md](mir.html) | MIR: 中レベル中間表現（SSA、CFG） |
| [lir.md](lir.html) | LIR: 低レベル中間表現 |

### コード生成

| ドキュメント | 説明 |
|-------------|------|
| [codegen/README.md](codegen/README.html) | Codegenアーキテクチャ |
| [codegen/rust.md](codegen/rust.html) | Rust変換規則 |
| [codegen/typescript.md](codegen/typescript.html) | TypeScript変換、React連携 |
| [codegen/wasm.md](codegen/wasm.html) | WASM生成 |
| [interpreter.md](interpreter.html) | インタプリタ/JIT設計 |

### プラットフォーム

| ドキュメント | 説明 |
|-------------|------|
| [ffi.md](ffi.html) | C/Rust FFI |
| [baremetal.md](baremetal.html) | Bare-metal/OS開発 |
| [wasm_runtime.md](wasm_runtime.html) | WASM環境、React/Vue連携 |

### ツール・品質

| ドキュメント | 説明 |
|-------------|------|
| [backends.md](backends.html) | マルチバックエンド設計 |
| [package_manager.md](package_manager.html) | gen パッケージマネージャ |
| [debug.md](debug.html) | デバッグモード |
| [testing.md](testing.html) | テスト戦略 |
| [technical_challenges.md](technical_challenges.html) | 技術的課題と解決策 |
| [async.md](async.html) | 非同期処理 (Future型) |
| [platform.md](platform.html) | プラットフォーム・ターゲット設計 |
| [derive.md](derive.html) | 自動実装 (Debug, Clone, Default等) |
| [editor.md](editor.html) | エディタ拡張、シンタックスハイライト |

## 設計原則

1. **クロスバックエンド一致**: インタプリタとコンパイラで同一結果
2. **マルチプラットフォーム**: Windows/macOS/Linux対応
3. **拡張性**: 新バックエンドの追加容易
4. **良いエラーメッセージ**: ソース位置情報の保持