#pragma once

// デバッグメッセージ統合ヘッダ
// 各ステージのメッセージを一括インクルード

#include "debug/ast.hpp"
#include "debug/codegen.hpp"
#include "debug/hir.hpp"
#include "debug/lex.hpp"
#include "debug/mir.hpp"
#include "debug/par.hpp"
#include "debug/tc.hpp"

// 使用例:
// debug::lex::log(debug::lex::Id::Start);
// debug::lex::log(debug::lex::Id::Keyword, "struct", debug::Level::Trace);
// debug::par::log(debug::par::Id::FuncDef, "main");
