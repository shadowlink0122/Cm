#!/usr/bin/env bash
# VERSIONに対応するリリースノートの存在チェック（無ければ警告のみ）
set -euo pipefail

FILE_VERSION=$(tr -d '[:space:]' < VERSION)
RELEASE_NOTE="docs/releases/v${FILE_VERSION}.md"

if [ ! -f "$RELEASE_NOTE" ]; then
    echo "::warning::リリースノートが見つかりません: ${RELEASE_NOTE}"
else
    echo "::notice::✅ リリースノート確認: ${RELEASE_NOTE}"
fi
