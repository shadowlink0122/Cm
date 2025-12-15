#!/bin/bash
# CI環境（Ubuntu 24.04 + LLVM 18）をDockerで再現してテスト実行

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# カラー出力
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}CI環境テスト（Docker）${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Dockerイメージ名
IMAGE_NAME="cm-ci-test"

# Dockerイメージビルド
echo -e "${YELLOW}[1/5] Dockerイメージをビルド中...${NC}"
docker build -f "$PROJECT_ROOT/Dockerfile.ci" -t "$IMAGE_NAME" "$PROJECT_ROOT"
echo -e "${GREEN}✓ イメージビルド完了${NC}"
echo ""

# コンテナ内でビルド＆テスト実行
echo -e "${YELLOW}[2/5] CMake設定中...${NC}"
docker run --rm \
    -v "$PROJECT_ROOT:/workspace" \
    -w /workspace \
    "$IMAGE_NAME" \
    bash -c "
        # ホスト側の成果物を削除（クロスビルドの混在を防ぐ）
        rm -f cm cm.exe
        rm -rf build
        cmake -B build -DCMAKE_BUILD_TYPE=Release -DCM_USE_LLVM=ON -DBUILD_TESTING=ON
    "
echo -e "${GREEN}✓ CMake設定完了${NC}"
echo ""

echo -e "${YELLOW}[3/5] ビルド中...${NC}"
docker run --rm \
    -v "$PROJECT_ROOT:/workspace" \
    -w /workspace \
    "$IMAGE_NAME" \
    bash -c "cmake --build build -j\$(nproc)"
echo -e "${GREEN}✓ ビルド完了${NC}"
echo ""

echo -e "${YELLOW}[4/5] RPATHを確認中...${NC}"
docker run --rm \
    -v "$PROJECT_ROOT:/workspace" \
    -w /workspace \
    "$IMAGE_NAME" \
    bash -c "echo '=== Binary Info ===' && file ./cm && echo '' && echo '=== Dependencies ===' && ldd ./cm && echo '' && echo '=== RPATH ===' && readelf -d ./cm | grep -E 'RPATH|RUNPATH' || echo 'No RPATH/RUNPATH found'"
echo ""

echo -e "${YELLOW}[5/5] 全テスト実行中...${NC}"
docker run --rm \
    -v "$PROJECT_ROOT:/workspace" \
    -w /workspace \
    "$IMAGE_NAME" \
    bash -c "
        set -e
        echo '=== Unit Tests ==='
        ./build/bin/lexer_test || echo 'lexer_test failed'
        ./build/bin/hir_lowering_test || echo 'hir_lowering_test failed'
        ./build/bin/mir_lowering_test || echo 'mir_lowering_test failed'
        ./build/bin/mir_optimization_test || echo 'mir_optimization_test failed'
        echo ''
        echo '=== Interpreter Tests (serial) ==='
        make test-interpreter || echo 'Interpreter tests failed'
        echo ''
        echo '=== LLVM Native Tests (serial) ==='
        make test-llvm || echo 'LLVM tests failed'
        echo ''
        echo '=== LLVM WASM Tests (serial) ==='
        make test-llvm-wasm || echo 'WASM tests failed'
    "

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}✓ 全テスト完了${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo -e "${BLUE}Docker環境情報:${NC}"
echo -e "  OS: Ubuntu 24.04"
echo -e "  LLVM: 18"
echo -e "  イメージ名: $IMAGE_NAME"
echo ""
echo -e "${BLUE}追加コマンド:${NC}"
echo -e "  対話シェル起動: ${YELLOW}docker run --rm -it -v \"\$PWD:/workspace\" -w /workspace $IMAGE_NAME${NC}"
echo -e "  イメージ削除: ${YELLOW}docker rmi $IMAGE_NAME${NC}"
