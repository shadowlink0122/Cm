#pragma once

#include "../../common/debug.hpp"
#include "../../hir/nodes.hpp"
#include "../nodes.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <variant>

namespace cm::mir {

// ============================================================
// モノモーフィゼーション用ユーティリティ関数
// ============================================================

// ヘルパー関数：MirOperandのクローン
MirOperandPtr clone_operand(const MirOperandPtr& op);

// ヘルパー関数：MirRvalueのクローン
MirRvaluePtr clone_rvalue(const MirRvaluePtr& rv);

// ヘルパー関数：MirStatementのクローン
MirStatementPtr clone_statement(const MirStatementPtr& stmt);

// ヘルパー関数：型置換マップを使ってMirTerminatorをクローンし、メソッド呼び出しを書き換え
MirTerminatorPtr clone_terminator_with_subst(
    const MirTerminatorPtr& term,
    const std::unordered_map<std::string, std::string>& type_name_subst);

// 型から型名文字列を取得するヘルパー
std::string get_type_name(const hir::TypePtr& type);

// 型名から正しいTypePtr型を作成するヘルパー
hir::TypePtr make_type_from_name(const std::string& name);

// 複数パラメータジェネリクス対応: 型引数文字列を分割
// "T, U" -> ["T", "U"], "int, string" -> ["int", "string"]
std::vector<std::string> split_type_args(const std::string& type_arg_str);

}  // namespace cm::mir
