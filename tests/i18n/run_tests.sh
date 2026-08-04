#!/bin/bash
# ============================================================
# エラーメッセージ・i18nのE2Eテスト
# ============================================================
# cmバイナリの実際の出力メッセージを英語（デフォルト）と日本語の両方で検証する。
# 言語の決定順序（--lang > CM_LANG > .cmconfig.yml language:)も確認する。
#
# 期待値は expects/<ケース名>.expect に1行1条件で記述する:
#   通常行   = 出力に含まれるべき部分文字列
#   ! 接頭辞 = 出力に含まれてはならない部分文字列
# ============================================================
set -u
cd "$(dirname "$0")/../.."

CM="${CM_EXECUTABLE:-./cm}"
DIR=tests/i18n
PASS=0
FAIL=0

# run_case <ケース名> <コマンド...>
# コマンドの出力を expects/<ケース名>.expect の各行と突き合わせる
run_case() {
    local name="$1"
    shift
    local expect_file="$DIR/expects/$name.expect"
    if [ ! -f "$expect_file" ]; then
        echo "[FAIL] $name (expect file not found: $expect_file)"
        FAIL=$((FAIL + 1))
        return
    fi
    local out
    out=$("$@" 2>&1)
    local ok=1 detail=""
    while IFS= read -r line; do
        [ -z "$line" ] && continue
        case "$line" in
            '!'*)
                if echo "$out" | grep -qF -- "${line#!}"; then
                    ok=0
                    detail="unexpected: ${line#!}"
                fi
                ;;
            *)
                if ! echo "$out" | grep -qF -- "$line"; then
                    ok=0
                    detail="expected: $line"
                fi
                ;;
        esac
    done < "$expect_file"
    if [ $ok -eq 1 ]; then
        echo "[PASS] $name"
        PASS=$((PASS + 1))
    else
        echo "[FAIL] $name ($detail)"
        echo "$out" | sed 's/^/    /' | head -6
        FAIL=$((FAIL + 1))
    fi
}

# ---------- CLIメッセージ（コマンドエラー） ----------
run_case cli-unknown-command-en "$CM" nosuchcmd
run_case cli-unknown-command-ja "$CM" nosuchcmd --lang=ja
run_case cli-unknown-option-en  "$CM" check --nope
run_case cli-unknown-option-ja  "$CM" check --nope --lang=ja
run_case cli-invalid-lang       "$CM" check --lang=xx "$DIR/ok.cm"
run_case cli-missing-input      "$CM" compile

# ---------- help（言語別・プレースホルダ置換） ----------
run_case help-en "$CM" help
run_case help-ja "$CM" help --lang=ja

# ---------- checkサマリー（言語別） ----------
run_case check-summary-en "$CM" check "$DIR/bad_var.cm"
run_case check-summary-ja "$CM" check --lang=ja "$DIR/bad_var.cm"

# ---------- 命名規則（L001・プレースホルダ埋め込み） ----------
run_case naming-l001-en "$CM" check --strict "$DIR/bad_naming.cm"
run_case naming-l001-ja "$CM" check --strict --lang=ja "$DIR/bad_naming.cm"

# ---------- const集約への代入警告（M3・checkモード限定の段階導入） ----------
run_case const-aggregate-en "$CM" check "$DIR/const_aggregate_assign.cm"
run_case const-aggregate-ja "$CM" check --lang=ja "$DIR/const_aggregate_assign.cm"

# ---------- const値への非constポインタ取得警告（M3段階3） ----------
run_case const-addr-of-en "$CM" check "$DIR/const_addr_of.cm"
run_case const-addr-of-ja "$CM" check --lang=ja "$DIR/const_addr_of.cm"

# ---------- 非exportヘルパーの非修飾公開の抑止（H7段階4） ----------
run_case non-export-helper-hidden "$CM" check "$DIR/non_export_helper/main.cm"
run_case non-export-helper-ok     "$CM" check "$DIR/non_export_helper/main_ok.cm"

# ---------- 確定代入・return網羅の--strictエラー昇格（H6段階3） ----------
run_case h6-check-warn-en   "$CM" check "$DIR/h6_strict_promotion.cm"
run_case h6-strict-error-en "$CM" check --strict "$DIR/h6_strict_promotion.cm"
run_case h6-strict-error-ja "$CM" check --strict --lang=ja "$DIR/h6_strict_promotion.cm"

# ---------- 同名シンボルの多重import診断（M2） ----------
run_case dup-import-en "$CM" check "$DIR/dup_import/main.cm"
run_case dup-import-ja "$CM" check --lang=ja "$DIR/dup_import/main.cm"

# ---------- 非export関数の選択importエラー（構造化importで警告から昇格） ----------
run_case non-export-import-en "$CM" check "$DIR/non_export_import/main.cm"
run_case non-export-import-ja "$CM" check --lang=ja "$DIR/non_export_import/main.cm"

# ---------- no_stdチェッカー（B001・複数プレースホルダ） ----------
run_case nostd-exit-en "$CM" compile --target=bm "$DIR/bad_nostd.cm" -o /dev/null
run_case nostd-exit-ja "$CM" compile --target=bm "$DIR/bad_nostd.cm" -o /dev/null --lang=ja

# ---------- 言語の決定順序 ----------
run_case lang-env-ja       env CM_LANG=ja "$CM" nosuchcmd
run_case lang-cli-over-env env CM_LANG=ja "$CM" nosuchcmd --lang=en

# .cmconfig.yml の language: ja（一時ディレクトリで検証）
TMPDIR_I18N=$(mktemp -d)
cp "$DIR/bad_var.cm" "$TMPDIR_I18N/"
printf 'language: ja\n' > "$TMPDIR_I18N/.cmconfig.yml"
CM_ABS=$(cd "$(dirname "$CM")" && pwd)/$(basename "$CM")
run_case lang-config-ja       env -C "$TMPDIR_I18N" "$CM_ABS" check bad_var.cm
run_case lang-cli-over-config env -C "$TMPDIR_I18N" "$CM_ABS" check bad_var.cm --lang=en
rm -rf "$TMPDIR_I18N"

echo ""
echo "i18n E2E: PASS=$PASS FAIL=$FAIL"
[ $FAIL -eq 0 ]
