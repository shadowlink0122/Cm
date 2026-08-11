#include "internal/types/checking/generic/context.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cm {

void GenericContext::add_type_param(const std::string& name,
                                    const std::vector<std::string>& bounds) {
    size_t index = type_params_.size();
    type_params_.push_back({name, bounds, nullptr});
    param_index_[name] = index;
}

bool GenericContext::has_type_param(const std::string& name) const {
    return param_index_.find(name) != param_index_.end();
}

GenericContext::TypeParam* GenericContext::get_type_param(const std::string& name) {
    auto it = param_index_.find(name);
    if (it != param_index_.end()) {
        return &type_params_[it->second];
    }
    return nullptr;
}

bool GenericContext::bind_type(const std::string& name, ast::TypePtr type) {
    auto* param = get_type_param(name);
    if (!param) {
        return false;
    }
    param->concrete_type = type;
    return true;
}

ast::TypePtr GenericContext::get_concrete_type(const std::string& name) const {
    auto it = param_index_.find(name);
    if (it != param_index_.end()) {
        return type_params_[it->second].concrete_type;
    }
    return nullptr;
}

ast::TypePtr GenericContext::substitute_type(const ast::Type& type) const {
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

void GenericContext::clear() {
    type_params_.clear();
    param_index_.clear();
    constraint_cache_.clear();
}

bool GenericContext::check_bounds(
    const std::string& param_name, const ast::Type& concrete_type,
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

GenericContext GenericContext::clone() const {
    GenericContext result;
    result.type_params_ = type_params_;
    result.param_index_ = param_index_;
    return result;
}

}  // namespace cm
