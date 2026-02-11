#!/bin/bash
# Cm Webアプリ ビルド＆起動スクリプト
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CM="${SCRIPT_DIR}/../../cm"

echo "=== Cm Web App ビルド ==="

# 1. CmソースをJSにコンパイル → CSSを生成
echo "[1/3] Cmソースをコンパイル..."
"$CM" compile --target=js "$SCRIPT_DIR/src/app.cm" -o "$SCRIPT_DIR/public/app.js"
echo "  → public/app.js 生成完了"

# 2. app.jsを実行してCSS生成
echo "[2/3] CSSを生成..."
node "$SCRIPT_DIR/public/app.js" > "$SCRIPT_DIR/public/styles.css"
echo "  → public/styles.css 生成完了"

# 3. 開発サーバー起動
echo "[3/3] 開発サーバーを起動..."
echo ""
echo "  http://localhost:3000 でアクセスできます"
echo "  Ctrl+C で停止"
echo ""
cd "$SCRIPT_DIR"
pnpm exec http-server public -p 3000 -c-1
