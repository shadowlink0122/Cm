#include "../../src/internal/syntax/lexer/lexer.hpp"
#include "../../src/internal/syntax/parser/parser.hpp"
#include "../../src/internal/types/checking/checker.hpp"

#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>

using namespace cm;

// ============================================================
// 数値縮小診断の文脈網羅テスト（局所処理調査F系）
// ============================================================
// 縮小・符号変化の警告がlet/代入/returnだけでなく、関数引数・配列要素リテラル・構造体フィールド初期化でも一律に出ることの回帰。
// Cmソースは tests/regression/cases/narrowing_diag/ の .cm ファイルに分割
class NarrowingDiagTest : public ::testing::Test {
   protected:
    std::string load_case(const std::string& name) {
        std::string path = std::string(CM_NARROWING_DIAG_CASE_DIR) + "/" + name + ".cm";
        std::ifstream ifs(path);
        EXPECT_TRUE(ifs.is_open()) << "ケースファイルを読み込めません: " << path;
        std::stringstream ss;
        ss << ifs.rdbuf();
        return ss.str();
    }

    // 型検査を実行し、縮小変換警告の件数を返す
    int narrowing_warning_count(const std::string& code) {
        Lexer lex(code);
        std::vector<Token> tokens = lex.tokenize();
        Parser p(tokens);
        auto ast = p.parse();

        TypeChecker checker;
        checker.check(ast);
        int count = 0;
        for (const auto& d : checker.diagnostics()) {
            if (d.severity == Severity::Warning &&
                d.message.find("narrowing") != std::string::npos) {
                ++count;
            }
        }
        return count;
    }
};

// 関数引数への縮小変換は警告される
TEST_F(NarrowingDiagTest, FunctionArgumentNarrowingWarns) {
    EXPECT_EQ(narrowing_warning_count(load_case("arg")), 1);
}

// 配列要素リテラルへの縮小変換は警告される
TEST_F(NarrowingDiagTest, ArrayElementNarrowingWarns) {
    EXPECT_EQ(narrowing_warning_count(load_case("array_elem")), 1);
}

// 構造体フィールド初期化への縮小変換は警告される
TEST_F(NarrowingDiagTest, StructFieldNarrowingWarns) {
    EXPECT_EQ(narrowing_warning_count(load_case("struct_field")), 1);
}

// 適合リテラルはどの文脈でも警告されない
TEST_F(NarrowingDiagTest, FittingLiteralsDoNotWarn) {
    EXPECT_EQ(narrowing_warning_count(load_case("fitting_literal")), 0);
}
