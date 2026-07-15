#!/usr/bin/env bash
# C++フォーマット検査（make format-check と同じ対象。違反があれば非0終了）
# 使用法: check_cpp_format.sh [バージョン]
set -euo pipefail

VERSION="${1:-17}"
FMT="clang-format-${VERSION}"
command -v "$FMT" >/dev/null || FMT="clang-format"

find src tests -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -print0 |
    xargs -0 "$FMT" --dry-run -Werror -style=file
echo "✅ C++フォーマット検査OK"
