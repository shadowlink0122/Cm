#pragma once

#include "../../common/debug/interp.hpp"
#include "../../mir/mir_nodes.hpp"

#include <any>
#include <functional>
#include <unordered_map>
#include <vector>

namespace cm::mir::interp {

// ============================================================
// 値の型定義
// ============================================================

/// MIR実行時の値を表現
using Value = std::any;

/// 構造体の値を表現（型名 + フィールド値のマップ）
struct StructValue {
    std::string type_name;
    std::unordered_map<FieldId, Value> fields;
};

/// 配列の値を表現
struct ArrayValue {
    std::vector<Value> elements;
    hir::TypePtr element_type;
};

// ============================================================
// 組み込み関数の型
// ============================================================

/// 組み込み関数のシグネチャ
using BuiltinFn =
    std::function<Value(std::vector<Value>, const std::unordered_map<LocalId, Value>&)>;

/// 組み込み関数レジストリ
using BuiltinRegistry = std::unordered_map<std::string, BuiltinFn>;

// ============================================================
// 実行コンテキスト
// ============================================================

/// 関数実行時のコンテキスト
struct ExecutionContext {
    const MirFunction* function;
    std::unordered_map<LocalId, Value> locals;
    BuiltinRegistry* builtins;

    ExecutionContext(const MirFunction* func, BuiltinRegistry* builtin_registry)
        : function(func), builtins(builtin_registry) {
        initialize_locals();
    }

    /// ローカル変数を初期化
    void initialize_locals() {
        for (const auto& local : function->locals) {
            // 構造体型の場合はStructValueとして初期化
            if (local.type && local.type->kind == hir::TypeKind::Struct) {
                StructValue sv;
                sv.type_name = local.type->name;
                locals[local.id] = Value(sv);
                debug::interp::log(
                    debug::interp::Id::LocalInit,
                    "Initialized struct local _" + std::to_string(local.id) + " as " + sv.type_name,
                    debug::Level::Debug);
            }
        }
    }
};

// ============================================================
// 実行結果
// ============================================================

/// 関数実行の結果
struct ExecutionResult {
    bool success;
    Value return_value;
    std::string error_message;

    static ExecutionResult ok(Value val = {}) { return {true, std::move(val), ""}; }

    static ExecutionResult error(const std::string& msg) { return {false, {}, msg}; }
};

}  // namespace cm::mir::interp
