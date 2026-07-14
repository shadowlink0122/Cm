#!/usr/bin/env bash
# リリース入力バージョンとVERSIONファイル・VSCode拡張の整合チェック
# 使用法: check_release_version.sh <version>
set -euo pipefail

INPUT_VERSION="${1:?リリースバージョンを指定してください}"
FILE_VERSION=$(tr -d '[:space:]' < VERSION)

if [ "$FILE_VERSION" != "$INPUT_VERSION" ]; then
    echo "::error::バージョン不一致! VERSION: ${FILE_VERSION}, 入力: ${INPUT_VERSION}"
    exit 1
fi

PKG_VERSION=$(node -p "require('./vscode-extension/package.json').version")
if [ "$FILE_VERSION" != "$PKG_VERSION" ]; then
    echo "::error::VSCode拡張バージョン不一致! VERSION: ${FILE_VERSION}, package.json: ${PKG_VERSION}"
    exit 1
fi

echo "✅ バージョン整合OK: ${FILE_VERSION}"
