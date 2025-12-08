#pragma once

#include "cpp_mir_converter.hpp"
#include "cpp_mir_codegen.hpp"
#include "../hir/hir_nodes.hpp"
#include "../common/debug.hpp"
#include <string>
#include <iostream>

namespace cm {
namespace codegen {

/**
 * C++コード生成器 v2
 *
 * HIR → CPP-MIR → C++ のパイプラインを実装
 * ステートマシンを使わず、効率的なC++コードを生成
 */
class CppCodeGeneratorV2 {
public:
    // HIRプログラムからC++コードを生成
    std::string generate(const hir::HirProgram& hir_program, bool verbose = false) {
        if (verbose) {
            std::cerr << "[CPP-CODEGEN-V2] Starting code generation\n";
        }

        // Step 1: HIR → CPP-MIR
        cpp_mir::HirToCppMirConverter mir_converter;
        auto cpp_mir = mir_converter.convert(hir_program);

        if (verbose) {
            std::cerr << "[CPP-CODEGEN-V2] Generated CPP-MIR:\n";
            printCppMirInfo(cpp_mir);
        }

        // Step 2: CPP-MIR → C++
        cpp_mir::CppCodeGenerator codegen;
        std::string cpp_code = codegen.generate(cpp_mir);

        if (verbose) {
            std::cerr << "[CPP-CODEGEN-V2] Generated C++ code ("
                     << countLines(cpp_code) << " lines)\n";
        }

        return cpp_code;
    }

private:
    void printCppMirInfo(const cpp_mir::Program& program) {
        std::cerr << "  - Headers: ";
        for (const auto& header : program.includes) {
            std::cerr << header << " ";
        }
        std::cerr << "\n";

        std::cerr << "  - Functions: " << program.functions.size() << "\n";
        for (const auto& func : program.functions) {
            std::cerr << "    * " << func.name << " (";
            std::cerr << "linear=" << (func.is_linear ? "true" : "false");
            std::cerr << ", uses_printf=" << (func.uses_printf ? "true" : "false");
            std::cerr << ", statements=" << func.body.size();
            std::cerr << ")\n";
        }
    }

    int countLines(const std::string& str) {
        int count = 0;
        for (char c : str) {
            if (c == '\n') count++;
        }
        return count;
    }
};

} // namespace codegen
} // namespace cm