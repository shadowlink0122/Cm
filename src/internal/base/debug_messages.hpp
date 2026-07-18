#pragma once

// デバッグメッセージ統合ヘッダ
// 各ステージのメッセージを一括インクルード

#include "internal/base/debug/ast.hpp"
#include "internal/base/debug/codegen.hpp"
#include "internal/base/debug/hir.hpp"
#include "internal/base/debug/lex.hpp"
#include "internal/base/debug/mir.hpp"
#include "internal/base/debug/par.hpp"
#include "internal/base/debug/tc.hpp"

// 使用例:
// debug::lex::log(debug::lex::Id::Start);
// debug::lex::log(debug::lex::Id::Keyword, "struct", debug::Level::Trace);
// debug::par::log(debug::par::Id::FuncDef, "main");
