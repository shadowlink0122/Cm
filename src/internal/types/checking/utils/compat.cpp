// ============================================================
// TypeChecker 実装 - 型解決（typedef/名前空間）・型互換性・共通型・インターフェース適合・型有効性の判定
// ============================================================

#include "internal/base/i18n.hpp"
#include "internal/types/type_checker.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace cm {

ast::TypePtr TypeChecker::resolve_typedef(ast::TypePtr type) {
    if (!type)
        return type;

    // 名前付き型（Struct/Interface/Generic）の場合
    if (type->kind == ast::TypeKind::Struct || type->kind == ast::TypeKind::Interface ||
        type->kind == ast::TypeKind::Generic) {
        // 名前空間内の非修飾名は「現在の名前空間::名前」へ書き換える（全ての型がここを通るため、宣言型・戻り値型・リテラル型の修飾が一貫し、HIR/コード生成も同じ名前を見る）
        if (!type->name.empty() && struct_defs_.count(type->name) == 0 &&
            enum_names_.count(type->name) == 0 && typedef_defs_.count(type->name) == 0 &&
            interface_names_.count(type->name) == 0 &&
            !generic_context_.has_type_param(type->name)) {
            if (auto qualified = resolve_in_namespace(type->name)) {
                type->name = *qualified;
            }
        }
        // enum名の場合はint型として解決
        // ただしtype_argsを持つTagged Union enum（Result<int,string>等）は除外
        if (enum_names_.count(type->name)) {
            // type_argsを持つジェネリックenum（Result<T,E>, Option<T>等）はTagged Unionとしてそのまま保持
            if (!type->type_args.empty() && generic_enums_.count(type->name)) {
                // そのまま返す（構造体として扱われる）
            } else if (!type->type_args.empty()) {
                // ジェネリックenum以外でもtype_argsがある場合はTagged Union判定
                bool is_tagged_union = false;
                auto def_it = enum_defs_.find(type->name);
                if (def_it != enum_defs_.end() && def_it->second) {
                    for (const auto& member : def_it->second->members) {
                        if (member.has_data()) {
                            is_tagged_union = true;
                            break;
                        }
                    }
                }
                if (!is_tagged_union) {
                    return ast::make_int();
                }
            } else {
                // type_argsなし: ペイロード付きバリアントを持つenum（IntResult等）はTagged Unionとして保持し、値enum（従来型）のみintへ解決する（一律int化すると関数返却・match束縛でペイロードが失われる）
                bool is_tagged_union = false;
                auto def_it = enum_defs_.find(type->name);
                if (def_it != enum_defs_.end() && def_it->second) {
                    for (const auto& member : def_it->second->members) {
                        if (member.has_data()) {
                            is_tagged_union = true;
                            break;
                        }
                    }
                }
                if (!is_tagged_union) {
                    return ast::make_int();
                }
            }
        }

        // typedefに登録されていれば解決
        auto it = typedef_defs_.find(type->name);
        if (it != typedef_defs_.end()) {
            return it->second;
        }
    }

    return type;
}

bool TypeChecker::types_compatible(ast::TypePtr a, ast::TypePtr b) {
    // ビットベクタと整数の互換（v0.16.0 ビットスライス）:
    // bit[N] への整数代入・整数文脈での bit[N] 使用を許可する
    {
        auto is_bit_vec = [](const ast::TypePtr& t) {
            return t && t->kind == ast::TypeKind::Array && t->element_type &&
                   t->element_type->kind == ast::TypeKind::Bit;
        };
        if ((is_bit_vec(a) && b && b->is_integer()) || (is_bit_vec(b) && a && a->is_integer())) {
            return true;
        }
    }

    if (!a || !b)
        return false;
    if (a->kind == ast::TypeKind::Error || b->kind == ast::TypeKind::Error)
        return true;

    // ユニオン型への代入互換性チェック
    // 例: int | null x = null; → a=Union{int,null}, b=Void
    // 例: int | null x = 42;   → a=Union{int,null}, b=Int
    if (a->kind == ast::TypeKind::Union) {
        auto* union_type = static_cast<const ast::UnionType*>(a.get());
        if (union_type) {
            // nullリテラル（Void型）の代入: Nullバリアントがあれば許可
            if (b->kind == ast::TypeKind::Void) {
                for (const auto& variant : union_type->variants) {
                    if (!variant.fields.empty() && variant.fields[0] &&
                        variant.fields[0]->kind == ast::TypeKind::Null) {
                        return true;
                    }
                }
            }
            // メンバー型との互換性チェック: いずれかのバリアントの型と互換なら許可
            for (const auto& variant : union_type->variants) {
                if (!variant.fields.empty() && variant.fields[0]) {
                    // Null型バリアントはスキップ（Voidは上で処理済み）
                    if (variant.fields[0]->kind == ast::TypeKind::Null)
                        continue;
                    if (types_compatible(variant.fields[0], b)) {
                        return true;
                    }
                }
            }
        }
    }

    // 再帰ガード：相互参照型による無限再帰を防止
    // 型ペアを正規化してセットで管理
    static thread_local std::set<std::pair<std::string, std::string>> visited_pairs;
    std::string a_str = ast::type_to_string(*a);
    std::string b_str = ast::type_to_string(*b);
    // 順序を正規化（a < b）
    auto key = a_str < b_str ? std::make_pair(a_str, b_str) : std::make_pair(b_str, a_str);

    if (visited_pairs.count(key) > 0) {
        // 既に比較中のペア → 無限再帰を回避、同じと見なす
        return true;
    }

    // RAIIガード
    struct RecursionGuard {
        std::set<std::pair<std::string, std::string>>& set;
        std::pair<std::string, std::string> k;
        RecursionGuard(std::set<std::pair<std::string, std::string>>& s,
                       std::pair<std::string, std::string> key)
            : set(s), k(key) {
            set.insert(k);
        }
        ~RecursionGuard() { set.erase(k); }
    } guard(visited_pairs, key);

    // ジェネリック型パラメータのチェック
    std::string a_name = ast::type_to_string(*a);
    std::string b_name = ast::type_to_string(*b);
    if (generic_context_.has_type_param(a_name) || generic_context_.has_type_param(b_name)) {
        return a_name == b_name;
    }

    // ジェネリックenum（Tagged Union）互換性: resolve_typedef前に判定
    // Result<int, string> vs Result のようにtype_argsが一方にしかない場合でも
    // 同名のジェネリックenumなら互換とみなす
    if ((a->kind == ast::TypeKind::Struct || a->kind == ast::TypeKind::Generic) &&
        (b->kind == ast::TypeKind::Struct || b->kind == ast::TypeKind::Generic) &&
        a->name == b->name && !a->name.empty() && generic_enums_.count(a->name) > 0 &&
        a->type_args.size() != b->type_args.size()) {
        return true;
    }

    // typedefを展開（名前付き型の場合）
    a = resolve_typedef(a);
    b = resolve_typedef(b);

    // インターフェース互換性チェック
    if (a->kind == ast::TypeKind::Struct && interface_names_.count(a->name)) {
        if (b->kind == ast::TypeKind::Struct && !interface_names_.count(b->name)) {
            auto it = impl_interfaces_.find(b->name);
            if (it != impl_interfaces_.end()) {
                if (it->second.count(a->name)) {
                    return true;
                }
            }
        }
    }

    // 同じ型
    if (a->kind == b->kind) {
        if (a->kind == ast::TypeKind::Struct) {
            // 名前が一致しない場合は不一致
            if (a->name != b->name) {
                return false;
            }
            // ジェネリック型引数の比較（Result<int, string> vs Result<int, int>など）
            if (a->type_args.size() != b->type_args.size()) {
                // Tagged Union enum: 一方がtype_argsなしでも名前一致なら互換
                // Result<int, string> vs Result（コンストラクタ側が型引数未推論の場合）
                if (generic_enums_.count(a->name) > 0) {
                    return true;
                }
                return false;
            }
            for (size_t i = 0; i < a->type_args.size(); ++i) {
                if (!types_compatible(a->type_args[i], b->type_args[i])) {
                    return false;
                }
            }
            return true;
        }
        if (a->kind == ast::TypeKind::Interface) {
            return a->name == b->name;
        }
        // ポインタ型の互換性チェック（借用安全性）
        if (a->kind == ast::TypeKind::Pointer) {
            // void* → T* の暗黙変換を許可（FFI用）
            if (b->element_type && b->element_type->kind == ast::TypeKind::Void) {
                return true;
            }
            // T* → void* の暗黙変換を許可（FFI用）
            if (a->element_type && a->element_type->kind == ast::TypeKind::Void) {
                return true;
            }
            // const T* → T* は禁止（constを外せない）
            // b（代入元）がconstでa（代入先）が非constの場合は禁止
            if (b->qualifiers.is_const && !a->qualifiers.is_const) {
                return false;
            }
            // 要素型のconst外しも禁止
            if (b->element_type && b->element_type->qualifiers.is_const && a->element_type &&
                !a->element_type->qualifiers.is_const) {
                return false;
            }
            // 要素型の互換性をチェック
            return types_compatible(a->element_type, b->element_type);
        }
        // 関数ポインタ型の互換性チェック
        if (a->kind == ast::TypeKind::Function) {
            if (!types_compatible(a->return_type, b->return_type)) {
                return false;
            }
            if (a->param_types.size() != b->param_types.size()) {
                return false;
            }
            for (size_t i = 0; i < a->param_types.size(); ++i) {
                if (!types_compatible(a->param_types[i], b->param_types[i])) {
                    return false;
                }
            }
            return true;
        }
        return true;
    }

    // 数値型間の暗黙変換
    if (a->is_numeric() && b->is_numeric()) {
        return true;
    }

    // 構造体のdefaultメンバとの互換性チェック
    if (a->kind == ast::TypeKind::Struct) {
        auto default_type = get_default_member_type(a->name);
        if (default_type && types_compatible(default_type, b)) {
            return true;
        }
    }
    if (b->kind == ast::TypeKind::Struct) {
        auto default_type = get_default_member_type(b->name);
        if (default_type && types_compatible(a, default_type)) {
            return true;
        }
    }

    // 配列→ポインタ暗黙変換 (array decay)
    if (a->kind == ast::TypeKind::Pointer && b->kind == ast::TypeKind::Array) {
        if (a->element_type && b->element_type) {
            return types_compatible(a->element_type, b->element_type);
        }
    }

    // string → *char 暗黙変換 (FFI用)
    if (a->kind == ast::TypeKind::Pointer && b->kind == ast::TypeKind::String) {
        if (a->element_type && a->element_type->kind == ast::TypeKind::Char) {
            return true;
        }
    }

    // cstring ↔ string 暗黙変換 (FFI用)
    if ((a->kind == ast::TypeKind::CString && b->kind == ast::TypeKind::String) ||
        (a->kind == ast::TypeKind::String && b->kind == ast::TypeKind::CString)) {
        return true;
    }

    // cstring ↔ *char 暗黙変換 (FFI用)
    if (a->kind == ast::TypeKind::CString && b->kind == ast::TypeKind::Pointer) {
        if (b->element_type && b->element_type->kind == ast::TypeKind::Char) {
            return true;
        }
    }
    if (b->kind == ast::TypeKind::CString && a->kind == ast::TypeKind::Pointer) {
        if (a->element_type && a->element_type->kind == ast::TypeKind::Char) {
            return true;
        }
    }

    // LiteralUnion型とプリミティブ型の互換性
    // typedef HttpMethod = "GET" | "POST" のような型へのstring代入を許可
    if (a->kind == ast::TypeKind::LiteralUnion) {
        // LiteralUnionTypeから基底型を判定
        auto* lit_union = static_cast<ast::LiteralUnionType*>(a.get());
        if (lit_union && !lit_union->literals.empty()) {
            const auto& first_lit = lit_union->literals[0];
            // 文字列リテラルユニオン → string互換
            if (std::holds_alternative<std::string>(first_lit.value)) {
                if (b->kind == ast::TypeKind::String) {
                    return true;
                }
            }
            // 整数リテラルユニオン → int互換
            else if (std::holds_alternative<int64_t>(first_lit.value)) {
                if (b->is_numeric()) {
                    return true;
                }
            }
            // 浮動小数点リテラルユニオン → float/double互換
            else if (std::holds_alternative<double>(first_lit.value)) {
                if (b->is_floating()) {
                    return true;
                }
            }
        }
    }

    return false;
}

// ============================================================
// リテラル型チェック
// typedef HttpMethod = "GET" | "POST" | "PUT" | "DELETE" のような
// リテラルユニオン型への代入時に、許容されるリテラル値かをチェックする
// ============================================================
bool TypeChecker::check_literal_assignment(ast::TypePtr target_type, ast::Expr* init_expr,
                                           Span span) {
    if (!target_type || !init_expr) {
        return true;  // チェック不要
    }

    // typedefを解決
    auto resolved_type = resolve_typedef(target_type);
    if (!resolved_type || resolved_type->kind != ast::TypeKind::LiteralUnion) {
        return true;  // LiteralUnion型でなければチェック不要
    }

    // LiteralUnionType にキャスト（TypeKind::LiteralUnionで判定済みなのでstatic_castで安全）
    auto* lit_union = static_cast<ast::LiteralUnionType*>(resolved_type.get());
    if (!lit_union || lit_union->literals.empty()) {
        return true;  // リテラルリストが空ならチェック不要
    }

    // 初期化式がリテラルでなければチェック不可（動的値は許容）
    auto* lit_expr = init_expr->as<ast::LiteralExpr>();
    if (!lit_expr) {
        // リテラルでない場合は実行時に検証される想定でパス
        // TODO: 変数参照の場合も追跡可能にするかは将来検討
        return true;
    }

    // 許容リテラル一覧を取得して検証
    bool found = false;
    std::vector<std::string> allowed_values;

    for (const auto& allowed : lit_union->literals) {
        // 文字列リテラル
        if (std::holds_alternative<std::string>(allowed.value)) {
            const auto& allowed_str = std::get<std::string>(allowed.value);
            allowed_values.push_back("\"" + allowed_str + "\"");

            if (std::holds_alternative<std::string>(lit_expr->value)) {
                if (std::get<std::string>(lit_expr->value) == allowed_str) {
                    found = true;
                    break;
                }
            }
        }
        // 整数リテラル
        else if (std::holds_alternative<int64_t>(allowed.value)) {
            int64_t allowed_int = std::get<int64_t>(allowed.value);
            allowed_values.push_back(std::to_string(allowed_int));

            if (std::holds_alternative<int64_t>(lit_expr->value)) {
                if (std::get<int64_t>(lit_expr->value) == allowed_int) {
                    found = true;
                    break;
                }
            }
        }
        // 浮動小数点リテラル
        else if (std::holds_alternative<double>(allowed.value)) {
            double allowed_float = std::get<double>(allowed.value);
            allowed_values.push_back(std::to_string(allowed_float));

            if (std::holds_alternative<double>(lit_expr->value)) {
                if (std::get<double>(lit_expr->value) == allowed_float) {
                    found = true;
                    break;
                }
            }
        }
    }

    if (!found) {
        // 代入される値を文字列化
        std::string actual_value;
        if (std::holds_alternative<std::string>(lit_expr->value)) {
            actual_value = "\"" + std::get<std::string>(lit_expr->value) + "\"";
        } else if (std::holds_alternative<int64_t>(lit_expr->value)) {
            actual_value = std::to_string(std::get<int64_t>(lit_expr->value));
        } else if (std::holds_alternative<double>(lit_expr->value)) {
            actual_value = std::to_string(std::get<double>(lit_expr->value));
        } else if (std::holds_alternative<bool>(lit_expr->value)) {
            actual_value = std::get<bool>(lit_expr->value) ? "true" : "false";
        } else {
            actual_value = "(unknown)";
        }

        // 許容値の一覧を作成
        std::string allowed_list;
        for (size_t i = 0; i < allowed_values.size(); ++i) {
            if (i > 0)
                allowed_list += " | ";
            allowed_list += allowed_values[i];
        }

        error(span, "Invalid literal value " + actual_value +
                        " for literal type. Allowed values: " + allowed_list);
        return false;
    }

    return true;
}

ast::TypePtr TypeChecker::common_type(ast::TypePtr a, ast::TypePtr b) {
    if (a->kind == b->kind)
        return a;

    // float > int
    if (a->is_floating() || b->is_floating()) {
        return a->kind == ast::TypeKind::Double || b->kind == ast::TypeKind::Double
                   ? ast::make_double()
                   : ast::make_float();
    }

    // より大きい整数型
    auto a_info = a->info();
    auto b_info = b->info();
    return a_info.size >= b_info.size ? a : b;
}

bool TypeChecker::type_implements_interface(const std::string& type_name,
                                            const std::string& interface_name) {
    // プリミティブ型の組み込みインターフェース
    // Ord: 比較可能な型（数値型、文字型）
    if (interface_name == "Ord") {
        if (type_name == "int" || type_name == "uint" || type_name == "tiny" ||
            type_name == "utiny" || type_name == "short" || type_name == "ushort" ||
            type_name == "long" || type_name == "ulong" || type_name == "float" ||
            type_name == "double" || type_name == "char") {
            return true;
        }
    }

    // Eq: 等価比較可能な型
    if (interface_name == "Eq") {
        if (type_name == "int" || type_name == "uint" || type_name == "tiny" ||
            type_name == "utiny" || type_name == "short" || type_name == "ushort" ||
            type_name == "long" || type_name == "ulong" || type_name == "float" ||
            type_name == "double" || type_name == "char" || type_name == "bool" ||
            type_name == "string") {
            return true;
        }
    }

    // Clone: コピー可能な型
    if (interface_name == "Clone") {
        if (type_name == "int" || type_name == "uint" || type_name == "tiny" ||
            type_name == "utiny" || type_name == "short" || type_name == "ushort" ||
            type_name == "long" || type_name == "ulong" || type_name == "float" ||
            type_name == "double" || type_name == "char" || type_name == "bool" ||
            type_name == "string") {
            return true;
        }
    }

    // 明示的なimpl実装をチェック
    auto it = impl_interfaces_.find(type_name);
    if (it != impl_interfaces_.end()) {
        if (it->second.count(interface_name)) {
            return true;
        }
    }

    // with による自動実装をチェック
    if (has_auto_impl(type_name, interface_name)) {
        return true;
    }

    return false;
}

bool TypeChecker::check_type_constraints(const std::string& type_name,
                                         const std::vector<std::string>& constraints) {
    for (const auto& constraint : constraints) {
        if (!type_implements_interface(type_name, constraint)) {
            return false;
        }
    }
    return true;
}

std::optional<std::string> TypeChecker::resolve_in_namespace(const std::string& name) const {
    if (current_namespace_.empty() || name.find("::") != std::string::npos) {
        return std::nullopt;
    }
    // 内側の名前空間から外側へ向かって探索する（M::N内なら M::N::name → M::name）
    std::string ns = current_namespace_;
    while (!ns.empty()) {
        std::string qualified = ns + "::" + name;
        if (struct_defs_.count(qualified) > 0 || interface_names_.count(qualified) > 0 ||
            enum_names_.count(qualified) > 0 || typedef_defs_.count(qualified) > 0) {
            return qualified;
        }
        auto pos = ns.rfind("::");
        if (pos == std::string::npos) {
            break;
        }
        ns = ns.substr(0, pos);
    }
    return std::nullopt;
}

bool TypeChecker::is_valid_type(ast::TypePtr type) {
    if (!type)
        return true;

    // プリミティブ型は有効
    if (type->is_primitive())
        return true;

    switch (type->kind) {
        case ast::TypeKind::Posedge:
        case ast::TypeKind::Negedge:
        case ast::TypeKind::Wire:
        case ast::TypeKind::Reg:
        case ast::TypeKind::Bit:
        case ast::TypeKind::Null:
            return true;
        case ast::TypeKind::Pointer:
        case ast::TypeKind::Array:
            return is_valid_type(type->element_type);
        case ast::TypeKind::Struct:
        case ast::TypeKind::Interface:
        case ast::TypeKind::Generic:
            // 構造体名、インターフェース名、enum名、typedef名、またはジェネリック型引数として存在するかチェック
            if (struct_defs_.count(type->name) > 0 || interface_names_.count(type->name) > 0 ||
                enum_names_.count(type->name) > 0 || typedef_defs_.count(type->name) > 0 ||
                generic_context_.has_type_param(type->name)) {
                return true;
            }
            // 名前空間内では非修飾名を「現在の名前空間::名前」として解決する（外側の名前空間へ向かって順に探索）。解決できた場合は型名を修飾名へ書き換え、HIR/MIR/コード生成が一貫した名前を見るようにする
            if (auto qualified = resolve_in_namespace(type->name)) {
                type->name = *qualified;
                return true;
            }
            return false;
        default:
            return true;
    }
}

}  // namespace cm
