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
	@echo "Test Commands (Optimization Levels):"
	@echo "  make test-opt-levels       - 全最適化レベル(O0-O3)テスト"
	@echo "  make test-opt-quick        - 各カテゴリ1ファイルで最適化テスト（高速）"
	@echo "  make test-opt-llvm         - LLVMネイティブで全最適化レベルテスト"
	@echo "  make test-opt-wasm         - WebAssemblyで全最適化レベルテスト"
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
	@echo "  make tj  - test-js"
	@echo "  make tjp - test-js-parallel"
	@echo "  make tol - test-opt-levels"
	@echo "  make toq - test-opt-quick"

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

# 並列インタプリタテスト
.PHONY: test-interpreter-parallel
test-interpreter-parallel:
	@echo "Running interpreter tests (parallel)..."
	@chmod +x tests/unified_test_runner.sh
	@tests/unified_test_runner.sh -b interpreter -p

# ========================================
# LLVM Backend Test Commands
# ========================================

# LLVM ネイティブテスト
.PHONY: test-llvm
test-llvm:
	@echo "Running LLVM native code generation tests..."
	@chmod +x tests/unified_test_runner.sh
	@tests/unified_test_runner.sh -b llvm

# LLVM ネイティブテスト（並列）
.PHONY: test-llvm-parallel
test-llvm-parallel:
	@echo "Running LLVM native code generation tests (parallel)..."
	@chmod +x tests/unified_test_runner.sh
	@tests/unified_test_runner.sh -b llvm -p

# LLVM WebAssemblyテスト
.PHONY: test-llvm-wasm
test-llvm-wasm:
	@echo "Running LLVM WebAssembly code generation tests..."
	@chmod +x tests/unified_test_runner.sh
	@tests/unified_test_runner.sh -b llvm-wasm

# LLVM WebAssemblyテスト（並列）
.PHONY: test-llvm-wasm-parallel
test-llvm-wasm-parallel:
	@echo "Running LLVM WebAssembly code generation tests (parallel)..."
	@chmod +x tests/unified_test_runner.sh
	@tests/unified_test_runner.sh -b llvm-wasm -p

# ========================================
# JavaScript Backend Test Commands
# ========================================

# JavaScript テスト
.PHONY: test-js
test-js:
	@echo "Running JavaScript code generation tests..."
	@chmod +x tests/unified_test_runner.sh
	@tests/unified_test_runner.sh -b js

# JavaScript テスト（並列）
.PHONY: test-js-parallel
test-js-parallel:
	@echo "Running JavaScript code generation tests (parallel)..."
	@chmod +x tests/unified_test_runner.sh
	@tests/unified_test_runner.sh -b js -p

# すべてのLLVMテストを実行
.PHONY: test-llvm-all
test-llvm-all: test-llvm test-llvm-wasm
	@echo ""
	@echo "=========================================="
	@echo "✅ All LLVM tests completed!"
	@echo "=========================================="

# ========================================
# Optimization Level Test Commands
# ========================================

# 全最適化レベルテスト（全テストファイル）
.PHONY: test-opt-levels
test-opt-levels:
	@echo "Running optimization level tests (O0-O3) for all test files..."
	@chmod +x tests/optimization_test_runner.sh
	@tests/optimization_test_runner.sh -b llvm

# 全最適化レベルテスト（クイックモード - 各カテゴリ1ファイルのみ）
.PHONY: test-opt-quick
test-opt-quick:
	@echo "Running quick optimization level tests (O0-O3)..."
	@chmod +x tests/optimization_test_runner.sh
	@tests/optimization_test_runner.sh -b llvm -q

# LLVMネイティブで全最適化レベルテスト
.PHONY: test-opt-llvm
test-opt-llvm:
	@echo "Running LLVM native optimization level tests (O0-O3)..."
	@chmod +x tests/optimization_test_runner.sh
	@tests/optimization_test_runner.sh -b llvm -c "basic,control_flow,functions"

# WebAssemblyで全最適化レベルテスト
.PHONY: test-opt-wasm
test-opt-wasm:
	@echo "Running WebAssembly optimization level tests (O0-O3)..."
	@chmod +x tests/optimization_test_runner.sh
	@tests/optimization_test_runner.sh -b llvm-wasm -c "basic,control_flow,functions"

# すべてのテストを実行（並列）
.PHONY: test-all-parallel
test-all-parallel:
	@echo "Running all tests in parallel..."
	@chmod +x tests/unified_test_runner.sh
	@tests/unified_test_runner.sh -b interpreter -p
	@tests/unified_test_runner.sh -b llvm -p
	@tests/unified_test_runner.sh -b llvm-wasm -p
	@echo ""
	@echo "=========================================="
	@echo "✅ All parallel tests completed!"
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

.PHONY: tap
tap: test-all-parallel

.PHONY: c
c: clean

.PHONY: ti
ti: test-interpreter

.PHONY: tip
tip: test-interpreter-parallel

.PHONY: tl
tl: test-llvm

.PHONY: tlp
tlp: test-llvm-parallel

.PHONY: tlw
tlw: test-llvm-wasm

.PHONY: tlwp
tlwp: test-llvm-wasm-parallel

.PHONY: tla
tla: test-llvm-all

.PHONY: tj
tj: test-js

.PHONY: tjp
tjp: test-js-parallel

.PHONY: tol
tol: test-opt-levels

.PHONY: toq
toq: test-opt-quick

# デフォルトファイル設定
FILE ?=
