#!/usr/bin/env bash
# リリース配布物（tar.gz）の構築
# 使用法: build_dist.sh <version> <arch>
set -euo pipefail

VERSION="${1:?バージョンを指定してください}"
ARCH="${2:?アーキテクチャを指定してください}"
OS=$(uname -s | tr 'A-Z' 'a-z')
DIST_DIR="dist/cm-v${VERSION}-${OS}-${ARCH}"

rm -rf "${DIST_DIR}"
mkdir -p "${DIST_DIR}"/{bin,lib,vscode-extension,docs/tutorials,examples}

# コンパイラバイナリ
cp cm "${DIST_DIR}/bin/"

# ランタイムライブラリ
cp build/lib/*.o build/lib/*.a "${DIST_DIR}/lib/" 2>/dev/null || true

# VSCode拡張
cp vscode-extension/cm-language-*.vsix "${DIST_DIR}/vscode-extension/" 2>/dev/null || true

# チュートリアル
cp -r docs/tutorials/ja "${DIST_DIR}/docs/tutorials/"
cp -r docs/tutorials/en "${DIST_DIR}/docs/tutorials/"

# GitHub Pagesリンク案内
cat > "${DIST_DIR}/docs/DOCUMENTATION.md" << 'EOF'
# Cm ドキュメント

## オンラインドキュメント

最新のドキュメントはGitHub Pagesで公開されています：

🌐 https://shadowlink0122.github.io/Cm/

- [クイックスタート](https://shadowlink0122.github.io/Cm/QUICKSTART.html)
- [言語仕様](https://shadowlink0122.github.io/Cm/design/CANONICAL_SPEC.html)
- [チュートリアル一覧](https://shadowlink0122.github.io/Cm/tutorials/)
- [機能リファレンス](https://shadowlink0122.github.io/Cm/features/)
- [リリースノート](https://shadowlink0122.github.io/Cm/releases/)

## オフラインドキュメント

このアーカイブには以下のドキュメントが含まれています：

- `tutorials/ja/` - 日本語チュートリアル
- `tutorials/en/` - 英語チュートリアル
EOF

# サンプルコード（node_modules除外）
cp -r examples/* "${DIST_DIR}/examples/"
find "${DIST_DIR}/examples" -name "node_modules" -type d -exec rm -rf {} + 2>/dev/null || true
find "${DIST_DIR}/examples" -name ".DS_Store" -delete 2>/dev/null || true

# README & VERSION
cp README.md VERSION "${DIST_DIR}/"

# アーカイブ作成
(cd dist && tar czf "cm-v${VERSION}-${OS}-${ARCH}.tar.gz" "cm-v${VERSION}-${OS}-${ARCH}/")

echo "=== Distribution Contents ==="
find "${DIST_DIR}" -type f | head -30
echo "..."
echo "=== Archive ==="
ls -lh "dist/cm-v${VERSION}-${OS}-${ARCH}.tar.gz"
echo "✅ Distribution build complete!"
