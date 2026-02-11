#!/bin/bash
# Cm Webアプリ ビルド＆起動スクリプト
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CM="${SCRIPT_DIR}/../../cm"

echo "=== Cm Web App ビルド ==="

# 1. CSSスタイル生成（styles.cm → styles.css）
echo "[1/4] CSSスタイルをコンパイル..."
"$CM" compile --target=js "$SCRIPT_DIR/src/styles.cm" -o "$SCRIPT_DIR/public/_styles_gen.js"
echo "  → _styles_gen.js 生成完了"

echo "[2/4] CSSを生成..."
node "$SCRIPT_DIR/public/_styles_gen.js" > "$SCRIPT_DIR/public/styles.css"
rm -f "$SCRIPT_DIR/public/_styles_gen.js"
echo "  → public/styles.css 生成完了"

# 2. アプリロジック生成（app.cm → app.js）
echo "[3/4] アプリロジックをコンパイル..."
"$CM" compile --target=js "$SCRIPT_DIR/src/app.cm" -o "$SCRIPT_DIR/public/app.js"
echo "  → public/app.js 生成完了"

# 3. 開発サーバー起動
echo "[4/4] 開発サーバーを起動..."
echo ""
echo "  http://localhost:3000 でアクセスできます"
echo "  Ctrl+C で停止"
echo ""
cd "$SCRIPT_DIR"
pnpm exec http-server public -p 3000 -c-1
