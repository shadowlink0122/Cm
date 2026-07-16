#include "../../src/fmt/formatter.hpp"

#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

using namespace cm::fmt;

// ============================================================
// フォーマッタ統合テスト
// ============================================================
// Cmソースは tests/regression/cases/formatter/ の .cm ファイルに分割。
// - <name>.input.cm + <name>.expected.cm: 整形結果がexpectedに一致すること
// - <name>.cm: 既に整形済みで、fmtを適用しても変化しないこと（安定ケース）
// いずれも冪等性（expected/安定ファイルへの再適用で変化なし）を検証する。
class FormatterIntegrationTest : public ::testing::Test {
   protected:
    std::string load_case(const std::string& filename) {
        std::string path = std::string(CM_FORMATTER_CASE_DIR) + "/" + filename;
        std::ifstream ifs(path);
        EXPECT_TRUE(ifs.is_open()) << "ケースファイルを読み込めません: " << path;
        std::stringstream ss;
        ss << ifs.rdbuf();
        return ss.str();
    }

    std::string format(const std::string& source) {
        Formatter formatter;
        return formatter.format(source).formatted_code;
    }

    // input → expected の整形と、expected の冪等性を検証
    void expect_format_case(const std::string& name) {
        std::string input = load_case(name + ".input.cm");
        std::string expected = load_case(name + ".expected.cm");
        EXPECT_EQ(format(input), expected) << "ケース: " << name;
        EXPECT_EQ(format(expected), expected) << "冪等性違反: " << name;
    }

    // 整形済みファイルが変化しないことを検証
    void expect_stable_case(const std::string& name) {
        std::string source = load_case(name + ".cm");
        EXPECT_EQ(format(source), source) << "安定ケース: " << name;
    }
};

// ============================================================
// 最大行幅（100桁）を超える宣言・式の折り返し
// ============================================================

// 長い配列リテラルはカンマ位置で折り返す（継続行は1段深いインデント）
TEST_F(FormatterIntegrationTest, WrapLongArrayLiteral) {
    expect_format_case("wrap/long_array");
}

// 長い式は二項演算子の直前で折り返す（継続行が演算子で始まる既存スタイルに一致）
TEST_F(FormatterIntegrationTest, WrapLongExpression) {
    expect_format_case("wrap/long_expr");
}

// コードが短く行末コメントだけで幅超過する行は折り返さない
TEST_F(FormatterIntegrationTest, WrapSkipsCommentOverflow) {
    expect_stable_case("wrap/comment_overflow");
}

// 文字列リテラル内に折り返し候補相当の文字があっても分割しない（候補なし→無変更）
TEST_F(FormatterIntegrationTest, WrapSkipsLongStringLiteral) {
    expect_stable_case("wrap/long_string");
}

// ============================================================
// 条件付きコンパイルブロック（#ifdef〜#end）のインデント
// ============================================================

// トップレベルの #ifdef: 内容を1段インデント、ディレクティブは外側の深さ
TEST_F(FormatterIntegrationTest, IfdefTopLevelIndent) {
    expect_format_case("ifdef/top_level");
}

// ネストした #ifdef: 各レベルで1段ずつ深くなる
TEST_F(FormatterIntegrationTest, IfdefNestedIndent) {
    expect_format_case("ifdef/nested");
}

// #else は対応する #ifdef と同列に揃う（ネスト時も含む）
TEST_F(FormatterIntegrationTest, IfdefElseAlignment) {
    expect_format_case("ifdef/else_alignment");
}

// 関数内（ブレース深さ1）の #ifdef: ブレース深さと合算
TEST_F(FormatterIntegrationTest, IfdefInsideFunction) {
    expect_format_case("ifdef/inside_function");
}

// #ifdef 内に波括弧ブロックを含むケース
TEST_F(FormatterIntegrationTest, IfdefContainingBraces) {
    expect_format_case("ifdef/containing_braces");
}

// 分岐ごとに波括弧が不均衡なケース: #else で分岐開始状態に復元されるため
// ディレクティブが波括弧カウントの影響を受けず崩れない
TEST_F(FormatterIntegrationTest, IfdefBranchUnbalancedBraces) {
    expect_format_case("ifdef/branch_unbalanced");
}

// ============================================================
// 文の継続行（長い式の折り返し）のインデント
// ============================================================

// 演算子先頭（+ 等）の継続行: 1回目で+1段、2回目以降も同じ深さ
TEST_F(FormatterIntegrationTest, ContinuationOperatorLeading) {
    expect_format_case("continuation/operator");
}

// 演算子末尾（&& 等）で折り返した継続行も同様
TEST_F(FormatterIntegrationTest, ContinuationTrailingOperator) {
    expect_format_case("continuation/trailing_op");
}

// 未閉括弧を持ち越す継続行は括弧深さのインデントに従う
TEST_F(FormatterIntegrationTest, ContinuationParenCarry) {
    expect_stable_case("continuation/paren_carry");
}

// ============================================================
// 行末コメント・裸ブロック
// ============================================================

// 行末コメントの手動調整位置は保持される（2スペース以上なら変更しない）
TEST_F(FormatterIntegrationTest, TrailingCommentManualSpacing) {
    expect_stable_case("style/trailing_comment_manual");
}

// 裸ブロックの { は前の行に結合されない
TEST_F(FormatterIntegrationTest, BareBlockNotJoined) {
    expect_stable_case("style/bare_block");
}

// ============================================================
// SV幅付きリテラル（N'dVALUE等）と文字リテラルの区別
// ============================================================

// 幅付きリテラルの ' が文字リテラル開始と誤認されると
// 行内の括弧カウントが狂い、後続行のインデントが崩れる
TEST_F(FormatterIntegrationTest, SizedLiteralDoesNotBreakIndent) {
    expect_stable_case("literal/sized_indent");
}

// 2進・16進の幅付きリテラルも同様
TEST_F(FormatterIntegrationTest, SizedLiteralBinaryHex) {
    expect_stable_case("literal/sized_binary_hex");
}

// 通常の文字リテラルは引き続き認識される（内部の括弧はカウントされない）
TEST_F(FormatterIntegrationTest, CharLiteralStillRecognized) {
    expect_stable_case("literal/char_paren");
}
