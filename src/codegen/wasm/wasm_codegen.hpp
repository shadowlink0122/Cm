/// @file wasm_codegen.hpp
/// @brief WASM コード生成モジュール
///
/// LLVM WASM ターゲットを使用した WebAssembly コード生成
/// 現在はLLVMバックエンドの一部として動作

#pragma once

#include <string>

namespace cm::codegen::wasm {

/// WASM ランタイムのパスを取得
std::string getWasmRuntimePath();

/// WASM JavaScript glue コードのパスを取得
std::string getWasmJsGluePath();

}  // namespace cm::codegen::wasm
