#!/usr/bin/env bash
# VSCode拡張のビルドと.vsixパッケージング（vscode-extension/で実行）
set -euo pipefail

cd vscode-extension
npm install
npm run compile
npm run verify-version
npx @vscode/vsce package --allow-missing-repository --skip-license
echo "✅ VSCode extension packaged"
