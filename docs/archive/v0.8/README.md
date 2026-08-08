# v0.8 設計文書アーカイブ（索引）

v0.8の設計文書をサブシステム別カテゴリに整理したもの（歴史的アーカイブ）。

## backends-codegen/

- [backends.md](backends-codegen/backends.md) — バックエンド設計
- [cpp_mir_example.md](backends-codegen/cpp_mir_example.md) — CPP-MIR実装例
- [efficient_cpp_codegen.md](backends-codegen/efficient_cpp_codegen.md) — 効率的なC++コード生成の設計
- [llvm_architecture.md](backends-codegen/llvm_architecture.md) — LLVM アーキテクチャ設計
- [optimized_cpp_codegen.md](backends-codegen/optimized_cpp_codegen.md) — 最適化されたC++コード生成設計
- [transcompiler.md](backends-codegen/transcompiler.md) — Cm言語 トランスコンパイラ設計

## codegen/

- [README.md](codegen/README.md) — Codegen アーキテクチャ
- [rust.md](codegen/rust.md) — Rust Codegen 設計
- [typescript.md](codegen/typescript.md) — TypeScript Codegen 設計
- [wasm.md](codegen/wasm.md) — WASM Codegen 設計

## overloading/

- [function_overloading_v2.md](overloading/function_overloading_v2.md) — Cm言語 関数オーバーロード設計 v2
- [overload.md](overloading/overload.md) — Cm言語 オーバーロードシステム設計
- [overload_meaning.md](overloading/overload_meaning.md) — overload 修飾子の意味と使い方
- [overload_system.md](overloading/overload_system.md) — Cm言語 オーバーロードシステム設計
- [overload_unified.md](overloading/overload_unified.md) — Cm言語 統一オーバーロードシステム設計
- [transpiler_overload_strategy.md](overloading/transpiler_overload_strategy.md) — トランスパイラ オーバーロード実装戦略

## roadmap/

- [FEATURES.md](roadmap/FEATURES.md) — Cm言語 実装済み機能一覧
- [IMPLEMENTATION_ROADMAP.md](roadmap/IMPLEMENTATION_ROADMAP.md) — Cm言語 実装ロードマップ
- [P0_DEVELOPMENT_PRIORITY.md](roadmap/P0_DEVELOPMENT_PRIORITY.md) — Cm言語 開発優先順位とテスト計画
- [P1_FEATURE_PRIORITY.md](roadmap/P1_FEATURE_PRIORITY.md) — Cm言語 機能実装優先順位

## structs/

- [STRUCT_IMPLEMENTATION_STATUS.md](structs/STRUCT_IMPLEMENTATION_STATUS.md) — Cm言語 構造体実装調査報告書
- [STRUCT_INVESTIGATION_INDEX.md](structs/STRUCT_INVESTIGATION_INDEX.md) — 構造体実装調査 - ドキュメントインデックス
- [STRUCT_QUICK_REFERENCE.md](structs/STRUCT_QUICK_REFERENCE.md) — 構造体実装 クイックリファレンス
- [impl_blocks.md](structs/impl_blocks.md) — Cm言語 impl ブロック設計

## type-system/

- [derive.md](type-system/derive.md) — 自動実装 (with) 設計
- [switch_pattern.md](type-system/switch_pattern.md) — Switch文パターンマッチング仕様
- [type_system.md](type-system/type_system.md) — Cm言語 型システム設計
