# Cm Language Makefile
# 便利なコマンドをmakeで実行できるようにします

# 設定
CM := ./cm
BUILD_DIR := build
# ターゲットアーキテクチャ（デフォルト: LLVMホストターゲットから自動検出）
# 使用例: make build ARCH=arm64 / make build ARCH=x86_64
ARCH ?= $(shell llvm-config --host-target 2>/dev/null | cut -d- -f1 || uname -m)

# OS判定
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
  # ========================================
  # macOS: Homebrew前提
  # ========================================

  # アーキテクチャに応じたHomebrewプレフィックスを自動設定
  ifeq ($(ARCH),arm64)
    BREW_PREFIX ?= /opt/homebrew
  else ifeq ($(ARCH),aarch64)
    BREW_PREFIX ?= /opt/homebrew
  else
    BREW_PREFIX ?= /usr/local
  endif

  # LLVM/OpenSSLパスの自動設定
  LLVM_PREFIX := $(BREW_PREFIX)/opt/llvm@17
  OPENSSL_PREFIX := $(BREW_PREFIX)/opt/openssl@3

  # CMake共通フラグ（Homebrew依存のパスを統一）
  CMAKE_ARCH_FLAGS := \
    -DCM_TARGET_ARCH=$(ARCH) \
    -DCMAKE_PREFIX_PATH="$(LLVM_PREFIX);$(OPENSSL_PREFIX)" \
    -DOPENSSL_ROOT_DIR=$(OPENSSL_PREFIX) \
    -DOPENSSL_SSL_LIBRARY=$(OPENSSL_PREFIX)/lib/libssl.dylib \
    -DOPENSSL_CRYPTO_LIBRARY=$(OPENSSL_PREFIX)/lib/libcrypto.dylib \
    -DOPENSSL_INCLUDE_DIR=$(OPENSSL_PREFIX)/include \
    -DCMAKE_C_COMPILER=/usr/bin/clang \
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++

  # ビルド時の環境変数（x86 LDFLAGSの混入防止）
  BUILD_ENV := \
    LDFLAGS="-L$(LLVM_PREFIX)/lib -L$(OPENSSL_PREFIX)/lib" \
    CPPFLAGS="-I$(LLVM_PREFIX)/include -I$(OPENSSL_PREFIX)/include" \
    PATH="$(LLVM_PREFIX)/bin:$(BREW_PREFIX)/bin:$(PATH)"

else
  # ========================================
  # Linux: システムパッケージ前提
  # ========================================

  # CMakeフラグ（システムのfind_packageに任せる）
  CMAKE_ARCH_FLAGS := \
    -DCM_TARGET_ARCH=$(ARCH)

  # LLVM_DIRが設定されている場合はそれを使用
  ifneq ($(LLVM_DIR),)
    CMAKE_ARCH_FLAGS += -DLLVM_DIR=$(LLVM_DIR)
  endif

  # ビルド時の環境変数
  BUILD_ENV :=

endif

# デフォルトターゲット
.PHONY: help
help:
	@echo "Cm Language - Make Commands"
	@echo ""
	@echo "Build Commands:"
	@echo "  make all            - ビルド（テスト含む）"
	@echo "  make build          - コンパイラ + ランタイムのビルド"
	@echo "  make build-compiler - コンパイラのみビルド"
	@echo "  make libs           - ランタイムライブラリのビルド"
	@echo "  make build-all      - テストを含む全ビルド"
	@echo "  make configure      - CMake configure (明示的再構成)"
	@echo "  make release        - リリースビルド"
	@echo "  make dist           - 配布用アーカイブ作成 (.tar.gz)"
	@echo "  make install        - ~/.cm/ にインストール"
	@echo "  make uninstall      - ~/.cm/ からアンインストール"
	@echo "  make clean          - ビルドディレクトリをクリーン"
	@echo "  make rebuild        - クリーン後に再ビルド"
	@echo ""
	@echo "Architecture Options:"
	@echo "  ARCH=arm64   - ARM64ビルド（デフォルト: LLVM自動検出）"
	@echo "  ARCH=x86_64  - x86_64ビルド"
	@echo "  例: make build ARCH=x86_64"
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
	@echo "Test Commands (Baremetal/UEFI):"
	@echo "  make test-baremetal   - ベアメタルコンパイルテスト"
	@echo "  make test-uefi        - UEFIコンパイルテスト"
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
	@echo "  make tjit - test-jit"
	@echo "  make tjitp - test-jit-parallel"
	@echo "  make ti0/ti1/ti2/ti3 - インタプリタ O0-O3（シリアル）"
	@echo "  make tl0/tl1/tl2/tl3 - LLVM O0-O3（シリアル）"
	@echo "  make tlw0/tlw1/tlw2/tlw3 - WASM O0-O3（シリアル）"
	@echo "  make tj0/tj1/tj2/tj3 - JS O0-O3（シリアル）"
	@echo "  make tjit0/tjit1/tjit2/tjit3 - JIT O0-O3（シリアル）"
	@echo "  make tip0/tip1/tip2/tip3 - インタプリタ O0-O3（パラレル）"
	@echo "  make tlp0/tlp1/tlp2/tlp3 - LLVM O0-O3（パラレル）"
	@echo "  make tlwp0/tlwp1/tlwp2/tlwp3 - WASM O0-O3（パラレル）"
	@echo "  make tjp0/tjp1/tjp2/tjp3 - JS O0-O3（パラレル）"
	@echo "  make tjitp0/tjitp1/tjitp2/tjitp3 - JIT O0-O3（パラレル）"

# ========================================
# Build Commands
# ========================================

.PHONY: all
all: build

# CMake configure（初回 or 明示的に実行）
.PHONY: configure
configure:
	@echo "Configuring CMake (debug mode, arch=$(ARCH))..."
	@$(BUILD_ENV) cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug -DCM_USE_LLVM=ON $(CMAKE_ARCH_FLAGS)
	@echo "✅ Configure complete!"

.PHONY: configure-release
configure-release:
	@echo "Configuring CMake (release mode, arch=$(ARCH))..."
	@$(BUILD_ENV) cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DCM_USE_LLVM=ON $(CMAKE_ARCH_FLAGS)
	@echo "✅ Configure complete!"

.PHONY: configure-test
configure-test:
	@echo "Configuring CMake (debug mode with tests, arch=$(ARCH))..."
	@$(BUILD_ENV) cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug -DCM_USE_LLVM=ON -DBUILD_TESTING=ON $(CMAKE_ARCH_FLAGS)
	@echo "✅ Configure complete!"

# コンパイラのみビルド（configureは初回のみ自動実行）
.PHONY: build-compiler
build-compiler:
	@if [ ! -f $(BUILD_DIR)/CMakeCache.txt ]; then \
		echo "初回ビルド: CMake configureを実行..."; \
		$(BUILD_ENV) cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug -DCM_USE_LLVM=ON $(CMAKE_ARCH_FLAGS); \
	fi
	@$(BUILD_ENV) cmake --build $(BUILD_DIR) -j$$(sysctl -n hw.ncpu 2>/dev/null || nproc)
	@echo "✅ Compiler build complete! ($(ARCH))"

# コンパイラ + ランタイムライブラリをビルド
.PHONY: build
build: build-compiler libs
	@echo "✅ Build complete! ($(ARCH))"

.PHONY: libs
libs:
	@echo "Building runtime libraries (arch=$(ARCH))..."
	@$(MAKE) -C libs/native ARCH=$(ARCH) all-with-wasm
	@echo "✅ Runtime libraries build complete!"

.PHONY: libs-clean
libs-clean:
	@$(MAKE) -C libs/native clean

.PHONY: build-all
build-all:
	@if [ ! -f $(BUILD_DIR)/CMakeCache.txt ] || ! grep -q 'BUILD_TESTING:BOOL=ON' $(BUILD_DIR)/CMakeCache.txt 2>/dev/null; then \
		echo "テスト有効で CMake configureを実行..."; \
		$(BUILD_ENV) cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug -DCM_USE_LLVM=ON -DBUILD_TESTING=ON $(CMAKE_ARCH_FLAGS); \
	fi
	@$(BUILD_ENV) cmake --build $(BUILD_DIR) -j$$(sysctl -n hw.ncpu 2>/dev/null || nproc)
	@$(MAKE) libs
	@echo "✅ Build complete (with tests, $(ARCH))!"

.PHONY: release
release:
	@if [ ! -f $(BUILD_DIR)/CMakeCache.txt ] || ! grep -q 'CMAKE_BUILD_TYPE:STRING=Release' $(BUILD_DIR)/CMakeCache.txt 2>/dev/null; then \
		echo "リリースモードで CMake configureを実行..."; \
		$(BUILD_ENV) cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DCM_USE_LLVM=ON $(CMAKE_ARCH_FLAGS); \
	fi
	@$(BUILD_ENV) cmake --build $(BUILD_DIR) -j$$(sysctl -n hw.ncpu 2>/dev/null || nproc)
	@$(MAKE) libs
	@echo "✅ Release build complete! ($(ARCH))"

# 配布物ビルド（tar.gz作成）
# 含まれるもの: コンパイラ, stdランタイム, VSCode拡張, チュートリアル, examples, README
.PHONY: dist
dist: release
	@VERSION=$$(cat VERSION | tr -d '[:space:]'); \
	OS=$$(uname -s | tr 'A-Z' 'a-z'); \
	DIST_DIR=".tmp/cm-v$${VERSION}-$${OS}-$(ARCH)"; \
	DIST_ARCHIVE=".tmp/cm-v$${VERSION}-$${OS}-$(ARCH).tar.gz"; \
	echo "Building distribution: cm-v$${VERSION}-$${OS}-$(ARCH)..."; \
	rm -rf "$${DIST_DIR}" "$${DIST_ARCHIVE}"; \
	mkdir -p "$${DIST_DIR}"/{bin,lib,vscode-extension,docs/tutorials,examples}; \
	cp cm "$${DIST_DIR}/bin/"; \
	cp build/lib/*.o build/lib/*.a "$${DIST_DIR}/lib/" 2>/dev/null || true; \
	if [ -d vscode-extension ]; then \
		(cd vscode-extension && npm install --silent 2>/dev/null && npm run compile --silent 2>/dev/null && \
		 npx @vscode/vsce package --allow-missing-repository --skip-license 2>/dev/null || true); \
		cp vscode-extension/cm-language-*.vsix "$${DIST_DIR}/vscode-extension/" 2>/dev/null || true; \
	fi; \
	cp -r docs/tutorials/ja "$${DIST_DIR}/docs/tutorials/" 2>/dev/null || true; \
	cp -r docs/tutorials/en "$${DIST_DIR}/docs/tutorials/" 2>/dev/null || true; \
	printf '# Cm ドキュメント\n\n## オンラインドキュメント\n\n🌐 https://shadowlink0122.github.io/Cm/\n\n- [クイックスタート](https://shadowlink0122.github.io/Cm/QUICKSTART.html)\n- [言語仕様](https://shadowlink0122.github.io/Cm/design/CANONICAL_SPEC.html)\n- [チュートリアル](https://shadowlink0122.github.io/Cm/tutorials/)\n- [リリースノート](https://shadowlink0122.github.io/Cm/releases/)\n\n## オフラインドキュメント\n\n- tutorials/ja/ - 日本語チュートリアル\n- tutorials/en/ - 英語チュートリアル\n' > "$${DIST_DIR}/docs/DOCUMENTATION.md"; \
	cp -r examples/* "$${DIST_DIR}/examples/" 2>/dev/null || true; \
	find "$${DIST_DIR}/examples" -name "node_modules" -type d -prune -exec rm -rf {} + 2>/dev/null || true; \
	find "$${DIST_DIR}/examples" -name ".DS_Store" -delete 2>/dev/null || true; \
	cp README.md VERSION "$${DIST_DIR}/"; \
	(cd .tmp && tar czf "cm-v$${VERSION}-$${OS}-$(ARCH).tar.gz" "cm-v$${VERSION}-$${OS}-$(ARCH)/"); \
	echo ""; \
	echo "=========================================="; \
	echo "  ✅ Distribution build complete!"; \
	echo "=========================================="; \
	echo "  Archive: $${DIST_ARCHIVE}"; \
	ls -lh "$${DIST_ARCHIVE}" | awk '{print "  Size:    " $$5}'; \
	echo "  Contents:"; \
	echo "    bin/cm             - コンパイラ"; \
	echo "    lib/               - ランタイムライブラリ"; \
	echo "    vscode-extension/  - VSCode拡張 (.vsix)"; \
	echo "    docs/tutorials/    - チュートリアル (ja/en)"; \
	echo "    examples/          - サンプルコード"; \
	echo "    README.md          - プロジェクト説明"; \
	echo "=========================================="

# インストール: ~/.cm/bin/cm と ~/.cm/lib/ にインストール
CM_INSTALL_DIR = $(HOME)/.cm

.PHONY: install
install: release
	@echo "=========================================="
	@echo "  Cm インストール"
	@echo "=========================================="
	@mkdir -p $(CM_INSTALL_DIR)/bin
	@mkdir -p $(CM_INSTALL_DIR)/lib
	@cp -L $(CM) $(CM_INSTALL_DIR)/bin/cm
	@cp build/lib/*.o $(CM_INSTALL_DIR)/lib/ 2>/dev/null || true
	@cp build/lib/*.a $(CM_INSTALL_DIR)/lib/ 2>/dev/null || true
	@echo ""
	@echo "✅ インストール完了!"
	@echo "  バイナリ: $(CM_INSTALL_DIR)/bin/cm"
	@echo "  ライブラリ: $(CM_INSTALL_DIR)/lib/"
	@echo ""
	@if echo "$$PATH" | grep -q "$(CM_INSTALL_DIR)/bin"; then \
		echo "  PATHは設定済みです"; \
	else \
		echo "  以下をシェル設定ファイルに追加してください:"; \
		echo ""; \
		echo "    export PATH=\"$(CM_INSTALL_DIR)/bin:\$$PATH\""; \
		echo ""; \
		echo "  例: echo 'export PATH=\"$(CM_INSTALL_DIR)/bin:\$$PATH\"' >> ~/.zshrc"; \
	fi
	@echo "=========================================="

.PHONY: uninstall
uninstall:
	@echo "Cm をアンインストール中..."
	@rm -rf $(CM_INSTALL_DIR)
	@echo "✅ $(CM_INSTALL_DIR) を削除しました"
	@echo "  シェル設定ファイルからPATH設定も削除してください"

.PHONY: clean
clean:
	@echo "Cleaning build directory..."
	@rm -rf $(CM) $(BUILD_DIR) .tmp/* .cm-cache
	@find . -name "*.o" -not -path "./build/*" -not -path "./.git/*" -delete 2>/dev/null || true
	@find . -name "*.EFI" -not -path "./.git/*" -delete 2>/dev/null || true
	@find . -name "*.lib" -not -path "./build/*" -not -path "./.git/*" -delete 2>/dev/null || true
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
# Integration Test Commands（マクロで自動生成）
# ========================================

# テストターゲット自動生成マクロ
# 引数: $(1)=バックエンド名, $(2)=表示名
define BACKEND_DEFAULT_TARGETS
.PHONY: test-$(1)
test-$(1):
	@echo "Running $(2) tests (O3)..."
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b $(1)

.PHONY: test-$(1)-parallel
test-$(1)-parallel:
	@echo "Running $(2) tests (parallel, O3)..."
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b $(1) -p
endef

# 最適化レベル別テスト自動生成マクロ
# 引数: $(1)=バックエンド名, $(2)=表示名, $(3)=最適化レベル(0-3)
define BACKEND_OPT_TARGETS
.PHONY: test-$(1)-o$(3)
test-$(1)-o$(3):
	@echo "Running $(2) tests (O$(3), serial)..."
	@OPT_LEVEL=$(3) tests/unified_test_runner.sh -b $(1)

.PHONY: test-$(1)-o$(3)-parallel
test-$(1)-o$(3)-parallel:
	@echo "Running $(2) tests (O$(3), parallel)..."
	@OPT_LEVEL=$(3) tests/unified_test_runner.sh -b $(1) -p
endef

# 全最適化レベルテスト集約マクロ
# 引数: $(1)=バックエンド名, $(2)=表示名
define BACKEND_ALL_OPTS_TARGET
.PHONY: test-$(1)-all-opts
test-$(1)-all-opts: test-$(1)-o0-parallel test-$(1)-o1-parallel test-$(1)-o2-parallel test-$(1)-o3-parallel
	@echo ""
	@echo "=========================================="
	@echo "✅ All $(2) optimization level tests completed!"
	@echo "=========================================="
endef

# バックエンド定義: 名前 表示名 ショートカット
# --- interpreter ---
$(eval $(call BACKEND_DEFAULT_TARGETS,interpreter,interpreter))
$(foreach o,0 1 2 3,$(eval $(call BACKEND_OPT_TARGETS,interpreter,interpreter,$(o))))
$(eval $(call BACKEND_ALL_OPTS_TARGET,interpreter,interpreter))

# --- jit ---
$(eval $(call BACKEND_DEFAULT_TARGETS,jit,JIT))
$(foreach o,0 1 2 3,$(eval $(call BACKEND_OPT_TARGETS,jit,JIT,$(o))))
$(eval $(call BACKEND_ALL_OPTS_TARGET,jit,JIT))

# --- llvm ---
$(eval $(call BACKEND_DEFAULT_TARGETS,llvm,LLVM native))
$(foreach o,0 1 2 3,$(eval $(call BACKEND_OPT_TARGETS,llvm,LLVM native,$(o))))
$(eval $(call BACKEND_ALL_OPTS_TARGET,llvm,LLVM native))

# --- llvm-wasm ---
$(eval $(call BACKEND_DEFAULT_TARGETS,llvm-wasm,LLVM WASM))
$(foreach o,0 1 2 3,$(eval $(call BACKEND_OPT_TARGETS,llvm-wasm,LLVM WASM,$(o))))
$(eval $(call BACKEND_ALL_OPTS_TARGET,llvm-wasm,LLVM WASM))

# --- js ---
$(eval $(call BACKEND_DEFAULT_TARGETS,js,JavaScript))
$(foreach o,0 1 2 3,$(eval $(call BACKEND_OPT_TARGETS,js,JavaScript,$(o))))
$(eval $(call BACKEND_ALL_OPTS_TARGET,js,JavaScript))

# ========================================
# UEFI / Baremetal Test Commands
# ========================================

# UEFI コンパイルテスト
.PHONY: test-uefi
test-uefi:
	@echo "Running UEFI compile tests..."
	@OPT_LEVEL=2 tests/unified_test_runner.sh -b llvm-uefi -c uefi:uefi_compile

# ベアメタル コンパイルテスト
.PHONY: test-baremetal
test-baremetal:
	@echo "Running Baremetal compile tests..."
	@OPT_LEVEL=2 tests/unified_test_runner.sh -b llvm-baremetal -c "baremetal:baremetal baremetal:errors baremetal:allowed"

# ========================================
# Test Suite Commands
# ========================================

# スイート別テスト自動生成マクロ
# 引数: $(1)=スイート名
define SUITE_TARGET
.PHONY: test-suite-$(1)
test-suite-$(1):
	@echo "Running $(1) suite tests..."
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b jit -s $(1) -p
endef

$(foreach s,core syntax stdlib modules platform runtime,$(eval $(call SUITE_TARGET,$(s))))

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
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b interpreter -p
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b llvm -p
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b llvm-wasm -p
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b js -p
	@echo ""
	@echo "=========================================="
	@echo "✅ All parallel tests completed!"
	@echo "=========================================="

# キャッシュ無効テスト（並列）
.PHONY: tipnc tlpnc twpnc tjpnc test-all-parallel-nc
tipnc: build  ## インタプリタ（パラレル、キャッシュ無効）
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b interpreter -p --no-cache

tlpnc: build  ## LLVM（パラレル、キャッシュ無効）
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b llvm -p --no-cache

twpnc: build  ## WASM（パラレル、キャッシュ無効）
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b llvm-wasm -p --no-cache

tjpnc: build  ## JavaScript（パラレル、キャッシュ無効）
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b js -p --no-cache

test-all-parallel-nc: build  ## 全バックエンド（パラレル、キャッシュ無効）
	@echo "Running all tests in parallel (no cache)..."
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b interpreter -p --no-cache
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b llvm -p --no-cache
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b llvm-wasm -p --no-cache
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b js -p --no-cache
	@echo ""
	@echo "=========================================="
	@echo "✅ All parallel tests (no cache) completed!"
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

# デフォルトファイル設定
FILE ?=

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
# Linter Test Commands
# ========================================

.PHONY: test-lint
test-lint:
	@echo "Running Linter tests..."
	@chmod +x tests/linter/scripts/run_tests.sh
	@tests/linter/scripts/run_tests.sh

.PHONY: tli
tli: test-lint

# ========================================
# Development Commands
# ========================================

.PHONY: format
format:
	@echo "Formatting C++ code..."
	@find src tests -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" \) \
		-exec clang-format -i -style=file {} \;
	@echo "Formatting Cm code..."
	@find tests/programs libs -type f -name "*.cm" -exec ./cm fmt -q {} \;
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
# Quick Shortcuts（マクロで自動生成）
# ========================================

# 基本ショートカット
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

# バックエンド別ショートカット自動生成マクロ
# 引数: $(1)=ショートカットプレフィクス, $(2)=バックエンド名
define SHORTCUT_TEMPLATE
.PHONY: $(1)
$(1): test-$(2)

.PHONY: $(1)p
$(1)p: test-$(2)-parallel

.PHONY: $(1)0
$(1)0: test-$(2)-o0

.PHONY: $(1)1
$(1)1: test-$(2)-o1

.PHONY: $(1)2
$(1)2: test-$(2)-o2

.PHONY: $(1)3
$(1)3: test-$(2)-o3

.PHONY: $(1)p0
$(1)p0: test-$(2)-o0-parallel

.PHONY: $(1)p1
$(1)p1: test-$(2)-o1-parallel

.PHONY: $(1)p2
$(1)p2: test-$(2)-o2-parallel

.PHONY: $(1)p3
$(1)p3: test-$(2)-o3-parallel
endef

# ショートカット生成
$(eval $(call SHORTCUT_TEMPLATE,ti,interpreter))
$(eval $(call SHORTCUT_TEMPLATE,tjit,jit))
$(eval $(call SHORTCUT_TEMPLATE,tl,llvm))
$(eval $(call SHORTCUT_TEMPLATE,tlw,llvm-wasm))
$(eval $(call SHORTCUT_TEMPLATE,tj,js))

# LLVM集約ショートカット
.PHONY: tla
tla: test-llvm-all

# Baremetal/UEFI ショートカット
.PHONY: tb
tb: test-baremetal

.PHONY: tu
tu: test-uefi

.PHONY: tbu
tbu: test-baremetal test-uefi

# ========================================
# Benchmark Commands
# ========================================

# Run all benchmarks (compare with Python, C++, Rust)
.PHONY: bench
bench:
	@echo "=========================================="
	@echo "   Running Cm Language Benchmarks"
	@echo "=========================================="
	@chmod +x tests/bench_marks/run_individual_benchmarks.sh
	@cd tests/bench_marks && ./run_individual_benchmarks.sh

# Quick benchmark (prime numbers only)
.PHONY: bench-prime
bench-prime:
	@echo "Running prime number benchmark..."
	@./cm compile -O3 tests/bench_marks/cm/01_prime.cm -o /tmp/bench_prime
	@time /tmp/bench_prime

# Quick benchmark (fibonacci recursive)
.PHONY: bench-fib
bench-fib:
	@echo "Running fibonacci recursive benchmark..."
	@./cm compile -O3 tests/bench_marks/cm/02_fibonacci_recursive.cm -o /tmp/bench_fib
	@time /tmp/bench_fib

# Quick benchmark (array sort)
.PHONY: bench-sort
bench-sort:
	@echo "Running array sort benchmark..."
	@./cm compile -O3 tests/bench_marks/cm/04_array_sort.cm -o /tmp/bench_sort
	@time /tmp/bench_sort

# Quick benchmark (matrix multiply)
.PHONY: bench-matrix
bench-matrix:
	@echo "Running matrix multiply benchmark..."
	@./cm compile -O3 tests/bench_marks/cm/05_matrix_multiply.cm -o /tmp/bench_matrix
	@time /tmp/bench_matrix

# Run JIT benchmarks (faster than interpreter)
.PHONY: bench-jit
bench-jit:
	@echo "Running JIT benchmarks..."
	@for file in tests/bench_marks/cm/*.cm; do \
		echo "Running $$file..."; \
		time ./cm run --jit $$file; \
		echo ""; \
	done

# Run interpreter benchmarks (slower, for comparison)
.PHONY: bench-interpreter
bench-interpreter:
	@echo "Running interpreter benchmarks..."
	@for file in tests/bench_marks/cm/*.cm; do \
		echo "Running $$file..."; \
		time ./cm run $$file; \
		echo ""; \
	done

# Clean benchmark results
.PHONY: bench-clean
bench-clean:
	@echo "Cleaning benchmark files..."
	@rm -rf tests/bench_marks/results/*
	@rm -f tests/bench_marks/cm/*_native
	@rm -f tests/bench_marks/cpp/01_prime
	@rm -f tests/bench_marks/cpp/02_fibonacci_recursive
	@rm -f tests/bench_marks/cpp/03_fibonacci_iterative
	@rm -f tests/bench_marks/cpp/04_array_sort
	@rm -f tests/bench_marks/cpp/05_matrix_multiply
	@rm -f tests/bench_marks/cpp/*.o
	@rm -f tests/bench_marks/cpp/cpp_results.txt
	@rm -f tests/bench_marks/python/*.pyc
	@rm -rf tests/bench_marks/python/__pycache__
	@rm -f tests/bench_marks/python/python_results.txt
	@if [ -d tests/bench_marks/rust ]; then cd tests/bench_marks/rust && cargo clean 2>/dev/null || true; fi
	@rm -f tests/bench_marks/rust/rust_results.txt
	@rm -f /tmp/bench_*
	@echo "✅ Benchmark cleanup complete!"

# ========================================
# Standard Library Test Commands
# ========================================

# std::asm テスト
.PHONY: test-std-asm-basic
test-std-asm-basic:
	@echo "Running std::asm/basic tests..."
	@mkdir -p .tmp
	@$(CM) run tests/std/asm/basic.cm > .tmp/asm_basic.out 2>&1 || true
	@diff -u tests/std/asm/basic.expect .tmp/asm_basic.out && echo "✅ asm/basic passed!" || echo "❌ asm/basic failed!"

.PHONY: test-std-asm
test-std-asm: test-std-asm-basic
	@echo ""
	@echo "=========================================="
	@echo "✅ All std::asm tests completed!"
	@echo "=========================================="

# すべてのstdライブラリテストを実行
.PHONY: test-std
test-std: test-std-asm
	@echo ""
	@echo "=========================================="
	@echo "✅ All std library tests completed!"
	@echo "=========================================="

# Shortcuts
.PHONY: ts
ts: test-std

# ========================================
# Security Check Commands
# ========================================

# ローカルパス情報チェック（コミット前に必ず実行）
# 注: .agent/workflows/ は例示コードを含むため除外
.PHONY: security-check
security-check:
	@echo "🔒 Checking for local path information..."
	@if grep -rn "/Users/[a-zA-Z]\|/home/[a-zA-Z]\|C:\\\\Users\\\\" docs/ --include="*.md" --include="*.txt" --include="*.yaml" 2>/dev/null | grep -v "^.agent/workflows"; then \
		echo ""; \
		echo "❌ ERROR: Local path information found!"; \
		echo "   Please remove all absolute paths before committing."; \
		echo "   Use: find docs -type f -name '*.md' -exec sed -i '' 's|/Users/username/path/||g' {} \\;"; \
		exit 1; \
	else \
		echo "✅ No local path information found."; \
	fi

# プレコミットチェック
.PHONY: pre-commit
pre-commit: format-check security-check
	@echo ""
	@echo "✅ Pre-commit checks passed!"

.PHONY: sc
sc: security-check

.PHONY: pc
pc: pre-commit
