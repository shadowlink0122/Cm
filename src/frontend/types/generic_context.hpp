#pragma once

#include "../ast/types.hpp"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cm {

// ============================================================
// ジェネリックコンテキスト
// ジェネリックパラメータとその制約を管理
// ============================================================
class GenericContext {
   public:
    // 型パラメータの情報
    struct TypeParam {
        std::string name;
        std::vector<std::string> bounds;  // 型制約（例: Ord, Clone）
        ast::TypePtr concrete_type;       // インスタンス化時の具体的な型
    };

   private:
    // 現在のジェネリックパラメータ
    std::vector<TypeParam> type_params_;

    // 型パラメータ名からインデックスへのマップ
    std::unordered_map<std::string, size_t> param_index_;

    // 型制約のチェック結果のキャッシュ
    std::unordered_map<std::string, bool> constraint_cache_;

   public:
    // 型パラメータを追加
    void add_type_param(const std::string& name, const std::vector<std::string>& bounds = {}) {
        size_t index = type_params_.size();
        type_params_.push_back({name, bounds, nullptr});
        param_index_[name] = index;
    }

    // 型パラメータが存在するか
    bool has_type_param(const std::string& name) const {
        return param_index_.find(name) != param_index_.end();
    }

    // 型パラメータを取得
    TypeParam* get_type_param(const std::string& name) {
        auto it = param_index_.find(name);
        if (it != param_index_.end()) {
            return &type_params_[it->second];
        }
        return nullptr;
    }

    // 型パラメータを具体的な型にバインド
    bool bind_type(const std::string& name, ast::TypePtr type) {
        auto* param = get_type_param(name);
        if (!param) {
            return false;
        }
        param->concrete_type = type;
        return true;
    }

    // 型パラメータの具体的な型を取得
    ast::TypePtr get_concrete_type(const std::string& name) const {
        auto it = param_index_.find(name);
        if (it != param_index_.end()) {
            return type_params_[it->second].concrete_type;
        }
        return nullptr;
    }

    // 型の置換（ジェネリックパラメータを具体的な型に置き換え）
    ast::TypePtr substitute_type(const ast::Type& type) const {
        // Genericタイプの場合、ジェネリックパラメータかチェック
        if (type.kind == ast::TypeKind::Generic) {
            const std::string& name = type.name;
            if (auto concrete = get_concrete_type(name)) {
                return concrete;
            }
        }

        // Arrayタイプの場合、要素型を再帰的に置換
        if (type.kind == ast::TypeKind::Array) {
            if (type.element_type) {
                auto substituted = substitute_type(*type.element_type);
                auto result = ast::make_array(substituted, type.array_size);
                return result;
            }
        }

        // Pointerタイプの場合、指す型を再帰的に置換
        if (type.kind == ast::TypeKind::Pointer) {
            if (type.element_type) {
                auto substituted = substitute_type(*type.element_type);
                return ast::make_pointer(substituted);
            }
        }

        // 置換不要な場合は元の型をコピー
        return std::make_shared<ast::Type>(type);
    }

    // 全ての型パラメータを取得
    const std::vector<TypeParam>& type_params() const { return type_params_; }

    // クリア
    void clear() {
        type_params_.clear();
        param_index_.clear();
        constraint_cache_.clear();
    }

    // 型制約をチェック（インターフェース実装のチェック）
    bool check_bounds(const std::string& param_name, const ast::Type& concrete_type,
                      const std::function<bool(const std::string&, const std::string&)>& has_impl) {
        auto* param = get_type_param(param_name);
        if (!param) {
            return false;
        }

        // 全ての制約をチェック
        std::string type_str = ast::type_to_string(concrete_type);
        for (const auto& bound : param->bounds) {
            if (!has_impl(type_str, bound)) {
                return false;
            }
        }

        return true;
    }

    // コンテキストをコピー
    GenericContext clone() const {
        GenericContext result;
        result.type_params_ = type_params_;
        result.param_index_ = param_index_;
        return result;
    }
};

}  // namespace cm