#!/usr/bin/env bash
# VSCode拡張 package.json のバージョンがVERSIONファイルと一致するかチェック
set -euo pipefail

FILE_VERSION=$(tr -d '[:space:]' < VERSION)
PKG_VERSION=$(node -p "require('./vscode-extension/package.json').version")

if [ "$FILE_VERSION" != "$PKG_VERSION" ]; then
    echo "::error::VSCode拡張バージョン不一致! VERSION: ${FILE_VERSION}, package.json: ${PKG_VERSION}"
    echo ""
    echo "=========================================="
    echo "  ❌ VSCode拡張バージョン不一致"
    echo "=========================================="
    echo "  VERSIONファイル:           ${FILE_VERSION}"
    echo "  vscode-extension/package.json: ${PKG_VERSION}"
    echo ""
    echo "  修正方法:"
    echo "    cd vscode-extension && npm run update-version"
    echo "=========================================="
    exit 1
fi

echo "::notice::✅ VSCode拡張バージョン一致: ${PKG_VERSION}"
