#!/usr/bin/env bash
# チュートリアルのバージョン表記がVERSIONファイルと一致するかチェック
set -euo pipefail

if ! python3 scripts/update_tutorial_version.py --check; then
    echo "::error::チュートリアルのバージョン表記がVERSIONファイルと乖離しています"
    echo ""
    echo "=========================================="
    echo "  ❌ ドキュメントのバージョン記載漏れ検出"
    echo "=========================================="
    echo "  修正方法:"
    echo "    make update-docs-version"
    echo "    git add docs/tutorials && git commit -m 'docs: チュートリアルのバージョン表記を更新'"
    echo "=========================================="
    exit 1
fi
