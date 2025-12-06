# Cm Language Makefile
# 便利なコマンドをmakeで実行できるようにします

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
	@echo "Test Commands:"
	@echo "  make test           - すべてのテストを実行"
	@echo "  make test-unit      - ユニットテストのみ"
	@echo "  make test-lexer     - Lexerテストのみ"
	@echo "  make test-hir       - HIR Loweringテストのみ"
	@echo "  make test-mir       - MIR Loweringテストのみ"
	@echo "  make test-opt       - 最適化テストのみ"
	@echo ""
	@echo "Run Commands:"
	@echo "  make run FILE=<file> - Cmファイルを実行"
	@echo "  make run-debug FILE=<file> - デバッグモードで実行"
	@echo "  make run-verbose FILE=<file> - 詳細モードで実行"
	@echo ""
	@echo "Example Commands:"
	@echo "  make run-hello      - Hello Worldサンプル実行"
	@echo "  make run-variables  - 変数サンプル実行"
	@echo "  make run-format     - フォーマット文字列サンプル実行"
	@echo "  make run-all-examples - すべてのサンプルを実行"
	@echo ""
	@echo "Code Generation Tests:"
	@echo "  make test-compile-rust - Rustコード生成テスト"
	@echo "  make test-compile-ts   - TypeScriptコード生成テスト"
	@echo "  make test-compile-wasm - WASMコード生成テスト"
	@echo ""
	@echo "Analysis Commands:"
	@echo "  make check FILE=<file> - 型チェックのみ"
	@echo "  make ast FILE=<file>   - AST表示"
	@echo "  make hir FILE=<file>   - HIR表示"
	@echo "  make mir FILE=<file>   - MIR表示"
	@echo "  make mir-opt FILE=<file> - 最適化後のMIR表示"
	@echo ""
	@echo "Development Commands:"
	@echo "  make format         - コードフォーマット（clang-format）"
	@echo "  make format-check   - フォーマットチェック（修正なし）"
	@echo "  make lint           - 静的解析（clang-tidy）"
	@echo "  make docker-build   - Docker環境でビルド"
	@echo "  make docker-test    - Docker環境でテスト"
	@echo ""
	@echo "Documentation:"
	@echo "  make docs           - ドキュメント生成"
	@echo "  make serve-docs     - ドキュメントをローカルサーバーで表示"

# ========================================
# Build Commands
# ========================================

.PHONY: build
build:
	@echo "Building Cm compiler (debug mode)..."
	@cmake -B build -DCMAKE_BUILD_TYPE=Debug -G Ninja
	@cmake --build build

.PHONY: release
release:
	@echo "Building Cm compiler (release mode)..."
	@cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja
	@cmake --build build

.PHONY: clean
clean:
	@echo "Cleaning build directory..."
	@rm -rf build

.PHONY: rebuild
rebuild: clean build

# ========================================
# Test Commands
# ========================================

.PHONY: test
test:
	@echo "Running all tests..."
	@cd build && ctest --output-on-failure

.PHONY: test-unit
test-unit:
	@echo "Running unit tests..."
	@cd build && ctest -L unit --output-on-failure

.PHONY: test-lexer
test-lexer:
	@echo "Running lexer tests..."
	@cd build && ctest -R lexer --output-on-failure

.PHONY: test-hir
test-hir:
	@echo "Running HIR lowering tests..."
	@cd build && ctest -R hir_lowering --output-on-failure

.PHONY: test-mir
test-mir:
	@echo "Running MIR lowering tests..."
	@cd build && ctest -R mir_lowering --output-on-failure

.PHONY: test-opt
test-opt:
	@echo "Running optimization tests..."
	@cd build && ctest -R optimization --output-on-failure

# ========================================
# Run Commands
# ========================================

# 実行ファイルのパス
CM := ./cm

.PHONY: run
run:
	@if [ -z "$(FILE)" ]; then echo "Usage: make run FILE=<file.cm>"; exit 1; fi
	@echo "Running $(FILE)..."
	@$(CM) run $(FILE)

.PHONY: run-debug
run-debug:
	@if [ -z "$(FILE)" ]; then echo "Usage: make run-debug FILE=<file.cm>"; exit 1; fi
	@echo "Running $(FILE) in debug mode..."
	@$(CM) run $(FILE) -d

.PHONY: run-verbose
run-verbose:
	@if [ -z "$(FILE)" ]; then echo "Usage: make run-verbose FILE=<file.cm>"; exit 1; fi
	@echo "Running $(FILE) in verbose mode..."
	@$(CM) run $(FILE) --verbose

# ========================================
# Example Commands
# ========================================

.PHONY: run-hello
run-hello:
	@echo "Running Hello World example..."
	@$(CM) run examples/basics/02_hello.cm

.PHONY: run-variables
run-variables:
	@echo "Running variables example..."
	@$(CM) run examples/basics/03_variables.cm

.PHONY: run-format
run-format:
	@echo "Running format string example..."
	@$(CM) run examples/basics/07_auto_variable_capture.cm

.PHONY: run-all-examples
run-all-examples:
	@echo "Running all examples..."
	@for file in examples/basics/*.cm; do \
		echo "========================================"; \
		echo "Running $$file"; \
		echo "========================================"; \
		$(CM) run $$file || true; \
		echo ""; \
	done

# ========================================
# Code Generation Tests
# ========================================

.PHONY: test-compile-rust
test-compile-rust:
	@echo "Testing Rust code generation..."
	@echo "// Test Rust compilation" > /tmp/test.cm
	@echo "int main() { return 42; }" >> /tmp/test.cm
	@$(CM) compile /tmp/test.cm --emit-rust
	@echo "Rust code generated successfully!"

.PHONY: test-compile-ts
test-compile-ts:
	@echo "Testing TypeScript code generation..."
	@echo "// Test TypeScript compilation" > /tmp/test.cm
	@echo "int main() { return 42; }" >> /tmp/test.cm
	@$(CM) compile /tmp/test.cm --emit-ts
	@echo "TypeScript code generated successfully!"

.PHONY: test-compile-wasm
test-compile-wasm:
	@echo "Testing WASM code generation..."
	@echo "// Test WASM compilation" > /tmp/test.cm
	@echo "int main() { return 42; }" >> /tmp/test.cm
	@$(CM) compile /tmp/test.cm --emit-wasm
	@echo "WASM code generated successfully!"

# ========================================
# Analysis Commands
# ========================================

.PHONY: check
check:
	@if [ -z "$(FILE)" ]; then echo "Usage: make check FILE=<file.cm>"; exit 1; fi
	@echo "Type checking $(FILE)..."
	@$(CM) check $(FILE)

.PHONY: ast
ast:
	@if [ -z "$(FILE)" ]; then echo "Usage: make ast FILE=<file.cm>"; exit 1; fi
	@echo "Showing AST for $(FILE)..."
	@$(CM) run $(FILE) --ast

.PHONY: hir
hir:
	@if [ -z "$(FILE)" ]; then echo "Usage: make hir FILE=<file.cm>"; exit 1; fi
	@echo "Showing HIR for $(FILE)..."
	@$(CM) run $(FILE) --hir

.PHONY: mir
mir:
	@if [ -z "$(FILE)" ]; then echo "Usage: make mir FILE=<file.cm>"; exit 1; fi
	@echo "Showing MIR for $(FILE)..."
	@$(CM) run $(FILE) --mir

.PHONY: mir-opt
mir-opt:
	@if [ -z "$(FILE)" ]; then echo "Usage: make mir-opt FILE=<file.cm>"; exit 1; fi
	@echo "Showing optimized MIR for $(FILE)..."
	@$(CM) run $(FILE) --mir-opt

# ========================================
# Development Commands
# ========================================

.PHONY: format
format:
	@echo "Formatting C++ code..."
	@find src tests -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" -o -name "*.cc" \) | xargs clang-format -i -style=file
	@echo "Format complete!"

.PHONY: format-check
format-check:
	@echo "Checking code formatting..."
	@find src tests -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" -o -name "*.cc" \) | xargs clang-format -style=file -dry-run -Werror 2>&1 | grep -q "error" && \
		(echo "❌ Format check failed! Run 'make format' to fix." && exit 1) || \
		echo "✅ Format check passed!"

.PHONY: lint
lint:
	@echo "Running clang-tidy..."
	@find src -name "*.cpp" | xargs clang-tidy -p build

.PHONY: docker-build
docker-build:
	@echo "Building in Docker..."
	@docker compose run --rm build-clang

.PHONY: docker-test
docker-test:
	@echo "Testing in Docker..."
	@docker compose run --rm test

# ========================================
# Documentation
# ========================================

.PHONY: docs
docs:
	@echo "Generating documentation..."
	@doxygen Doxyfile 2>/dev/null || echo "Doxygen not installed"

.PHONY: serve-docs
serve-docs:
	@echo "Serving documentation at http://localhost:8000"
	@cd docs && python3 -m http.server

# ========================================
# Installation
# ========================================

.PHONY: install
install:
	@echo "Installing Cm compiler..."
	@cmake --install build --prefix ~/.local

.PHONY: uninstall
uninstall:
	@echo "Uninstalling Cm compiler..."
	@rm -f ~/.local/bin/cm

# ========================================
# Version Info
# ========================================

.PHONY: version
version:
	@$(CM) --version

# ========================================
# Benchmark
# ========================================

.PHONY: bench
bench:
	@echo "Running benchmarks..."
	@time $(CM) run examples/optimization/01_optimization.cm
	@echo ""
	@echo "With optimization:"
	@time $(CM) run examples/optimization/01_optimization.cm -O2

# ========================================
# Quick Development Shortcuts
# ========================================

.PHONY: b
b: build

.PHONY: t
t: test

.PHONY: r
r: run

.PHONY: c
c: clean

# デフォルトファイル設定（開発時の便利設定）
FILE ?= examples/basics/01_simple.cm