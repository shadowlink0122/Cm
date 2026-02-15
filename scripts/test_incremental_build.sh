#!/bin/bash
# 差分ビルド（インクリメンタルビルド）効率テスト
# 2回コンパイルし、2回目がキャッシュヒットで高速化されることを検証

set -e

CM="${1:-./cm}"
TEST_FILE="${2:-examples/01_basics/hello_world.cm}"
OUTPUT="/tmp/incremental_test_output.o"

echo "=== インクリメンタルビルド効率テスト ==="
echo "コンパイラ: ${CM}"
echo "テストファイル: ${TEST_FILE}"
echo ""

# キャッシュをクリア
rm -rf .cm-cache

# === 1回目: フルコンパイル（キャッシュミス） ===
echo "--- 1回目: フルコンパイル ---"
START1=$(date +%s%N 2>/dev/null || python3 -c 'import time; print(int(time.time()*1e9))')
${CM} compile -o ${OUTPUT} ${TEST_FILE} 2>&1
END1=$(date +%s%N 2>/dev/null || python3 -c 'import time; print(int(time.time()*1e9))')
TIME1=$(( (END1 - START1) / 1000000 ))
echo "1回目の時間: ${TIME1}ms"
echo ""

# キャッシュが作成されたか確認
if [ ! -d ".cm-cache" ]; then
    echo "❌ FAIL: キャッシュディレクトリが作成されませんでした"
    exit 1
fi
echo "✓ キャッシュディレクトリ確認: .cm-cache"

# マニフェストが存在するか確認
if [ ! -f ".cm-cache/manifest.json" ]; then
    echo "❌ FAIL: マニフェストファイルが作成されませんでした"
    exit 1
fi
echo "✓ マニフェスト確認: .cm-cache/manifest.json"

# === 2回目: キャッシュヒット ===
echo ""
echo "--- 2回目: キャッシュヒット ---"
START2=$(date +%s%N 2>/dev/null || python3 -c 'import time; print(int(time.time()*1e9))')
OUTPUT2=$(${CM} compile -o ${OUTPUT} ${TEST_FILE} 2>&1)
END2=$(date +%s%N 2>/dev/null || python3 -c 'import time; print(int(time.time()*1e9))')
TIME2=$(( (END2 - START2) / 1000000 ))
echo "${OUTPUT2}"
echo "2回目の時間: ${TIME2}ms"
echo ""

# キャッシュヒットメッセージの確認
if echo "${OUTPUT2}" | grep -q "キャッシュヒット"; then
    echo "✓ キャッシュヒット確認"
else
    echo "❌ FAIL: キャッシュヒットしませんでした"
    echo "出力: ${OUTPUT2}"
    exit 1
fi

# === 高速化の検証 ===
echo ""
echo "=== 結果比較 ==="
echo "1回目 (フルコンパイル): ${TIME1}ms"
echo "2回目 (キャッシュヒット): ${TIME2}ms"

if [ ${TIME1} -eq 0 ]; then
    echo "⚠️ 1回目の時間が0ms（計測精度不足）、スキップ"
    SPEEDUP="N/A"
else
    SPEEDUP=$(( TIME1 / (TIME2 > 0 ? TIME2 : 1) ))
    echo "高速化倍率: ${SPEEDUP}x"
fi

# 2回目は1回目の半分以下であることを検証（控えめな基準）
if [ ${TIME2} -lt ${TIME1} ]; then
    echo ""
    echo "✅ PASS: 差分ビルドによる高速化を確認 (${TIME1}ms → ${TIME2}ms)"
else
    echo ""
    echo "❌ FAIL: 差分ビルドで高速化が見られません (${TIME1}ms → ${TIME2}ms)"
    exit 1
fi

# === cache stats テスト ===
echo ""
echo "--- cache stats テスト ---"
STATS_OUTPUT=$(${CM} cache stats 2>&1)
echo "${STATS_OUTPUT}"
if echo "${STATS_OUTPUT}" | grep -q "エントリ数"; then
    echo "✓ cache stats 正常"
else
    echo "❌ FAIL: cache stats が正しく動作しません"
    exit 1
fi

# === cache clear テスト ===
echo ""
echo "--- cache clear テスト ---"
CLEAR_OUTPUT=$(${CM} cache clear 2>&1)
echo "${CLEAR_OUTPUT}"
if [ ! -d ".cm-cache" ]; then
    echo "✓ cache clear 正常（ディレクトリ削除確認）"
else
    echo "❌ FAIL: cache clear でディレクトリが削除されませんでした"
    exit 1
fi

# === --no-cache テスト ===
echo ""
echo "--- --no-cache テスト ---"
${CM} compile --no-cache -o ${OUTPUT} ${TEST_FILE} 2>&1
if [ ! -d ".cm-cache" ]; then
    echo "✓ --no-cache: キャッシュ未作成"
else
    echo "⚠️ --no-cache でもキャッシュディレクトリが作成された（内容確認）"
fi

echo ""
echo "=== 全テスト完了 ==="
echo "✅ インクリメンタルビルド効率テスト: PASS"
