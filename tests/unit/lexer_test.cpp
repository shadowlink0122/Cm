#include "../../src/frontend/lexer/lexer.hpp"

#include <gtest/gtest.h>

using namespace cm;

class LexerTest : public ::testing::Test {
   protected:
    std::vector<Token> tokenize(const std::string& source) {
        Lexer lexer(source);
        return lexer.tokenize();
    }
};

// 基本的なトークン化
TEST_F(LexerTest, EmptySource) {
    auto tokens = tokenize("");
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0].kind, TokenKind::Eof);
}

TEST_F(LexerTest, Identifier) {
    auto tokens = tokenize("foo bar _baz");
    ASSERT_EQ(tokens.size(), 4);  // foo, bar, _baz, Eof
    EXPECT_EQ(tokens[0].kind, TokenKind::Ident);
    EXPECT_EQ(tokens[0].get_string(), "foo");
    EXPECT_EQ(tokens[1].kind, TokenKind::Ident);
    EXPECT_EQ(tokens[1].get_string(), "bar");
    EXPECT_EQ(tokens[2].kind, TokenKind::Ident);
    EXPECT_EQ(tokens[2].get_string(), "_baz");
}

// キーワード
TEST_F(LexerTest, Keywords) {
    auto tokens = tokenize("if else while for return struct with");
    EXPECT_EQ(tokens[0].kind, TokenKind::KwIf);
    EXPECT_EQ(tokens[1].kind, TokenKind::KwElse);
    EXPECT_EQ(tokens[2].kind, TokenKind::KwWhile);
    EXPECT_EQ(tokens[3].kind, TokenKind::KwFor);
    EXPECT_EQ(tokens[4].kind, TokenKind::KwReturn);
    EXPECT_EQ(tokens[5].kind, TokenKind::KwStruct);
    EXPECT_EQ(tokens[6].kind, TokenKind::KwWith);
}

// 型キーワード
TEST_F(LexerTest, TypeKeywords) {
    auto tokens = tokenize("int uint float double bool char string void");
    EXPECT_EQ(tokens[0].kind, TokenKind::KwInt);
    EXPECT_EQ(tokens[1].kind, TokenKind::KwUint);
    EXPECT_EQ(tokens[2].kind, TokenKind::KwFloat);
    EXPECT_EQ(tokens[3].kind, TokenKind::KwDouble);
    EXPECT_EQ(tokens[4].kind, TokenKind::KwBool);
    EXPECT_EQ(tokens[5].kind, TokenKind::KwChar);
    EXPECT_EQ(tokens[6].kind, TokenKind::KwString);
    EXPECT_EQ(tokens[7].kind, TokenKind::KwVoid);
}

// 整数リテラル
TEST_F(LexerTest, IntegerLiterals) {
    auto tokens = tokenize("123 0 42");
    EXPECT_EQ(tokens[0].kind, TokenKind::IntLiteral);
    EXPECT_EQ(tokens[0].get_int(), 123);
    EXPECT_EQ(tokens[1].kind, TokenKind::IntLiteral);
    EXPECT_EQ(tokens[1].get_int(), 0);
    EXPECT_EQ(tokens[2].kind, TokenKind::IntLiteral);
    EXPECT_EQ(tokens[2].get_int(), 42);
}

// 16進数・2進数
TEST_F(LexerTest, HexAndBinaryLiterals) {
    auto tokens = tokenize("0xFF 0b1010");
    EXPECT_EQ(tokens[0].kind, TokenKind::IntLiteral);
    EXPECT_EQ(tokens[0].get_int(), 255);
    EXPECT_EQ(tokens[1].kind, TokenKind::IntLiteral);
    EXPECT_EQ(tokens[1].get_int(), 10);
}

// 浮動小数点リテラル
TEST_F(LexerTest, FloatLiterals) {
    auto tokens = tokenize("3.14 0.5 1e10 2.5e-3");
    EXPECT_EQ(tokens[0].kind, TokenKind::FloatLiteral);
    EXPECT_DOUBLE_EQ(tokens[0].get_float(), 3.14);
    EXPECT_EQ(tokens[1].kind, TokenKind::FloatLiteral);
    EXPECT_DOUBLE_EQ(tokens[1].get_float(), 0.5);
    EXPECT_EQ(tokens[2].kind, TokenKind::FloatLiteral);
    EXPECT_DOUBLE_EQ(tokens[2].get_float(), 1e10);
    EXPECT_EQ(tokens[3].kind, TokenKind::FloatLiteral);
    EXPECT_DOUBLE_EQ(tokens[3].get_float(), 2.5e-3);
}

// 文字列リテラル
TEST_F(LexerTest, StringLiterals) {
    auto tokens = tokenize(R"("hello" "world" "foo\nbar")");
    EXPECT_EQ(tokens[0].kind, TokenKind::StringLiteral);
    EXPECT_EQ(tokens[0].get_string(), "hello");
    EXPECT_EQ(tokens[1].kind, TokenKind::StringLiteral);
    EXPECT_EQ(tokens[1].get_string(), "world");
    EXPECT_EQ(tokens[2].kind, TokenKind::StringLiteral);
    EXPECT_EQ(tokens[2].get_string(), "foo\nbar");
}

// 演算子
TEST_F(LexerTest, Operators) {
    auto tokens = tokenize("+ - * / % = == != < > <= >= && || !");
    EXPECT_EQ(tokens[0].kind, TokenKind::Plus);
    EXPECT_EQ(tokens[1].kind, TokenKind::Minus);
    EXPECT_EQ(tokens[2].kind, TokenKind::Star);
    EXPECT_EQ(tokens[3].kind, TokenKind::Slash);
    EXPECT_EQ(tokens[4].kind, TokenKind::Percent);
    EXPECT_EQ(tokens[5].kind, TokenKind::Eq);
    EXPECT_EQ(tokens[6].kind, TokenKind::EqEq);
    EXPECT_EQ(tokens[7].kind, TokenKind::BangEq);
    EXPECT_EQ(tokens[8].kind, TokenKind::Lt);
    EXPECT_EQ(tokens[9].kind, TokenKind::Gt);
    EXPECT_EQ(tokens[10].kind, TokenKind::LtEq);
    EXPECT_EQ(tokens[11].kind, TokenKind::GtEq);
    EXPECT_EQ(tokens[12].kind, TokenKind::AmpAmp);
    EXPECT_EQ(tokens[13].kind, TokenKind::PipePipe);
    EXPECT_EQ(tokens[14].kind, TokenKind::Bang);
}

// 区切り
TEST_F(LexerTest, Delimiters) {
    auto tokens = tokenize("( ) { } [ ] , ; . ::");
    EXPECT_EQ(tokens[0].kind, TokenKind::LParen);
    EXPECT_EQ(tokens[1].kind, TokenKind::RParen);
    EXPECT_EQ(tokens[2].kind, TokenKind::LBrace);
    EXPECT_EQ(tokens[3].kind, TokenKind::RBrace);
    EXPECT_EQ(tokens[4].kind, TokenKind::LBracket);
    EXPECT_EQ(tokens[5].kind, TokenKind::RBracket);
    EXPECT_EQ(tokens[6].kind, TokenKind::Comma);
    EXPECT_EQ(tokens[7].kind, TokenKind::Semicolon);
    EXPECT_EQ(tokens[8].kind, TokenKind::Dot);
    EXPECT_EQ(tokens[9].kind, TokenKind::ColonColon);
}

// コメント
TEST_F(LexerTest, Comments) {
    auto tokens = tokenize("foo // comment\nbar /* block */ baz");
    ASSERT_EQ(tokens.size(), 4);
    EXPECT_EQ(tokens[0].get_string(), "foo");
    EXPECT_EQ(tokens[1].get_string(), "bar");
    EXPECT_EQ(tokens[2].get_string(), "baz");
}

// 完全な関数定義
TEST_F(LexerTest, FunctionDefinition) {
    auto tokens = tokenize(R"(
int add(int a, int b) {
    return a + b;
}
)");
    // int add ( int a , int b ) { return a + b ; } Eof
    EXPECT_EQ(tokens[0].kind, TokenKind::KwInt);
    EXPECT_EQ(tokens[1].kind, TokenKind::Ident);
    EXPECT_EQ(tokens[1].get_string(), "add");
    EXPECT_EQ(tokens[2].kind, TokenKind::LParen);
}

// struct with構文
TEST_F(LexerTest, StructWith) {
    auto tokens = tokenize("struct Point with Debug { int x; int y; }");
    EXPECT_EQ(tokens[0].kind, TokenKind::KwStruct);
    EXPECT_EQ(tokens[1].kind, TokenKind::Ident);
    EXPECT_EQ(tokens[1].get_string(), "Point");
    EXPECT_EQ(tokens[2].kind, TokenKind::KwWith);
    EXPECT_EQ(tokens[3].kind, TokenKind::Ident);
    EXPECT_EQ(tokens[3].get_string(), "Debug");
}
