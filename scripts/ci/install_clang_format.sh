#!/usr/bin/env bash
# clang-formatのインストール（ローカル開発環境と同じLLVM系バージョンに固定）
# 使用法: install_clang_format.sh [バージョン]
set -euo pipefail

VERSION="${1:-17}"
sudo apt-get update
sudo apt-get install -y "clang-format-${VERSION}" || {
    wget -q https://apt.llvm.org/llvm.sh && chmod +x llvm.sh
    sudo ./llvm.sh "${VERSION}"
    sudo apt-get install -y "clang-format-${VERSION}"
}
echo "✅ clang-format-${VERSION} インストール完了"
