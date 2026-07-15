#!/usr/bin/env bash
# VERSIONファイルとブランチ名（feature/vX.Y.Z）の整合チェック
# CI: GITHUB_HEAD_REF/GITHUB_REFから判定。ローカル: 現在のブランチ名から判定
set -euo pipefail

if [ ! -f VERSION ]; then
    echo "::error::VERSIONファイルが見つかりません"
    exit 1
fi
FILE_VERSION=$(tr -d '[:space:]' < VERSION)
echo "VERSION file: ${FILE_VERSION}"

if [ -n "${GITHUB_HEAD_REF:-}" ]; then
    BRANCH="$GITHUB_HEAD_REF"
elif [ -n "${GITHUB_REF:-}" ]; then
    BRANCH="${GITHUB_REF#refs/heads/}"
else
    BRANCH="$(git rev-parse --abbrev-ref HEAD)"
fi
echo "Branch: ${BRANCH}"

if [[ "$BRANCH" =~ ^feature/v([0-9]+\.[0-9]+\.[0-9]+)$ ]]; then
    BRANCH_VERSION="${BASH_REMATCH[1]}"
    echo "Branch version: ${BRANCH_VERSION}"

    if [ "$FILE_VERSION" != "$BRANCH_VERSION" ]; then
        echo "::error::バージョン不一致! VERSIONファイル: ${FILE_VERSION}, ブランチ名: feature/v${BRANCH_VERSION}"
        echo ""
        echo "=========================================="
        echo "  ❌ VERSION不一致検出"
        echo "=========================================="
        echo "  VERSIONファイル: ${FILE_VERSION}"
        echo "  ブランチ名:     feature/v${BRANCH_VERSION}"
        echo ""
        echo "  修正方法:"
        echo "    echo '${BRANCH_VERSION}' > VERSION"
        echo "    git add VERSION && git commit -m 'fix: VERSIONを${BRANCH_VERSION}に更新'"
        echo "=========================================="
        exit 1
    fi

    echo "::notice::✅ バージョン一致: ${FILE_VERSION}"
else
    echo "::notice::feature/v*.*.* パターンのブランチではないためスキップ (branch: ${BRANCH})"
fi
