#!/usr/bin/env bash
# LLVMツールチェーンのインストール（CI/ローカル共用）
# 使用法: install_llvm.sh [バージョン] [--minimal]
#   --minimal: llvm-devのみ（テスト実行ジョブ用）。省略時はclang/libssl込み（ビルド用）
set -euo pipefail

VERSION="${1:-17}"
MODE="full"
[ "${2:-}" = "--minimal" ] && MODE="minimal"

# GitHub Actions外でも動くように環境ファイルは/dev/nullへフォールバック
GH_PATH="${GITHUB_PATH:-/dev/null}"
GH_ENV="${GITHUB_ENV:-/dev/null}"

case "$(uname -s)" in
    Linux)
        if [ ! -d "/usr/lib/llvm-${VERSION}" ]; then
            wget -q https://apt.llvm.org/llvm.sh
            chmod +x llvm.sh
            sudo ./llvm.sh "${VERSION}"
        fi
        if [ "$MODE" = "minimal" ]; then
            sudo apt-get install -y "llvm-${VERSION}-dev"
        else
            sudo apt-get install -y "llvm-${VERSION}-dev" "clang-${VERSION}" libssl-dev
        fi
        echo "/usr/lib/llvm-${VERSION}/bin" >> "$GH_PATH"
        echo "LLVM_DIR=/usr/lib/llvm-${VERSION}/lib/cmake/llvm" >> "$GH_ENV"
        ;;
    Darwin)
        brew install "llvm@${VERSION}" openssl@3
        LLVM_PREFIX="$(brew --prefix "llvm@${VERSION}")"
        echo "${LLVM_PREFIX}/bin" >> "$GH_PATH"
        echo "LDFLAGS=-L${LLVM_PREFIX}/lib" >> "$GH_ENV"
        echo "CPPFLAGS=-I${LLVM_PREFIX}/include" >> "$GH_ENV"
        echo "LLVM_DIR=${LLVM_PREFIX}/lib/cmake/llvm" >> "$GH_ENV"
        echo "OPENSSL_ROOT_DIR=$(brew --prefix openssl@3)" >> "$GH_ENV"
        ;;
    *)
        echo "Error: 未対応のOSです: $(uname -s)" >&2
        exit 1
        ;;
esac
echo "✅ LLVM ${VERSION} インストール完了 (${MODE})"
