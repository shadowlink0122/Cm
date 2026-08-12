// ============================================================
// TypeChecker 実装 - enum（variantコンストラクタ・タグ値）とtypedefの登録
// ============================================================

#include "internal/types/type_checker.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace cm {

void TypeChecker::register_enum(ast::EnumDecl& en) {
    debug::tc::log(debug::tc::Id::Resolved, "Registering enum: " + en.name, debug::Level::Debug);
    reject_const_generic_params(en.generic_params_v2, current_span_);

    enum_names_.insert(en.name);

    // ジェネリックenumの場合は型パラメータを登録
    if (!en.generic_params.empty()) {
        generic_enums_[en.name] = en.generic_params;
        debug::tc::log(debug::tc::Id::Resolved,
                       "Generic enum: " + en.name + " with " +
                           std::to_string(en.generic_params.size()) + " type params",
                       debug::Level::Debug);
    }

    // 基本型として登録
    scopes_.global().define(en.name, ast::make_named(en.name));

    // Tagged Union情報を保存
    enum_defs_[en.name] = &en;

    // ユーザー定義のResult/Optionは組み込み型を上書きするため
    // type_methods_をクリアする（組み込みメソッドをユーザー実装で上書き可能に）。
    // prelude注入された組み込み宣言（__prelude属性）は対象外
    if (en.name == "Result" || en.name == "Option") {
        bool is_prelude = false;
        for (const auto& attr : en.attributes) {
            if (attr.name == "__prelude") {
                is_prelude = true;
                break;
            }
        }
        if (!is_prelude) {
            type_methods_.erase(en.name);
        }
    }

    int64_t variant_index = 0;
    for (const auto& member : en.members) {
        std::string full_name = en.name + "::" + member.name;

        if (member.has_data()) {
            // ジェネリックenumの場合でもenum_values_に登録
            // Tagged Union用のタグ値として使用
            enum_values_[full_name] = variant_index;

            // ジェネリックenumの場合、variantは通常の関数として登録しない（infer_call内のenum constructor処理で処理する）
            if (!en.generic_params.empty()) {
                debug::tc::log(debug::tc::Id::Resolved,
                               "  " + full_name + "(...) -> " + en.name +
                                   " [generic variant constructor - deferred, tag=" +
                                   std::to_string(variant_index) + "]",
                               debug::Level::Debug);
                variant_index++;
                continue;
            }

            // Associated dataを持つVariant: コンストラクタ関数として登録
            std::vector<ast::TypePtr> param_types;
            for (const auto& [field_name, field_type] : member.fields) {
                param_types.push_back(field_type);
            }

            // 戻り値型はenum型
            ast::TypePtr return_type = ast::make_named(en.name);

            scopes_.global().define_function(full_name, std::move(param_types), return_type);

            debug::tc::log(debug::tc::Id::Resolved,
                           "  " + full_name + "(...) -> " + en.name + " [variant constructor]",
                           debug::Level::Debug);
        } else {
            // シンプルなVariant: 整数定数として登録
            int64_t value = member.value.value_or(0);
            enum_values_[full_name] = value;
            scopes_.global().define(full_name, ast::make_int());

            debug::tc::log(debug::tc::Id::Resolved,
                           "  " + full_name + " = " + std::to_string(value), debug::Level::Debug);
        }
    }
}

void TypeChecker::register_typedef(ast::TypedefDecl& td) {
    debug::tc::log(debug::tc::Id::Resolved, "Registering typedef: " + td.name, debug::Level::Debug);
    scopes_.global().define(td.name, td.type);
    typedef_defs_[td.name] = td.type;
}

}  // namespace cm
