#!/bin/bash
# LLVM バックエンドテストスクリプト

set -e

CM_COMPILER="../../../cm"
BUILD_DIR=".tmp"

# カラー出力
GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== Cm LLVM Backend Tests ===${NC}"

# ビルドディレクトリ作成
mkdir -p $BUILD_DIR

# 1. Native ターゲットテスト
echo -e "\n${BLUE}[1] Testing Native Target...${NC}"

for test in basic/*.cm; do
    name=$(basename $test .cm)
    echo -n "  Testing $name... "

    # コンパイル
    $CM_COMPILER compile $test \
        --backend=llvm \
        --target=native \
        --output=$BUILD_DIR/$name \
        --optimize=2

    # 実行
    if $BUILD_DIR/$name; then
        echo -e "${GREEN}PASS${NC}"
    else
        echo -e "${RED}FAIL${NC}"
        exit 1
    fi
done

# 2. LLVM IR 生成テスト
echo -e "\n${BLUE}[2] Testing LLVM IR Generation...${NC}"

for test in basic/*.cm; do
    name=$(basename $test .cm)
    echo -n "  Generating IR for $name... "

    # LLVM IR生成
    $CM_COMPILER compile $test \
        --backend=llvm \
        --target=native \
        --emit=llvm-ir \
        --output=$BUILD_DIR/$name.ll

    # 検証
    if llvm-as < $BUILD_DIR/$name.ll > /dev/null 2>&1; then
        echo -e "${GREEN}VALID${NC}"
    else
        echo -e "${RED}INVALID${NC}"
        exit 1
    fi
done

# 3. Baremetal ターゲットテスト（コンパイルのみ）
echo -e "\n${BLUE}[3] Testing Baremetal Target...${NC}"

for test in baremetal/*.cm; do
    name=$(basename $test .cm)
    echo -n "  Compiling $name... "

    # オブジェクトファイル生成
    $CM_COMPILER compile $test \
        --backend=llvm \
        --target=baremetal-arm \
        --emit=object \
        --output=$BUILD_DIR/$name.o \
        --no-std

    # 確認
    if [ -f $BUILD_DIR/$name.o ]; then
        echo -e "${GREEN}SUCCESS${NC}"

        # リンカスクリプトも生成されているか確認
        if [ -f link.ld ]; then
            echo "    Linker script generated"
        fi
    else
        echo -e "${RED}FAIL${NC}"
        exit 1
    fi
done

# 4. WebAssembly ターゲットテスト
echo -e "\n${BLUE}[4] Testing WebAssembly Target...${NC}"

for test in wasm/*.cm; do
    name=$(basename $test .cm)
    echo -n "  Compiling $name to WASM... "

    # WASM生成
    $CM_COMPILER compile $test \
        --backend=llvm \
        --target=wasm32 \
        --emit=wasm \
        --output=$BUILD_DIR/$name.wasm \
        --optimize=size

    # 確認
    if [ -f $BUILD_DIR/$name.wasm ]; then
        # wasm-validateがあれば検証
        if command -v wasm-validate &> /dev/null; then
            if wasm-validate $BUILD_DIR/$name.wasm; then
                echo -e "${GREEN}VALID${NC}"
            else
                echo -e "${RED}INVALID${NC}"
                exit 1
            fi
        else
            echo -e "${GREEN}GENERATED${NC}"
        fi
    else
        echo -e "${RED}FAIL${NC}"
        exit 1
    fi
done

# 5. 最適化レベルテスト
echo -e "\n${BLUE}[5] Testing Optimization Levels...${NC}"

test_file="basic/arithmetic.cm"

for opt_level in 0 1 2 3 s z; do
    echo -n "  Testing -O$opt_level... "

    # コンパイル
    $CM_COMPILER compile $test_file \
        --backend=llvm \
        --target=native \
        --output=$BUILD_DIR/opt_test_$opt_level \
        --optimize=$opt_level

    # サイズ確認
    size=$(stat -f%z $BUILD_DIR/opt_test_$opt_level 2>/dev/null || stat -c%s $BUILD_DIR/opt_test_$opt_level)
    echo -e "${GREEN}OK${NC} (size: $size bytes)"
done

# クリーンアップ
echo -e "\n${BLUE}Cleaning up...${NC}"
rm -rf $BUILD_DIR
rm -f link.ld

echo -e "\n${GREEN}=== All tests passed! ===${NC}"