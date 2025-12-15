#!/bin/bash
# Docker CI環境で対話シェルを起動

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

IMAGE_NAME="cm-ci-test"

# イメージが存在しない場合はビルド
if ! docker image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
    echo "イメージが存在しないため、ビルドします..."
    docker build -f "$PROJECT_ROOT/Dockerfile.ci" -t "$IMAGE_NAME" "$PROJECT_ROOT"
fi

echo "Docker CI環境（Ubuntu 24.04 + LLVM 18）で対話シェルを起動します..."
echo "終了するには 'exit' を入力してください"
echo ""

docker run --rm -it \
    -v "$PROJECT_ROOT:/workspace" \
    -w /workspace \
    "$IMAGE_NAME" \
    /bin/bash
