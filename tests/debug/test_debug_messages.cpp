// デバッグメッセージシステムのテスト
#include "../../src/common/debug_messages.hpp"

#include <iostream>
#include <string>

void test_lexer_debug() {
    std::cout << "\n=== Lexer Debug Messages Test ===" << std::endl;

    // 基本的なデバッグメッセージ
    cm::debug::lex::log(cm::debug::lex::Id::Start);
    cm::debug::lex::log(cm::debug::lex::Id::FileOpen, "test.cm");

    // トークン検出
    cm::debug::lex::log(cm::debug::lex::Id::Keyword, "struct", cm::debug::Level::Debug);
    cm::debug::lex::log(cm::debug::lex::Id::Ident, "MyStruct", cm::debug::Level::Debug);

    // Traceレベルの詳細情報
    cm::debug::lex::dump_token("KEYWORD", "int", 1, 1);
    cm::debug::lex::dump_token("IDENT", "main", 1, 5);
    cm::debug::lex::dump_position(1, 10, "after main");

    cm::debug::lex::log(cm::debug::lex::Id::End);
}

void test_parser_debug() {
    std::cout << "\n=== Parser Debug Messages Test ===" << std::endl;

    cm::debug::par::log(cm::debug::par::Id::Start);
    cm::debug::par::log(cm::debug::par::Id::FuncDef, "main");

    // ノード作成
    cm::debug::par::dump_node("FunctionDef", "main() -> int");
    cm::debug::par::dump_scope(1, "entering function body");

    // エラー処理
    cm::debug::par::dump_expectation("';'", "'}'");
    cm::debug::par::log(cm::debug::par::Id::Recover);

    cm::debug::par::log(cm::debug::par::Id::End);
}

void test_hir_debug() {
    std::cout << "\n=== HIR Debug Messages Test ===" << std::endl;

    cm::debug::hir::log(cm::debug::hir::Id::LowerStart);
    cm::debug::hir::log(cm::debug::hir::Id::FunctionNode, "main");

    // 型処理
    cm::debug::hir::dump_type("x", "int");
    cm::debug::hir::dump_symbol("main", "global", "() -> int");

    // ノード情報
    cm::debug::hir::dump_node("BinaryOp", "x + 1");

    cm::debug::hir::log(cm::debug::hir::Id::LowerEnd);
}

void test_mir_debug() {
    std::cout << "\n=== MIR Debug Messages Test ===" << std::endl;

    cm::debug::mir::log(cm::debug::mir::Id::LowerStart);
    cm::debug::mir::log(cm::debug::mir::Id::FunctionLower, "main");

    // 基本ブロック
    cm::debug::mir::log(cm::debug::mir::Id::BasicBlockCreate, "bb0");
    cm::debug::mir::dump_block("bb0", "entry block");

    // 命令生成
    cm::debug::mir::log(cm::debug::mir::Id::InstAlloc, "%0 = alloca i32");
    cm::debug::mir::log(cm::debug::mir::Id::InstStore, "store i32 42, %0");
    cm::debug::mir::log(cm::debug::mir::Id::InstLoad, "%1 = load %0");

    // 値のダンプ
    cm::debug::mir::dump_value("%0", "alloca i32");
    cm::debug::mir::dump_value("%1", "42");

    // SSA形式
    cm::debug::mir::log(cm::debug::mir::Id::SSAConstruct);
    cm::debug::mir::log(cm::debug::mir::Id::PhiNodeInsert, "phi [%1, bb0], [%2, bb1]");

    cm::debug::mir::log(cm::debug::mir::Id::LowerEnd);
}

void test_codegen_debug() {
    std::cout << "\n=== CodeGen Debug Messages Test ===" << std::endl;

    // Rust生成
    cm::debug::codegen::log(cm::debug::codegen::Id::RustStart);
    cm::debug::codegen::log(cm::debug::codegen::Id::RustFunction, "main");
    cm::debug::codegen::dump_code("function", "fn main() -> i32 { 42 }");
    cm::debug::codegen::log(cm::debug::codegen::Id::RustEnd);

    // TypeScript生成
    cm::debug::codegen::log(cm::debug::codegen::Id::TsStart);
    cm::debug::codegen::log(cm::debug::codegen::Id::TsFunction, "main");
    cm::debug::codegen::dump_code("function", "function main(): number { return 42; }");
    cm::debug::codegen::log(cm::debug::codegen::Id::TsEnd);

    // C++生成
    cm::debug::codegen::log(cm::debug::codegen::Id::CppStart);
    cm::debug::codegen::log(cm::debug::codegen::Id::CppFunction, "main");
    cm::debug::codegen::dump_code("function", "int main() { return 42; }");
    cm::debug::codegen::log(cm::debug::codegen::Id::CppEnd);
}

void test_interpreter_debug() {
    std::cout << "\n=== Interpreter Debug Messages Test ===" << std::endl;

    cm::debug::interp::log(cm::debug::interp::Id::Start);

    // 関数呼び出し
    cm::debug::interp::log(cm::debug::interp::Id::CallFunc, "main");
    cm::debug::interp::dump_frame(0, "main", 2);

    // 変数操作
    cm::debug::interp::log(cm::debug::interp::Id::VarDeclare, "x: int");
    cm::debug::interp::dump_value("x", "42", "int");

    // MIR命令実行
    cm::debug::interp::dump_instruction("alloca", "i32");
    cm::debug::interp::dump_instruction("store", "i32 42, %0");
    cm::debug::interp::dump_instruction("load", "%0 -> %1");

    // メモリ操作
    cm::debug::interp::dump_memory((void*)0x1000, 4, "variable x");

    // 式評価
    cm::debug::interp::log(cm::debug::interp::Id::EvalBinary, "x + 1");
    cm::debug::interp::dump_value("result", "43", "int");

    cm::debug::interp::log(cm::debug::interp::Id::Return, "43");
    cm::debug::interp::log(cm::debug::interp::Id::End);
}

void test_multilevel_debug() {
    std::cout << "\n=== Multi-level Debug Test ===" << std::endl;

    // 異なるデバッグレベルでのテスト
    std::cout << "Testing with DEBUG level:" << std::endl;
    cm::debug::set_level(cm::debug::Level::Debug);
    cm::debug::lex::log(cm::debug::lex::Id::Start, "", cm::debug::Level::Debug);
    cm::debug::lex::log(cm::debug::lex::Id::TokenFound, "int",
                        cm::debug::Level::Trace);  // 表示されない

    std::cout << "Testing with TRACE level:" << std::endl;
    cm::debug::set_level(cm::debug::Level::Trace);
    cm::debug::lex::log(cm::debug::lex::Id::TokenFound, "int",
                        cm::debug::Level::Trace);        // 表示される
    cm::debug::lex::dump_token("KEYWORD", "int", 1, 1);  // Traceレベルで表示
}

void test_japanese_messages() {
    std::cout << "\n=== Japanese Messages Test ===" << std::endl;

    // 日本語モードに設定
    cm::debug::set_lang(1);

    cm::debug::lex::log(cm::debug::lex::Id::Start);
    cm::debug::par::log(cm::debug::par::Id::FuncDef, "main");
    cm::debug::hir::log(cm::debug::hir::Id::TypeResolve, "int");
    cm::debug::mir::log(cm::debug::mir::Id::OptimizeStart);
    cm::debug::codegen::log(cm::debug::codegen::Id::CppStart);
    cm::debug::interp::log(cm::debug::interp::Id::CallFunc, "main");

    // 英語モードに戻す
    cm::debug::set_lang(0);
}

int main() {
    std::cout << "=== Cm Compiler Debug Messages Test ===" << std::endl;

    // デバッグモードを有効化
    cm::debug::set_debug_mode(true);
    cm::debug::set_level(cm::debug::Level::Debug);

    // 各ステージのテスト
    test_lexer_debug();
    test_parser_debug();
    test_hir_debug();
    test_mir_debug();
    test_codegen_debug();
    test_interpreter_debug();

    // 特殊なテスト
    test_multilevel_debug();
    test_japanese_messages();

    std::cout << "\n=== Test Complete ===" << std::endl;
    return 0;
}