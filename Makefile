# Cm Language Makefile
# 便利なコマンドをmakeで実行できるようにします

# 設定
CM := ./cm
BUILD_DIR := build

# デフォルトターゲット
.PHONY: help
help:
	@echo "Cm Language - Make Commands"
	@echo ""
	@echo "Build Commands:"
	@echo "  make build          - ビルド（デバッグモード）"
	@echo "  make release        - リリースビルド"
	@echo "  make clean          - ビルドディレクトリをクリーン"
	@echo "  make rebuild        - クリーン後に再ビルド"
	@echo ""
	@echo "Test Commands (Unit Tests):"
	@echo "  make test           - すべてのC++ユニットテストを実行"
	@echo "  make test-lexer     - Lexerテストのみ"
	@echo "  make test-hir       - HIR Loweringテストのみ"
	@echo "  make test-mir       - MIR Loweringテストのみ"
	@echo "  make test-opt       - 最適化テストのみ"
	@echo ""
	@echo "Test Commands (Integration/Backend Tests):"
	@echo "  make test-interpreter - インタプリタテスト"
	@echo "  make test-cpp         - C++コード生成テスト"
	@echo "  make test-rust        - Rustコード生成テスト"
	@echo "  make test-ts          - TypeScriptコード生成テスト"
	@echo "  make test-all         - すべてのテストを実行"
	@echo ""
	@echo "Run Commands:"
	@echo "  make run FILE=<file>       - Cmファイルを実行"
	@echo "  make run-debug FILE=<file> - デバッグモードで実行"
	@echo ""
	@echo "Development Commands:"
	@echo "  make format         - コードフォーマット"
	@echo "  make format-check   - フォーマットチェック"
	@echo ""
	@echo "Quick Shortcuts:"
	@echo "  make b  - build"
	@echo "  make t  - test"
	@echo "  make ta - test-all"

# ========================================
# Build Commands
# ========================================

.PHONY: build
build:
	@echo "Building Cm compiler (debug mode)..."
	@cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug
	@cmake --build $(BUILD_DIR)
	@echo "✅ Build complete!"

.PHONY: release
release:
	@echo "Building Cm compiler (release mode)..."
	@cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	@cmake --build $(BUILD_DIR)
	@echo "✅ Release build complete!"

.PHONY: clean
clean:
	@echo "Cleaning build directory..."
	@rm -rf $(BUILD_DIR)
	@echo "✅ Clean complete!"

.PHONY: rebuild
rebuild: clean build

# ========================================
# Unit Test Commands (C++ tests via ctest)
# ========================================

.PHONY: test
test:
	@echo "Running all C++ unit tests..."
	@ctest --test-dir $(BUILD_DIR) --output-on-failure
	@echo ""
	@echo "✅ All unit tests passed!"

.PHONY: test-lexer
test-lexer:
	@echo "Running Lexer tests..."
	@ctest --test-dir $(BUILD_DIR) -R "LexerTest" --output-on-failure

.PHONY: test-hir
test-hir:
	@echo "Running HIR Lowering tests..."
	@ctest --test-dir $(BUILD_DIR) -R "HirLoweringTest" --output-on-failure

.PHONY: test-mir
test-mir:
	@echo "Running MIR Lowering tests..."
	@ctest --test-dir $(BUILD_DIR) -R "MirLoweringTest" --output-on-failure

.PHONY: test-opt
test-opt:
	@echo "Running Optimization tests..."
	@ctest --test-dir $(BUILD_DIR) -R "MirOptimizationTest" --output-on-failure

# ========================================
# Integration/Backend Test Commands
# ========================================

.PHONY: test-interpreter
test-interpreter:
	@echo "Running interpreter tests..."
	@chmod +x tests/unified_test_runner.sh
	@tests/unified_test_runner.sh -b interpreter

.PHONY: test-cpp
test-cpp:
	@echo "Running C++ code generation tests..."
	@chmod +x tests/unified_test_runner.sh
	@tests/unified_test_runner.sh -b cpp

.PHONY: test-rust
test-rust:
	@echo "Running Rust code generation tests..."
	@chmod +x tests/unified_test_runner.sh
	@tests/unified_test_runner.sh -b rust

.PHONY: test-ts
test-ts:
	@echo "Running TypeScript code generation tests..."
	@chmod +x tests/unified_test_runner.sh
	@tests/unified_test_runner.sh -b typescript

# すべてのテストを実行
.PHONY: test-all
test-all: test test-interpreter test-cpp test-rust test-ts
	@echo ""
	@echo "=========================================="
	@echo "✅ All tests completed!"
	@echo "=========================================="

# ========================================
# Run Commands
# ========================================

.PHONY: run
run:
	@if [ -z "$(FILE)" ]; then \
		echo "Usage: make run FILE=<file.cm>"; \
		exit 1; \
	fi
	@$(CM) run $(FILE)

.PHONY: run-debug
run-debug:
	@if [ -z "$(FILE)" ]; then \
		echo "Usage: make run-debug FILE=<file.cm>"; \
		exit 1; \
	fi
	@$(CM) run $(FILE) --debug

# ========================================
# Development Commands
# ========================================

.PHONY: format
format:
	@echo "Formatting C++ code..."
	@find src tests -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" \) \
		-exec clang-format -i -style=file {} \;
	@echo "✅ Format complete!"

.PHONY: format-check
format-check:
	@echo "Checking code formatting..."
	@find src tests -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" \) \
		-exec clang-format -style=file -dry-run -Werror {} \; 2>&1 && \
		echo "✅ Format check passed!" || \
		(echo "❌ Format check failed! Run 'make format' to fix." && exit 1)

# ========================================
# Quick Development Shortcuts
# ========================================

.PHONY: b
b: build

.PHONY: t
t: test

.PHONY: ta
ta: test-all

.PHONY: c
c: clean

# デフォルトファイル設定
FILE ?=
