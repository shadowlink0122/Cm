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
// ジェネリックパラメータとその制約を管理（実装は generic_context.cpp）
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
    void add_type_param(const std::string& name, const std::vector<std::string>& bounds = {});

    // 型パラメータが存在するか
    bool has_type_param(const std::string& name) const;

    // 型パラメータを取得
    TypeParam* get_type_param(const std::string& name);

    // 型パラメータを具体的な型にバインド
    bool bind_type(const std::string& name, ast::TypePtr type);

    // 型パラメータの具体的な型を取得
    ast::TypePtr get_concrete_type(const std::string& name) const;

    // 型の置換（ジェネリックパラメータを具体的な型に置き換え）
    ast::TypePtr substitute_type(const ast::Type& type) const;

    // 全ての型パラメータを取得
    const std::vector<TypeParam>& type_params() const { return type_params_; }

    // クリア
    void clear();

    // 型制約をチェック（インターフェース実装のチェック）
    bool check_bounds(const std::string& param_name, const ast::Type& concrete_type,
                      const std::function<bool(const std::string&, const std::string&)>& has_impl);

    // コンテキストをコピー
    GenericContext clone() const;
};

}  // namespace cm
