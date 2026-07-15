#!/usr/bin/env bash
# バックエンドテストスイートの実行（CI/ローカル共用）
# make <target> を実行し、FAIL行と概要をGitHub Annotationとして出力する。
# ログは test_output.log に保存される（CIがartifactとしてアップロード）
# 使用法: run_backend_tests.sh <make_target>
set -uo pipefail

TARGET="${1:?makeターゲット（tjp0等）を指定してください}"
GH_OUTPUT="${GITHUB_OUTPUT:-/dev/null}"

make "$TARGET" 2>&1 | tee test_output.log
TEST_EXIT_CODE=${PIPESTATUS[0]}
echo "test_exit_code=${TEST_EXIT_CODE}" >> "$GH_OUTPUT"

# FAIL行と詳細（Expected/Got/Diff/Output）をAnnotationに出力
awk '
/\[FAIL\]/ {
    fail_line = $0
    gsub(/\033\[[0-9;]*m/, "", fail_line)
    detail = ""
    collecting = 1
    next
}
collecting && /^  (Expected|Got|Diff|Output)/ {
    line = $0
    gsub(/\033\[[0-9;]*m/, "", line)
    detail = detail " | " line
    next
}
collecting && !/^  / {
    print "::error::" fail_line detail
    collecting = 0
}
END {
    if (collecting) {
        print "::error::" fail_line detail
    }
}
' test_output.log

grep -E "^(Total|Passed|Failed|Skipped|Status):" test_output.log | while read -r line; do
    echo "::notice::${line}"
done

exit "${TEST_EXIT_CODE}"
