#!/bin/bash

# LLVMバックエンド関連を移動
mv -v llvm_backend_implementation.md llvm/ 2>/dev/null
mv -v LLVM_OPTIMIZATION.md llvm/ 2>/dev/null
mv -v LLVM_RUNTIME_LIBRARY.md llvm/ 2>/dev/null
mv -v llvm_implementation_summary.md llvm/ 2>/dev/null
mv -v llvm_migration_plan.md llvm/ 2>/dev/null
mv -v llvm_multiplatform.md llvm/ 2>/dev/null

# 実装進捗を移動
mv -v implementation_status.md implementation/ 2>/dev/null
mv -v implementation_progress_*.md implementation/ 2>/dev/null
mv -v known_limitations.md implementation/ 2>/dev/null

# アーカイブファイルを移動
mv -v CLEANUP_SUMMARY.md archive/ 2>/dev/null
mv -v STRUCT_*.{md,txt} archive/ 2>/dev/null
mv -v STRING_INTERPOLATION_*.md archive/ 2>/dev/null
mv -v backend_comparison.md archive/ 2>/dev/null
mv -v wasm_execution.md archive/ 2>/dev/null

echo "File organization complete"
