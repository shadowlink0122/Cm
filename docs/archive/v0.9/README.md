# v0.9 設計文書アーカイブ（索引）

v0.9の設計文書をサブシステム別カテゴリに整理したもの（歴史的アーカイブ）。

## backends/

- [MIR_INTERPRETER_SUMMARY.md](backends/MIR_INTERPRETER_SUMMARY.md) — MIRインタープリタと統合テストフレームワーク - 実装完了
- [backend_comparison.md](backends/backend_comparison.md) — バックエンド比較：3言語 vs LLVM
- [interpreter.md](backends/interpreter.md) — インタプリタ設計
- [wasm_execution.md](backends/wasm_execution.md) — WebAssemblyファイルの実行方法

## misc/

- [debug.md](misc/debug.md) — デバッグモード設計
- [ffi.md](misc/ffi.md) — FFI (Foreign Function Interface) 設計
- [infinite_loop_detection_system.md](misc/infinite_loop_detection_system.md) — 無限ループ検出システム設計書

## modules/

- [MODULE_SYSTEM.md](modules/MODULE_SYSTEM.md) — Cmモジュールシステム設計
- [module_compilation_strategy.md](modules/module_compilation_strategy.md) — Cm言語 モジュール分割コンパイル戦略
- [module_system_spec.md](modules/module_system_spec.md) — Cm言語 モジュールシステム仕様書
- [module_system_v2.md](modules/module_system_v2.md) — Cm言語 モジュールシステム v2 設計仕様
- [module_system_v3.md](modules/module_system_v3.md) — Cm言語 モジュールシステム v3 設計仕様
- [module_system_v4.md](modules/module_system_v4.md) — Cm言語 モジュールシステム v4 設計仕様

## strings/

- [STRING_INTERPOLATION_ARCHITECTURE.md](strings/STRING_INTERPOLATION_ARCHITECTURE.md) — String Interpolation Architecture Diagram
- [STRING_INTERPOLATION_EXAMPLES.md](strings/STRING_INTERPOLATION_EXAMPLES.md) — String Interpolation Examples and Code References
- [STRING_INTERPOLATION_LLVM.md](strings/STRING_INTERPOLATION_LLVM.md) — String Interpolation and Format Strings in LLVM Backend
- [STRING_INTERPOLATION_README.md](strings/STRING_INTERPOLATION_README.md) — String Interpolation and Format Strings - Complete Documentation
- [format_string_redesign.md](strings/format_string_redesign.md) — フォーマット文字列処理の再設計
- [format_strings.md](strings/format_strings.md) — Format Strings Design
- [string_interpolation.md](strings/string_interpolation.md) — 文字列埋め込み（String Interpolation）の正しい実装
- [string_interpolation_implementation.md](strings/string_interpolation_implementation.md) — 文字列埋め込み実装計画

## structs/

- [STRUCT_AND_ARRAY_DESIGN.md](structs/STRUCT_AND_ARRAY_DESIGN.md) — 構造体と配列の拡張設計

## testing/

- [test_framework.md](testing/test_framework.md) — Cm言語 統合テストフレームワーク
- [test_strategy.md](testing/test_strategy.md) — テスト戦略
- [unified_test_structure.md](testing/unified_test_structure.md) — Cm言語 統一テスト構造
