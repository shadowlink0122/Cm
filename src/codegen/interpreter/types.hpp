#pragma once

#include "../../common/debug/interp.hpp"
#include "../../mir/nodes.hpp"

#include <any>
#include <functional>
#include <unordered_map>
#include <unordered_set>
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

/// スライス（動的配列）の値を表現
struct SliceValue {
    std::vector<Value> elements;
    hir::TypePtr element_type;
    size_t capacity = 0;

    size_t len() const { return elements.size(); }
    size_t cap() const { return capacity > elements.size() ? capacity : elements.size(); }

    void push(const Value& val) { elements.push_back(val); }

    Value pop() {
        if (elements.empty())
            return Value{};
        Value val = elements.back();
        elements.pop_back();
        return val;
    }

    void remove(size_t idx) {
        if (idx < elements.size()) {
            elements.erase(elements.begin() + idx);
        }
    }

    void clear() { elements.clear(); }

    Value get(size_t idx) const {
        if (idx < elements.size()) {
            return elements[idx];
        }
        return Value{};
    }

    void set(size_t idx, const Value& val) {
        if (idx < elements.size()) {
            elements[idx] = val;
        }
    }
};

/// ポインタの値を表現
struct PointerValue {
    LocalId target_local;  // 参照先のローカル変数ID（外部メモリの場合は0xFFFFFFFF）
    hir::TypePtr element_type;           // 参照先の型
    std::optional<int64_t> array_index;  // 配列要素への参照の場合のインデックス
    std::optional<size_t> field_index;  // 構造体フィールドへの参照の場合のフィールドインデックス
    void* raw_ptr = nullptr;  // FFI経由の外部メモリへの生ポインタ

    bool is_external() const { return raw_ptr != nullptr; }
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
    std::unordered_set<LocalId> skip_static_init;  // 初期化をスキップするstatic変数

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
            // 配列型の場合
            else if (local.type && local.type->kind == hir::TypeKind::Array) {
                // 動的配列（スライス）の場合はSliceValueとして初期化
                if (!local.type->array_size.has_value()) {
                    SliceValue sv;
                    sv.element_type = local.type->element_type;
                    sv.capacity = 4;  // 初期容量
                    locals[local.id] = Value(sv);
                    debug::interp::log(debug::interp::Id::LocalInit,
                                       "Initialized slice local _" + std::to_string(local.id),
                                       debug::Level::Debug);
                }
                // 固定サイズ配列の場合はArrayValueとして初期化
                else {
                    ArrayValue av;
                    av.element_type = local.type->element_type;
                    // 固定サイズ配列の場合はサイズを設定
                    if (local.type->array_size) {
                        av.elements.resize(*local.type->array_size);
                        // 要素を初期化
                        for (auto& elem : av.elements) {
                            if (av.element_type) {
                                if (av.element_type->kind == hir::TypeKind::Int ||
                                    av.element_type->kind == hir::TypeKind::Long) {
                                    elem = Value(int64_t{0});
                                } else if (av.element_type->kind == hir::TypeKind::Double ||
                                           av.element_type->kind == hir::TypeKind::Float) {
                                    elem = Value(double{0.0});
                                } else if (av.element_type->kind == hir::TypeKind::Bool) {
                                    elem = Value(false);
                                } else if (av.element_type->kind == hir::TypeKind::String) {
                                    elem = Value(std::string{});
                                } else if (av.element_type->kind == hir::TypeKind::Struct) {
                                    StructValue sv;
                                    sv.type_name = av.element_type->name;
                                    elem = Value(sv);
                                } else {
                                    elem = Value(int64_t{0});
                                }
                            } else {
                                elem = Value(int64_t{0});
                            }
                        }
                    }
                    locals[local.id] = Value(av);
                    debug::interp::log(debug::interp::Id::LocalInit,
                                       "Initialized array local _" + std::to_string(local.id) +
                                           " with " + std::to_string(av.elements.size()) +
                                           " elements",
                                       debug::Level::Debug);
                }
            }
        }
    }

    /// static変数への初期化代入かどうかを判定
    bool should_skip_static_init(LocalId id) const { return skip_static_init.count(id) > 0; }

    /// static変数の初期化済みフラグをクリア（実際の代入後）
    void mark_static_initialized(LocalId id) { skip_static_init.erase(id); }
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

// ============================================================
// ヘルパー関数
// ============================================================

/// 値を文字列に変換（デバッグ用）
inline std::string value_to_string(const Value& val) {
    if (!val.has_value())
        return "<empty>";

    if (val.type() == typeid(int64_t)) {
        return std::to_string(std::any_cast<int64_t>(val));
    }
    if (val.type() == typeid(double)) {
        return std::to_string(std::any_cast<double>(val));
    }
    if (val.type() == typeid(bool)) {
        return std::any_cast<bool>(val) ? "true" : "false";
    }
    if (val.type() == typeid(std::string)) {
        return "\"" + std::any_cast<std::string>(val) + "\"";
    }
    if (val.type() == typeid(char)) {
        return std::string("'") + std::any_cast<char>(val) + "'";
    }
    if (val.type() == typeid(StructValue)) {
        return "<struct:" + std::any_cast<StructValue>(val).type_name + ">";
    }
    return "<unknown>";
}

}  // namespace cm::mir::interp
