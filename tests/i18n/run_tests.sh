#!/bin/bash
# ============================================================
# エラーメッセージ・i18nのE2Eテスト
# ============================================================
# cmバイナリの実際の出力メッセージを英語（デフォルト）と日本語の両方で検証する。
# 言語の決定順序（--lang > CM_LANG > .cmconfig.yml language:）も確認する。
# ============================================================
set -u
cd "$(dirname "$0")/../.."

CM="${CM_EXECUTABLE:-./cm}"
DIR=tests/i18n
PASS=0
FAIL=0

# check <名前> <出力に含むべき文字列> <コマンド...>
check() {
    local name="$1" needle="$2"
    shift 2
    local out
    out=$("$@" 2>&1)
    if echo "$out" | grep -qF "$needle"; then
        echo "[PASS] $name"
        PASS=$((PASS + 1))
    else
        echo "[FAIL] $name (expected: $needle)"
        echo "$out" | sed 's/^/    /' | head -6
        FAIL=$((FAIL + 1))
    fi
}

# check_absent <名前> <出力に含まれてはならない文字列> <コマンド...>
check_absent() {
    local name="$1" needle="$2"
    shift 2
    local out
    out=$("$@" 2>&1)
    if echo "$out" | grep -qF "$needle"; then
        echo "[FAIL] $name (unexpected: $needle)"
        echo "$out" | sed 's/^/    /' | head -6
        FAIL=$((FAIL + 1))
    else
        echo "[PASS] $name"
        PASS=$((PASS + 1))
    fi
}

# ---------- CLIメッセージ（コマンドエラー） ----------
check "en: unknown command"        "error: invalid command form"          "$CM" nosuchcmd
check "ja: unknown command (flag)" "エラー: 不正なコマンド形式です"        "$CM" nosuchcmd --lang=ja
check "en: unknown option"         "unknown option: --nope"               "$CM" check --nope
check "ja: unknown option"         "不明なオプション: --nope"              "$CM" check --nope --lang=ja
check "en: invalid lang value"     "invalid --lang value"                 "$CM" check --lang=xx "$DIR/ok.cm"
check "en: missing input"          "error: no input file"                 "$CM" compile

# ---------- help（言語別・プレースホルダ置換） ----------
check "en: help header"            "Cm language compiler v"               "$CM" help
check "ja: help header"            "Cm言語コンパイラ v"                    "$CM" help --lang=ja
check_absent "help: version placeholder resolved" "{version}"            "$CM" help
check_absent "help: program placeholder resolved" "{program}"            "$CM" help

# ---------- checkサマリー（言語別） ----------
check "en: check summary"          "=== Check complete ==="               "$CM" check "$DIR/bad_var.cm"
check "en: check errors label"     "errors: 1"                            "$CM" check "$DIR/bad_var.cm"
check "ja: check summary"          "=== チェック完了 ==="                  "$CM" check --lang=ja "$DIR/bad_var.cm"
check "ja: check errors label"     "エラー: 1"                             "$CM" check --lang=ja "$DIR/bad_var.cm"

# ---------- 命名規則（L001・プレースホルダ埋め込み） ----------
check "en: naming violation"       "variable name 'badCamelName' does not follow the snake_case naming convention [L001]" \
    "$CM" check --strict "$DIR/bad_naming.cm"
check "ja: naming violation"       "変数名 'badCamelName' は snake_case 命名規則に従っていません [L001]" \
    "$CM" check --strict --lang=ja "$DIR/bad_naming.cm"

# ---------- const集約への代入警告（M3・checkモード限定の段階導入） ----------
check "en: const aggregate field warn" "assignment to a field or element of const value 'p'" \
    "$CM" check "$DIR/const_aggregate_assign.cm"
check "en: const aggregate elem warn"  "assignment to a field or element of const value 'a'" \
    "$CM" check "$DIR/const_aggregate_assign.cm"
check "ja: const aggregate field warn" "const値 'p' のフィールド/要素へ代入しています" \
    "$CM" check --lang=ja "$DIR/const_aggregate_assign.cm"
check_absent "const aggregate warn: non-const is silent" "const value 'q'" \
    "$CM" check "$DIR/const_aggregate_assign.cm"

# ---------- no_stdチェッカー（B001・複数プレースホルダ） ----------
check "en: nostd exit"             "error: function 'main' uses 'exit'; process control is not available in bare-metal environments" \
    "$CM" compile --target=bm "$DIR/bad_nostd.cm" -o /dev/null
check "ja: nostd exit"             "エラー: 関数 'main' 内で 'exit' を使用しています。プロセス制御 はベアメタル環境では使用できません" \
    "$CM" compile --target=bm "$DIR/bad_nostd.cm" -o /dev/null --lang=ja

# ---------- 言語の決定順序 ----------
check "env: CM_LANG=ja"            "エラー: 不正なコマンド形式です"        env CM_LANG=ja "$CM" nosuchcmd
check "cli wins over env"          "error: invalid command form"          env CM_LANG=ja "$CM" nosuchcmd --lang=en

# .cmconfig.yml の language: ja（一時ディレクトリで検証）
TMPDIR_I18N=$(mktemp -d)
cp "$DIR/bad_var.cm" "$TMPDIR_I18N/"
printf 'language: ja\n' > "$TMPDIR_I18N/.cmconfig.yml"
CM_ABS=$(cd "$(dirname "$CM")" && pwd)/$(basename "$CM")
check "config: language ja"        "=== チェック完了 ===" \
    env -C "$TMPDIR_I18N" "$CM_ABS" check bad_var.cm
check "cli wins over config"       "=== Check complete ===" \
    env -C "$TMPDIR_I18N" "$CM_ABS" check bad_var.cm --lang=en
rm -rf "$TMPDIR_I18N"

echo ""
echo "i18n E2E: PASS=$PASS FAIL=$FAIL"
[ $FAIL -eq 0 ]
