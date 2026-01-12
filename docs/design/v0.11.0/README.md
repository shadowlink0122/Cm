# v0.11.0 設計ドキュメント

## 概要
v0.11.0では所有権・借用システム完成、ベアメタル/UEFI対応、イテレータ拡張、JS改善を実施。

## ドキュメント一覧

### 新規追加（計画中）
| 番号 | ファイル | 内容 |
|------|---------|------|
| 001 | [001_improvement_overview.md](001_improvement_overview.md) | 改善項目総覧 |
| 002 | [002_baremetal_uefi.md](002_baremetal_uefi.md) | ベアメタル/UEFI対応 |
| 003 | [003_iterator_extensions.md](003_iterator_extensions.md) | イテレータ拡張 |
| 004 | [004_js_improvements.md](004_js_improvements.md) | JS生成改善 |
| 005 | [005_ownership_completion.md](005_ownership_completion.md) | 所有権システム完成 |

### 統合（未実装機能）
| 番号 | ファイル | 内容 |
|------|---------|------|
| 006 | [006_generic_inference.md](006_generic_inference.md) | ジェネリック型推論改善 |
| 007 | [007_js_web_interop.md](007_js_web_interop.md) | JS/Web相互運用 |
| 008 | [008_memory_safety.md](008_memory_safety.md) | メモリ安全性強化 |
| 009 | [009_module_interop.md](009_module_interop.md) | モジュール相互運用 |
| 013 | [013_robust_type_system.md](013_robust_type_system.md) | 堅牢な型システム |
| 014 | [014_wasm_runtime.md](014_wasm_runtime.md) | WASMランタイム |
| 015 | [015_borrowed_self_fix.md](015_borrowed_self_fix.md) | BorrowedSelf修正 |
| 016 | [016_js_ffi_spec.md](016_js_ffi_spec.md) | JS FFI仕様 |
| 017 | [017_variable_aliasing_optimization.md](017_variable_aliasing_optimization.md) | 変数エイリアシング最適化 |

### アーカイブ移動（実装済み）
- `010_move_semantics_patterns.md` → archive/
- `011_ownership_and_borrowing.md` → archive/
- `012_refactoring_proposal.md` → archive/

## 参照
- [ROADMAP.md](/ROADMAP.md)
- [VERSION_PLAN.md](/docs/VERSION_PLAN.md)
