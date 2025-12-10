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
	@echo "  make all            - ビルド（テスト含む）"
	@echo "  make build          - cmコンパイラのみビルド"
	@echo "  make build-all      - テストを含むビルド"
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
	@echo "Test Commands (LLVM Backend):"
	@echo "  make test-llvm        - LLVM ネイティブテスト"
	@echo "  make test-llvm-wasm   - LLVM WebAssemblyテスト"
	@echo "  make test-llvm-all    - すべてのLLVMテスト"
	@echo ""
	@echo "  make test-all         - すべてのテストを実行"
	@echo ""
	@echo "Run Commands:"
	@echo "  make run FILE=<file>       - Cmファイルを実行"
	@echo "  make run-debug FILE=<file> - デバッグモードで実行"
	@echo ""
	@echo "Quick Shortcuts:"
	@echo "  make b   - build"
	@echo "  make t   - test"
	@echo "  make ta  - test-all"
	@echo "  make tl  - test-llvm"
	@echo "  make tlw - test-llvm-wasm"
	@echo "  make tla - test-llvm-all"

# ========================================
# Build Commands
# ========================================

.PHONY: all
all: build-all

.PHONY: build
build:
	@echo "Building Cm compiler (debug mode)..."
	@cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug -DCM_USE_LLVM=ON
	@cmake --build $(BUILD_DIR)
	@echo "✅ Build complete!"

.PHONY: build-all
build-all:
	@echo "Building Cm compiler with tests (debug mode)..."
	@cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug -DCM_USE_LLVM=ON -DBUILD_TESTING=ON
	@cmake --build $(BUILD_DIR)
	@echo "✅ Build complete (with tests)!"

.PHONY: release
release:
	@echo "Building Cm compiler (release mode)..."
	@cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DCM_USE_LLVM=ON
	@cmake --build $(BUILD_DIR)
	@echo "✅ Release build complete!"

.PHONY: clean
clean:
	@echo "Cleaning build directory..."
	@rm -rf $(CM) $(BUILD_DIR) .tmp/*
	@echo "✅ Clean complete!"

.PHONY: rebuild
rebuild: clean build-all

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
# Integration Test Commands
# ========================================

.PHONY: test-interpreter
test-interpreter:
	@echo "Running interpreter tests..."
	@chmod +x tests/unified_test_runner.sh
	@tests/unified_test_runner.sh -b interpreter

# ========================================
# LLVM Backend Test Commands
# ========================================

# LLVM ネイティブテスト
.PHONY: test-llvm
test-llvm:
	@echo "Running LLVM native code generation tests..."
	@chmod +x tests/unified_test_runner.sh
	@tests/unified_test_runner.sh -b llvm

# LLVM WebAssemblyテスト
.PHONY: test-llvm-wasm
test-llvm-wasm:
	@echo "Running LLVM WebAssembly code generation tests..."
	@chmod +x tests/unified_test_runner.sh
	@tests/unified_test_runner.sh -b llvm-wasm

# すべてのLLVMテストを実行
.PHONY: test-llvm-all
test-llvm-all: test-llvm test-llvm-wasm
	@echo ""
	@echo "=========================================="
	@echo "✅ All LLVM tests completed!"
	@echo "=========================================="

# すべてのテストを実行
.PHONY: test-all
test-all: test test-interpreter test-llvm-all
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

.PHONY: tl
tl: test-llvm

.PHONY: tlw
tlw: test-llvm-wasm

.PHONY: tla
tla: test-llvm-all

# デフォルトファイル設定
FILE ?=
