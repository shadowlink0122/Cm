#!/usr/bin/env bash
# ビルド成果物の検証
# 使用法: verify_build.sh [--strict]
#   --strict: 成果物欠落を即エラーにする（リリースビルド用）
set -euo pipefail

STRICT=0
[ "${1:-}" = "--strict" ] && STRICT=1

echo "=== Build Verification ==="
if [ "$STRICT" -eq 1 ]; then
    ./cm --version
    echo "=== Runtime Libraries ==="
    ls -la build/lib/
else
    ls -la build/lib/ || echo "No lib directory"
    ./cm --version || echo "cm not found"
fi
echo "✅ Build verification complete"
