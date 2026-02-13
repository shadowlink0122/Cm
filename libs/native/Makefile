# libs/native/Makefile - Cmランタイムライブラリビルド
# コンパイラ(cm)とは独立してネイティブランタイム(.o/.a)をビルド

# ========================================
# 設定
# ========================================

# ルートからの相対パス
ROOT_DIR := ../..
BUILD_LIB := $(ROOT_DIR)/build/lib
SRC_DIR := $(ROOT_DIR)/src

# アーキテクチャ（ルートMakefileから引数として渡される）
ARCH ?= $(shell uname -m)

# クロスコンパイル用フラグ
ifeq ($(shell uname -s),Darwin)
  # macOS: システムclangを使用
  CC := /usr/bin/clang
  CXX := /usr/bin/clang++
  ARCH_FLAG := -arch $(ARCH)
  # OpenSSL検出（Homebrew）
  ifeq ($(ARCH),arm64)
    BREW_PREFIX ?= /opt/homebrew
  else ifeq ($(ARCH),aarch64)
    BREW_PREFIX ?= /opt/homebrew
  else
    BREW_PREFIX ?= /usr/local
  endif
  OPENSSL_PREFIX := $(BREW_PREFIX)/opt/openssl@3
else
  # Linux
  CC := clang
  CXX := clang++
  ifeq ($(ARCH),x86_64)
    ARCH_FLAG := --target=x86_64-unknown-linux-gnu
  else
    ARCH_FLAG := --target=aarch64-unknown-linux-gnu
  endif
  OPENSSL_PREFIX := /usr
endif

# 共通フラグ
CFLAGS := -O2 -ffunction-sections -fdata-sections $(ARCH_FLAG)
CXXFLAGS := -O2 -std=c++17 -ffunction-sections -fdata-sections $(ARCH_FLAG)

# OpenSSL検出
HAS_OPENSSL := $(shell [ -f "$(OPENSSL_PREFIX)/include/openssl/ssl.h" ] && echo yes || echo no)

# ========================================
# ターゲット
# ========================================

# 必須ランタイム（常にビルド）
CORE_TARGETS := \
	$(BUILD_LIB)/cm_runtime.o

# stdランタイム
STD_TARGETS := \
	$(BUILD_LIB)/cm_net_runtime.o \
	$(BUILD_LIB)/cm_sync_runtime.a \
	$(BUILD_LIB)/cm_thread_runtime.o \
	$(BUILD_LIB)/cm_http_runtime.o

# Apple限定
ifeq ($(shell uname -s),Darwin)
  STD_TARGETS += $(BUILD_LIB)/cm_gpu_runtime.o
endif

# WASMランタイム（clangのwasm32ターゲットが必要）
WASM_TARGETS := $(BUILD_LIB)/cm_runtime_wasm.o

# デフォルトターゲット
.PHONY: all
all: $(CORE_TARGETS) $(STD_TARGETS)
	@echo "✅ ランタイムライブラリのビルド完了"

# WASM含む全ビルド
.PHONY: all-with-wasm
all-with-wasm: all $(WASM_TARGETS)
	@echo "✅ ランタイムライブラリのビルド完了（WASM含む）"

.PHONY: clean
clean:
	@rm -f $(CORE_TARGETS) $(STD_TARGETS) $(WASM_TARGETS)
	@rm -f $(BUILD_LIB)/cm_sync_runtime_obj.o $(BUILD_LIB)/cm_channel_runtime_obj.o
	@echo "✅ ランタイムライブラリをクリーン"

# ========================================
# コアランタイム
# ========================================

$(BUILD_LIB)/cm_runtime.o: $(SRC_DIR)/codegen/llvm/native/runtime.c
	@mkdir -p $(BUILD_LIB)
	$(CC) -c $< -o $@ $(CFLAGS)

# ========================================
# stdランタイム
# ========================================

# ネットワーク
$(BUILD_LIB)/cm_net_runtime.o: net/net_runtime.cpp
	@mkdir -p $(BUILD_LIB)
	$(CXX) -c $< -o $@ $(CXXFLAGS)

# 同期 (sync + channel → アーカイブ)
$(BUILD_LIB)/cm_sync_runtime_obj.o: sync/sync_runtime.cpp
	@mkdir -p $(BUILD_LIB)
	$(CXX) -c $< -o $@ $(CXXFLAGS)

$(BUILD_LIB)/cm_channel_runtime_obj.o: sync/channel_runtime.cpp
	@mkdir -p $(BUILD_LIB)
	$(CXX) -c $< -o $@ $(CXXFLAGS)

$(BUILD_LIB)/cm_sync_runtime.a: $(BUILD_LIB)/cm_sync_runtime_obj.o $(BUILD_LIB)/cm_channel_runtime_obj.o
	ar rcs $@ $^

# スレッド
$(BUILD_LIB)/cm_thread_runtime.o: thread/thread_runtime.cpp
	@mkdir -p $(BUILD_LIB)
	$(CXX) -c $< -o $@ $(CXXFLAGS)

# HTTP（OpenSSL対応）
$(BUILD_LIB)/cm_http_runtime.o: http/http_runtime.cpp
	@mkdir -p $(BUILD_LIB)
ifeq ($(HAS_OPENSSL),yes)
	$(CXX) -c $< -o $@ $(CXXFLAGS) -DCM_HAS_OPENSSL -I$(OPENSSL_PREFIX)/include
else
	@echo "⚠️  OpenSSL未検出 - HTTPS無効でビルド"
	$(CXX) -c $< -o $@ $(CXXFLAGS)
endif

# GPU (macOSのみ)
$(BUILD_LIB)/cm_gpu_runtime.o: gpu/gpu_runtime.mm
	@mkdir -p $(BUILD_LIB)
	$(CXX) -c $< -o $@ -O2 -std=c++17 -fobjc-arc $(ARCH_FLAG)

# ========================================
# WASMランタイム
# ========================================

$(BUILD_LIB)/cm_runtime_wasm.o: $(SRC_DIR)/codegen/llvm/wasm/runtime_wasm.c
	@mkdir -p $(BUILD_LIB)
	clang -c $< -o $@ -O2 --target=wasm32-unknown-wasi -ffreestanding -fno-builtin

# ========================================
# ヘルプ
# ========================================

.PHONY: help
help:
	@echo "Cm ランタイムライブラリビルド"
	@echo ""
	@echo "  make all          - コアランタイム + stdランタイム"
	@echo "  make all-with-wasm - 上記 + WASMランタイム"
	@echo "  make clean        - ビルド成果物を削除"
	@echo ""
	@echo "オプション:"
	@echo "  ARCH=arm64|x86_64  - ターゲットアーキテクチャ"
