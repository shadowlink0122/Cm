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
	@echo "  make test-llvm        - LLVM ネイティブテスト (O3)"
	@echo "  make test-llvm-wasm   - LLVM WebAssemblyテスト (Oz)"
	@echo "  make test-llvm-all    - すべてのLLVMテスト"
	@echo ""
	@echo "Test Commands (Optimization Levels):"
	@echo "  make test-interpreter-o0/o1/o2/o3  - インタプリタ最適化レベル別テスト（シリアル）"
	@echo "  make test-llvm-o0/o1/o2/o3         - LLVM最適化レベル別テスト（シリアル）"
	@echo "  make test-llvm-wasm-o0/o1/o2/o3    - WASM最適化レベル別テスト（シリアル）"
	@echo "  make test-js-o0/o1/o2/o3           - JS最適化レベル別テスト（シリアル）"
	@echo ""
	@echo "Test Commands (Optimization Levels - Parallel):"
	@echo "  make test-*-o0/o1/o2/o3-parallel   - パラレル実行版（高速）"
	@echo ""
	@echo "Test Commands (All Optimization Levels):"
	@echo "  make test-interpreter-all-opts     - インタプリタ全最適化レベルテスト"
	@echo "  make test-llvm-all-opts            - LLVM全最適化レベルテスト"
	@echo "  make test-llvm-wasm-all-opts       - WASM全最適化レベルテスト"
	@echo "  make test-js-all-opts              - JS全最適化レベルテスト"
	@echo "  make test-all-opts                 - 全プラットフォーム・全最適化レベルテスト"
	@echo ""
	@echo "  make test-all         - すべてのテストを実行"
	@echo ""
	@echo "Run Commands:"
	@echo "  make run FILE=<file>       - Cmファイルを実行"
	@echo "  make run-debug FILE=<file> - デバッグモードで実行"
	@echo ""
	@echo "Development Commands:"
	@echo "  make format       - C++コードを自動フォーマット"
	@echo "  make format-check - フォーマットをチェック"
	@echo "  make lint         - C++コードを静的解析(clang-tidy)"
	@echo ""
	@echo "Quick Shortcuts:"
	@echo "  make b   - build"
	@echo "  make t   - test"
	@echo "  make ta  - test-all"
	@echo "  make tao - test-all-opts (全最適化レベルテスト)"
	@echo "  make tl  - test-llvm"
	@echo "  make tlw - test-llvm-wasm"
	@echo "  make tla - test-llvm-all"
	@echo "  make tj  - test-js"
	@echo "  make tjp - test-js-parallel"
	@echo "  make ti0/ti1/ti2/ti3 - インタプリタ O0-O3（シリアル）"
	@echo "  make tl0/tl1/tl2/tl3 - LLVM O0-O3（シリアル）"
	@echo "  make tlw0/tlw1/tlw2/tlw3 - WASM O0-O3（シリアル）"
	@echo "  make tj0/tj1/tj2/tj3 - JS O0-O3（シリアル）"
	@echo "  make tip0/tip1/tip2/tip3 - インタプリタ O0-O3（パラレル）"
	@echo "  make tlp0/tlp1/tlp2/tlp3 - LLVM O0-O3（パラレル）"
	@echo "  make tlwp0/tlwp1/tlwp2/tlwp3 - WASM O0-O3（パラレル）"
	@echo "  make tjp0/tjp1/tjp2/tjp3 - JS O0-O3（パラレル）"

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

# インタプリタテスト（デフォルトはO3）
.PHONY: test-interpreter
test-interpreter:
	@echo "Running interpreter tests (O3)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b interpreter

# 並列インタプリタテスト
.PHONY: test-interpreter-parallel
test-interpreter-parallel:
	@echo "Running interpreter tests (parallel, O3)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b interpreter -p

# インタプリタ最適化レベル別テスト（シリアル）
.PHONY: test-interpreter-o0
test-interpreter-o0:
	@echo "Running interpreter tests (O0, serial)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=0 tests/unified_test_runner.sh -b interpreter

.PHONY: test-interpreter-o1
test-interpreter-o1:
	@echo "Running interpreter tests (O1, serial)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=1 tests/unified_test_runner.sh -b interpreter

.PHONY: test-interpreter-o2
test-interpreter-o2:
	@echo "Running interpreter tests (O2, serial)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=2 tests/unified_test_runner.sh -b interpreter

.PHONY: test-interpreter-o3
test-interpreter-o3:
	@echo "Running interpreter tests (O3, serial)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b interpreter

# インタプリタ最適化レベル別テスト（パラレル）
.PHONY: test-interpreter-o0-parallel
test-interpreter-o0-parallel:
	@echo "Running interpreter tests (O0, parallel)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=0 tests/unified_test_runner.sh -b interpreter -p

.PHONY: test-interpreter-o1-parallel
test-interpreter-o1-parallel:
	@echo "Running interpreter tests (O1, parallel)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=1 tests/unified_test_runner.sh -b interpreter -p

.PHONY: test-interpreter-o2-parallel
test-interpreter-o2-parallel:
	@echo "Running interpreter tests (O2, parallel)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=2 tests/unified_test_runner.sh -b interpreter -p

.PHONY: test-interpreter-o3-parallel
test-interpreter-o3-parallel:
	@echo "Running interpreter tests (O3, parallel)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b interpreter -p

.PHONY: test-interpreter-all-opts
test-interpreter-all-opts: test-interpreter-o0-parallel test-interpreter-o1-parallel test-interpreter-o2-parallel test-interpreter-o3-parallel
	@echo ""
	@echo "=========================================="
	@echo "✅ All interpreter optimization level tests completed!"
	@echo "=========================================="

# ========================================
# LLVM Backend Test Commands
# ========================================

# LLVM ネイティブテスト（デフォルトはO3）
.PHONY: test-llvm
test-llvm:
	@echo "Running LLVM native code generation tests (O3)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b llvm

# LLVM ネイティブテスト（並列）
.PHONY: test-llvm-parallel
test-llvm-parallel:
	@echo "Running LLVM native code generation tests (parallel, O3)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b llvm -p

# LLVM最適化レベル別テスト（シリアル）
.PHONY: test-llvm-o0
test-llvm-o0:
	@echo "Running LLVM native tests (O0, serial)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=0 tests/unified_test_runner.sh -b llvm

.PHONY: test-llvm-o1
test-llvm-o1:
	@echo "Running LLVM native tests (O1, serial)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=1 tests/unified_test_runner.sh -b llvm

.PHONY: test-llvm-o2
test-llvm-o2:
	@echo "Running LLVM native tests (O2, serial)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=2 tests/unified_test_runner.sh -b llvm

.PHONY: test-llvm-o3
test-llvm-o3:
	@echo "Running LLVM native tests (O3, serial)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b llvm

# LLVM最適化レベル別テスト（パラレル）
.PHONY: test-llvm-o0-parallel
test-llvm-o0-parallel:
	@echo "Running LLVM native tests (O0, parallel)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=0 tests/unified_test_runner.sh -b llvm -p

.PHONY: test-llvm-o1-parallel
test-llvm-o1-parallel:
	@echo "Running LLVM native tests (O1, parallel)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=1 tests/unified_test_runner.sh -b llvm -p

.PHONY: test-llvm-o2-parallel
test-llvm-o2-parallel:
	@echo "Running LLVM native tests (O2, parallel)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=2 tests/unified_test_runner.sh -b llvm -p

.PHONY: test-llvm-o3-parallel
test-llvm-o3-parallel:
	@echo "Running LLVM native tests (O3, parallel)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b llvm -p

.PHONY: test-llvm-all-opts
test-llvm-all-opts: test-llvm-o0-parallel test-llvm-o1-parallel test-llvm-o2-parallel test-llvm-o3-parallel
	@echo ""
	@echo "=========================================="
	@echo "✅ All LLVM optimization level tests completed!"
	@echo "=========================================="

# LLVM WebAssemblyテスト（デフォルトはO3）
.PHONY: test-llvm-wasm
test-llvm-wasm:
	@echo "Running LLVM WebAssembly code generation tests (O3)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b llvm-wasm

# LLVM WebAssemblyテスト（並列）
.PHONY: test-llvm-wasm-parallel
test-llvm-wasm-parallel:
	@echo "Running LLVM WebAssembly code generation tests (parallel, O3)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b llvm-wasm -p

# WASM最適化レベル別テスト（シリアル）
.PHONY: test-llvm-wasm-o0
test-llvm-wasm-o0:
	@echo "Running WASM tests (O0, serial)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=0 tests/unified_test_runner.sh -b llvm-wasm

.PHONY: test-llvm-wasm-o1
test-llvm-wasm-o1:
	@echo "Running WASM tests (O1, serial)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=1 tests/unified_test_runner.sh -b llvm-wasm

.PHONY: test-llvm-wasm-o2
test-llvm-wasm-o2:
	@echo "Running WASM tests (O2, serial)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=2 tests/unified_test_runner.sh -b llvm-wasm

.PHONY: test-llvm-wasm-o3
test-llvm-wasm-o3:
	@echo "Running WASM tests (O3, serial)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b llvm-wasm

# WASM最適化レベル別テスト（パラレル）
.PHONY: test-llvm-wasm-o0-parallel
test-llvm-wasm-o0-parallel:
	@echo "Running WASM tests (O0, parallel)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=0 tests/unified_test_runner.sh -b llvm-wasm -p

.PHONY: test-llvm-wasm-o1-parallel
test-llvm-wasm-o1-parallel:
	@echo "Running WASM tests (O1, parallel)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=1 tests/unified_test_runner.sh -b llvm-wasm -p

.PHONY: test-llvm-wasm-o2-parallel
test-llvm-wasm-o2-parallel:
	@echo "Running WASM tests (O2, parallel)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=2 tests/unified_test_runner.sh -b llvm-wasm -p

.PHONY: test-llvm-wasm-o3-parallel
test-llvm-wasm-o3-parallel:
	@echo "Running WASM tests (O3, parallel)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b llvm-wasm -p

.PHONY: test-llvm-wasm-all-opts
test-llvm-wasm-all-opts: test-llvm-wasm-o0-parallel test-llvm-wasm-o1-parallel test-llvm-wasm-o2-parallel test-llvm-wasm-o3-parallel
	@echo ""
	@echo "=========================================="
	@echo "✅ All WASM optimization level tests completed!"
	@echo "=========================================="

# ========================================
# JavaScript Backend Test Commands
# ========================================

# JavaScript テスト（デフォルトはO3）
.PHONY: test-js
test-js:
	@echo "Running JavaScript code generation tests (O3)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b js

# JavaScript テスト（並列）
.PHONY: test-js-parallel
test-js-parallel:
	@echo "Running JavaScript code generation tests (parallel, O3)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b js -p

# JS最適化レベル別テスト（シリアル）
.PHONY: test-js-o0
test-js-o0:
	@echo "Running JS tests (O0, serial)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=0 tests/unified_test_runner.sh -b js

.PHONY: test-js-o1
test-js-o1:
	@echo "Running JS tests (O1, serial)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=1 tests/unified_test_runner.sh -b js

.PHONY: test-js-o2
test-js-o2:
	@echo "Running JS tests (O2, serial)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=2 tests/unified_test_runner.sh -b js

.PHONY: test-js-o3
test-js-o3:
	@echo "Running JS tests (O3, serial)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b js

# JS最適化レベル別テスト（パラレル）
.PHONY: test-js-o0-parallel
test-js-o0-parallel:
	@echo "Running JS tests (O0, parallel)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=0 tests/unified_test_runner.sh -b js -p

.PHONY: test-js-o1-parallel
test-js-o1-parallel:
	@echo "Running JS tests (O1, parallel)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=1 tests/unified_test_runner.sh -b js -p

.PHONY: test-js-o2-parallel
test-js-o2-parallel:
	@echo "Running JS tests (O2, parallel)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=2 tests/unified_test_runner.sh -b js -p

.PHONY: test-js-o3-parallel
test-js-o3-parallel:
	@echo "Running JS tests (O3, parallel)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b js -p

.PHONY: test-js-all-opts
test-js-all-opts: test-js-o0-parallel test-js-o1-parallel test-js-o2-parallel test-js-o3-parallel
	@echo ""
	@echo "=========================================="
	@echo "✅ All JS optimization level tests completed!"
	@echo "=========================================="

# すべてのLLVMテストを実行
.PHONY: test-llvm-all
test-llvm-all: test-llvm test-llvm-wasm
	@echo ""
	@echo "=========================================="
	@echo "✅ All LLVM tests completed!"
	@echo "=========================================="

# すべての最適化レベルでテスト
.PHONY: test-all-opts
test-all-opts: test-interpreter-all-opts test-llvm-all-opts test-llvm-wasm-all-opts test-js-all-opts
	@echo ""
	@echo "=========================================="
	@echo "✅ All optimization level tests completed!"
	@echo "  - Interpreter: O0-O3"
	@echo "  - LLVM Native: O0-O3"
	@echo "  - LLVM WASM: O0-O3"
	@echo "  - JavaScript: O0-O3"
	@echo "=========================================="

# すべてのテストを実行（並列）
.PHONY: test-all-parallel
test-all-parallel:
	@echo "Running all tests in parallel..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b interpreter -p
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b llvm -p
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b llvm-wasm -p
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b js -p
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

.PHONY: lint
lint: format-check

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

.PHONY: tao
tao: test-all-opts

.PHONY: c
c: clean

.PHONY: ti
ti: test-interpreter

.PHONY: tip
tip: test-interpreter-parallel

.PHONY: ti0
ti0: test-interpreter-o0

.PHONY: ti1
ti1: test-interpreter-o1

.PHONY: ti2
ti2: test-interpreter-o2

.PHONY: ti3
ti3: test-interpreter-o3

.PHONY: tip0
tip0: test-interpreter-o0-parallel

.PHONY: tip1
tip1: test-interpreter-o1-parallel

.PHONY: tip2
tip2: test-interpreter-o2-parallel

.PHONY: tip3
tip3: test-interpreter-o3-parallel

.PHONY: tl
tl: test-llvm

.PHONY: tlp
tlp: test-llvm-parallel

.PHONY: tl0
tl0: test-llvm-o0

.PHONY: tl1
tl1: test-llvm-o1

.PHONY: tl2
tl2: test-llvm-o2

.PHONY: tl3
tl3: test-llvm-o3

.PHONY: tlp0
tlp0: test-llvm-o0-parallel

.PHONY: tlp1
tlp1: test-llvm-o1-parallel

.PHONY: tlp2
tlp2: test-llvm-o2-parallel

.PHONY: tlp3
tlp3: test-llvm-o3-parallel

.PHONY: tlw
tlw: test-llvm-wasm

.PHONY: tlwp
tlwp: test-llvm-wasm-parallel

.PHONY: tlw0
tlw0: test-llvm-wasm-o0

.PHONY: tlw1
tlw1: test-llvm-wasm-o1

.PHONY: tlw2
tlw2: test-llvm-wasm-o2

.PHONY: tlw3
tlw3: test-llvm-wasm-o3

.PHONY: tlwp0
tlwp0: test-llvm-wasm-o0-parallel

.PHONY: tlwp1
tlwp1: test-llvm-wasm-o1-parallel

.PHONY: tlwp2
tlwp2: test-llvm-wasm-o2-parallel

.PHONY: tlwp3
tlwp3: test-llvm-wasm-o3-parallel

.PHONY: tla
tla: test-llvm-all

.PHONY: tj
tj: test-js

.PHONY: tjp
tjp: test-js-parallel

.PHONY: tj0
tj0: test-js-o0

.PHONY: tj1
tj1: test-js-o1

.PHONY: tj2
tj2: test-js-o2

.PHONY: tj3
tj3: test-js-o3

.PHONY: tjp0
tjp0: test-js-o0-parallel

.PHONY: tjp1
tjp1: test-js-o1-parallel

.PHONY: tjp2
tjp2: test-js-o2-parallel

.PHONY: tjp3
tjp3: test-js-o3-parallel

# デフォルトファイル設定
FILE ?=
