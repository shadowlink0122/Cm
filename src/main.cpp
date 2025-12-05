#include "common/debug_messages.hpp"
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser.hpp"
#include "frontend/types/type_checker.hpp"
#include "hir/hir_lowering.hpp"

#include <iostream>

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-d" || arg == "--debug") {
            cm::debug::set_debug_mode(true);
        } else if (arg.substr(0, 3) == "-d=") {
            cm::debug::set_debug_mode(true);
            cm::debug::set_level(cm::debug::parse_level(arg.substr(3)));
        } else if (arg == "--ja") {
            cm::debug::set_lang(1);
        }
    }

    // テストコード
    std::string code = R"(
int add(int a, int b) {
    return a + b;
}

void main() {
    int x = 10;
    int y = add(x, 20);
    
    while (x > 0) {
        x = x - 1;
    }
}
)";

    // Lexer
    std::cout << "=== Lexer ===\n";
    cm::Lexer lexer(code);
    auto tokens = lexer.tokenize();
    std::cout << "Tokenized " << tokens.size() << " tokens\n";

    // Parser
    std::cout << "\n=== Parser ===\n";
    cm::Parser parser(std::move(tokens));
    auto program = parser.parse();

    if (parser.has_errors()) {
        std::cerr << "Parse errors!\n";
        return 1;
    }
    std::cout << "Parsed " << program.declarations.size() << " declarations\n";

    // Type Checker
    std::cout << "\n=== Type Checker ===\n";
    cm::TypeChecker checker;
    if (!checker.check(program)) {
        std::cerr << "Type errors!\n";
        return 1;
    }
    std::cout << "Type check passed!\n";

    // HIR Lowering
    std::cout << "\n=== HIR Lowering ===\n";
    cm::hir::HirLowering lowering;
    auto hir = lowering.lower(program);
    std::cout << "Lowered to " << hir.declarations.size() << " HIR declarations\n";

    // HIRダンプ
    std::cout << "\n=== HIR Dump ===\n";
    for (const auto& decl : hir.declarations) {
        if (auto* func = std::get_if<std::unique_ptr<cm::hir::HirFunction>>(&decl->kind)) {
            std::cout << "  fn " << (*func)->name << "(";
            for (size_t i = 0; i < (*func)->params.size(); ++i) {
                if (i > 0)
                    std::cout << ", ";
                std::cout << (*func)->params[i].name << ": "
                          << cm::hir::type_to_string(*(*func)->params[i].type);
            }
            std::cout << ") -> " << cm::hir::type_to_string(*(*func)->return_type);
            std::cout << " { " << (*func)->body.size() << " stmts }\n";
        } else if (auto* st = std::get_if<std::unique_ptr<cm::hir::HirStruct>>(&decl->kind)) {
            std::cout << "  struct " << (*st)->name << " { " << (*st)->fields.size()
                      << " fields }\n";
        }
    }

    std::cout << "\nCompilation pipeline complete!\n";
    return 0;
}
