#pragma once

#include <string>
#include <vector>

namespace cm::codegen::js {

// 組み込み関数かどうかをチェック
bool isBuiltinFunction(const std::string& name);

// 組み込み関数呼び出しをJSコードに変換
// argStrsは事前に変換済みの引数文字列
std::string emitBuiltinCall(const std::string& name, const std::vector<std::string>& argStrs);

}  // namespace cm::codegen::js
