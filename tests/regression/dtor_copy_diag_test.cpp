#include "../../src/internal/syntax/lexer/lexer.hpp"
#include "../../src/internal/syntax/parser/parser.hpp"
#include "../../src/internal/types/checking/checker.hpp"

#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>

using namespace cm;

// ============================================================
// dtor持ち構造体の暗黙コピー診断のテスト（二重解放ハザードの言語側対策）
// ============================================================
// moveなしの場所式コピーがlet初期化・代入・return・構造体リテラルフィールドで警告され、
// move使用・一時値・dtor無し構造体では警告されないことの回帰。
// Cmソースは tests/regression/cases/dtor_copy_diag/ の .cm ファイルに分割
class DtorCopyDiagTest : public ::testing::Test {
   protected:
    std::string load_case(const std::string& name) {
        std::string path = std::string(CM_DTOR_COPY_DIAG_CASE_DIR) + "/" + name + ".cm";
        std::ifstream ifs(path);
        EXPECT_TRUE(ifs.is_open()) << "ケースファイルを読み込めません: " << path;
        std::stringstream ss;
        ss << ifs.rdbuf();
        return ss.str();
    }

    // 型検査を実行し、dtor持ち構造体の暗黙コピー警告の件数を返す
    int dtor_copy_warning_count(const std::string& code) {
        Lexer lex(code);
        std::vector<Token> tokens = lex.tokenize();
        Parser p(tokens);
        auto ast = p.parse();

        TypeChecker checker;
        checker.check(ast);
        int count = 0;
        for (const auto& d : checker.diagnostics()) {
            if (d.severity == Severity::Warning &&
                d.message.find("destructor") != std::string::npos) {
                ++count;
            }
        }
        return count;
    }
};

// let初期化のコピー（moveなし）は警告される
TEST_F(DtorCopyDiagTest, LetInitCopyWarns) {
    EXPECT_EQ(dtor_copy_warning_count(load_case("let_copy")), 1);
}

// フィールドへの代入コピー（moveなし）は警告される（HashSet二重解放と同型のパターン）
TEST_F(DtorCopyDiagTest, FieldAssignCopyWarns) {
    EXPECT_EQ(dtor_copy_warning_count(load_case("assign_copy")), 1);
}

// ローカルの値返し（moveなし）は警告される（return moveを促す）
TEST_F(DtorCopyDiagTest, ReturnCopyWarns) {
    EXPECT_EQ(dtor_copy_warning_count(load_case("return_copy")), 1);
}

// 構造体リテラルのフィールド初期化コピーは警告される
TEST_F(DtorCopyDiagTest, StructLiteralFieldCopyWarns) {
    EXPECT_EQ(dtor_copy_warning_count(load_case("field_init_copy")), 1);
}

// move付きの所有権移動（let初期化・代入・return move）は警告されない
TEST_F(DtorCopyDiagTest, MoveDoesNotWarn) {
    EXPECT_EQ(dtor_copy_warning_count(load_case("move_ok")), 0);
}

// dtorを持たない構造体のコピーは警告されない
TEST_F(DtorCopyDiagTest, NoDtorStructDoesNotWarn) {
    EXPECT_EQ(dtor_copy_warning_count(load_case("no_dtor_ok")), 0);
}
