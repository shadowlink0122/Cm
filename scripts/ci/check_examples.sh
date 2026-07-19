#!/usr/bin/env bash
# examplesの全Cmソースが現行コンパイラで型検査を通ることを検証する（リリース同梱物の陳腐化防止）
# モジュール分割されたUEFIサンプルの util/ と libs/ は単体では依存が解決できないため、エントリポイント（examples/uefi/main.cm）の検査で依存ファイルごと検証する
set -euo pipefail

fail=0
while IFS= read -r file; do
    if ! ./cm check "$file" >/dev/null 2>&1; then
        echo "::error file=${file}::cm check failed: ${file}"
        ./cm check "$file" 2>&1 | head -20 || true
        fail=1
    fi
done < <(find examples -name '*.cm' -not -path 'examples/uefi/util/*' -not -path 'examples/uefi/libs/*')

if [ "$fail" -eq 0 ]; then
    echo "✅ examples check passed"
fi
exit "$fail"
