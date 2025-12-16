/// @file generator.cpp
/// @brief WASM コード生成実装

#include "generator.hpp"

#include <cstdlib>
#include <filesystem>

namespace cm::codegen::wasm {

std::string getWasmRuntimePath() {
    // 環境変数または既知のパスを検索
    if (const char* cmHome = std::getenv("CM_HOME")) {
        std::filesystem::path p(cmHome);
        p /= "lib" / "runtime_wasm.c";
        if (std::filesystem::exists(p)) {
            return p.string();
        }
    }

    // デフォルトパス
    return "src/codegen/llvm/wasm/runtime_wasm.c";
}

std::string getWasmJsGluePath() {
    if (const char* cmHome = std::getenv("CM_HOME")) {
        std::filesystem::path p(cmHome);
        p /= "lib" / "wasm_glue.js";
        if (std::filesystem::exists(p)) {
            return p.string();
        }
    }
    return "";
}

}  // namespace cm::codegen::wasm
