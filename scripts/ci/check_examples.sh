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

# セルフホスト素振り（セルフホスト準備 第4段）: OS連携API（args/read_bytes/split/bytes/write_bytes/process）の通し実行を
# jit（cm run -- 引数渡し）とnative（コンパイル済みバイナリへの直接引数渡し）の両方で検証し、成果物の一致まで確認する
DRILL=examples/07_selfhost_drill
./cm run "$DRILL/main.cm" -- "$DRILL/sample_input.cm" -o /tmp/drill_jit.bin
./cm compile "$DRILL/main.cm" -o /tmp/drill_bin
/tmp/drill_bin "$DRILL/sample_input.cm" -o /tmp/drill_native.bin
cmp /tmp/drill_jit.bin /tmp/drill_native.bin
echo "✅ selfhost drill passed (jit/native artifacts identical)"

exit "$fail"
