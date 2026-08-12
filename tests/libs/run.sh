#!/bin/bash
# ============================================================
# libs 単体テストランナー
# ============================================================
# libs/ 以下の *_test.cm（#[test] 属性付き関数）を discover して `cm test` で実行する。
# これはコンパイラのテストではなくライブラリ自体のテストなので、テストはライブラリと同じ
# ディレクトリ（libs/**/​*_test.cm）に置き、このランナーだけを tests/ 側に置く。
# ============================================================
set -u
cd "$(dirname "$0")/../.."

CM="${CM_EXECUTABLE:-./cm}"
FILES_PASS=0
FILES_FAIL=0

if [ ! -x "$CM" ]; then
    echo "error: compiler not found at $CM (run: make build-compiler)" >&2
    exit 1
fi

echo "=== libs import gate (cm check) ==="

# ============================================================
# libs 全モジュールのimportゲート（R9）
# 各 mod.cm の module ヘッダから import 文を生成して cm check し、
# 出荷されているstdlibが自身のエラーで壊れていないことを常時検証する
# （std::iterのコンパイル不能・native::mathのfloat接尾辞等が素通りしていた再発防止）
# ============================================================
GATE_PASS=0
GATE_FAIL=0
GATE_TMP=$(mktemp -d)
trap 'rm -rf "$GATE_TMP"' EXIT

# 既知の失敗リスト（言語機能不足でモジュールが未対応構文を使っている場合にここへ列挙する。現在は空 = 全モジュール検証。R22で3件を解消済み）
KNOWN_BROKEN=""

while IFS= read -r mod_file; do
    rel="${mod_file#./}"
    case " $KNOWN_BROKEN " in
        *" $rel "*)
            echo "[SKIP] import gate: $rel (known broken: R22 export-on-impl)"
            continue
            ;;
    esac
    mod_name=$(grep -m1 "^module " "$mod_file" | sed 's/^module //; s/;.*//' | tr -d ' ' | sed 's/\./::/g')
    if [ -z "$mod_name" ]; then
        echo "[FAIL] import gate: $rel (module header not found)"
        GATE_FAIL=$((GATE_FAIL + 1))
        continue
    fi
    printf 'import %s::*;\nint main() { return 0; }\n' "$mod_name" > "$GATE_TMP/gate.cm"
    if out=$("$CM" check "$GATE_TMP/gate.cm" 2>&1); then
        GATE_PASS=$((GATE_PASS + 1))
    else
        echo "[FAIL] import gate: $rel (import $mod_name::*)"
        echo "$out" | sed 's/^/    /' | head -8
        GATE_FAIL=$((GATE_FAIL + 1))
    fi
done < <(find libs -name 'mod.cm' | sort)

echo "libs import gate: PASS=$GATE_PASS FAIL=$GATE_FAIL"

echo ""
echo "=== libs unit tests (cm test) ==="

# libs 配下の *_test.cm を再帰的に探して実行する
while IFS= read -r test_file; do
    rel="${test_file#./}"
    out=$("$CM" test "$test_file" 2>&1)
    rc=$?
    if [ $rc -eq 0 ] && echo "$out" | grep -q "test(s) passed"; then
        count=$(echo "$out" | grep -oE '[0-9]+ test\(s\) passed' | grep -oE '^[0-9]+')
        echo "[PASS] $rel (${count:-?} tests)"
        FILES_PASS=$((FILES_PASS + 1))
    else
        echo "[FAIL] $rel (exit=$rc)"
        echo "$out" | sed 's/^/    /' | head -20
        FILES_FAIL=$((FILES_FAIL + 1))
    fi
done < <(find libs -name '*_test.cm' | sort)

echo ""
echo "libs tests: files PASS=$FILES_PASS FAIL=$FILES_FAIL (import gate FAIL=$GATE_FAIL)"
[ "$FILES_FAIL" -eq 0 ] && [ "$GATE_FAIL" -eq 0 ]
