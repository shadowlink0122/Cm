#pragma once

#include "emitter.hpp"

#include <unordered_set>

namespace cm::codegen::js {

// ランタイムヘルパーコードを出力
void emitRuntime(JSEmitter& emitter, const std::unordered_set<std::string>& used_helpers);

// Webランタイム出力
void emitWebRuntime(JSEmitter& emitter);

}  // namespace cm::codegen::js
