// ============================================================
// AST型ノードの実装
// ============================================================
// types.hpp で宣言された非トリビアルな関数の実装。ヘッダーには宣言と一行ヘルパーのみを置く

#include "types.hpp"

namespace cm::ast {

// プリミティブ型のサイズ情報
TypeInfo get_primitive_info(TypeKind kind) {
    switch (kind) {
        case TypeKind::Void:
            return {0, 1};
        case TypeKind::Bool:
            return {1, 1};
        case TypeKind::Tiny:
        case TypeKind::UTiny:
        case TypeKind::Char:
            return {1, 1};
        case TypeKind::Short:
        case TypeKind::UShort:
            return {2, 2};
        case TypeKind::Int:
        case TypeKind::UInt:
        case TypeKind::Float:
        case TypeKind::UFloat:
            return {4, 4};
        case TypeKind::Long:
        case TypeKind::ULong:
        case TypeKind::Double:
        case TypeKind::UDouble:
            return {8, 8};
        case TypeKind::ISize:
        case TypeKind::USize:
        case TypeKind::Pointer:
        case TypeKind::Reference:
        case TypeKind::String:
        case TypeKind::CString:
            return {8, 8};  // ポインタサイズ
        case TypeKind::Posedge:
        case TypeKind::Negedge:
            return {1, 1};  // 信号型（1bit）
        case TypeKind::Wire:
        case TypeKind::Reg:
            return {0, 1};  // 修飾子型（サイズは要素型依存）
        default:
            return {0, 1};
    }
}

// ============================================================
// Type
// ============================================================

TypeInfo Type::info() const {
    if (is_primitive() || is_pointer_like()) {
        return get_primitive_info(kind);
    }
    // TODO: 構造体サイズ計算
    return {0, 1};
}

// フラット化されたサイズを取得
uint32_t Type::get_flattened_size() const {
    if (!dimensions.empty()) {
        uint32_t total = 1;
        for (auto d : dimensions) {
            total *= d;
        }
        return total;
    }
    return array_size.value_or(1);
}

// 最終要素型を取得（多次元配列の基底要素型）
TypePtr Type::get_base_element_type() const {
    if (kind != TypeKind::Array)
        return nullptr;
    TypePtr current = element_type;
    while (current && current->kind == TypeKind::Array && current->element_type) {
        current = current->element_type;
    }
    return current;
}

// ============================================================
// 型の文字列表現
// ============================================================
std::string type_to_string(const Type& t) {
    std::string prefix = "";
    switch (t.kind) {
        case TypeKind::Void:
            return prefix + "void";
        case TypeKind::Bool:
            return prefix + "bool";
        case TypeKind::Tiny:
            return prefix + "tiny";
        case TypeKind::Short:
            return prefix + "short";
        case TypeKind::Int:
            return prefix + "int";
        case TypeKind::Long:
            return prefix + "long";
        case TypeKind::UTiny:
            return prefix + "utiny";
        case TypeKind::UShort:
            return prefix + "ushort";
        case TypeKind::UInt:
            return prefix + "uint";
        case TypeKind::ULong:
            return prefix + "ulong";
        case TypeKind::ISize:
            return prefix + "isize";
        case TypeKind::USize:
            return prefix + "usize";
        case TypeKind::Float:
            return prefix + "float";
        case TypeKind::Double:
            return prefix + "double";
        case TypeKind::UFloat:
            return prefix + "ufloat";
        case TypeKind::UDouble:
            return prefix + "udouble";
        case TypeKind::Char:
            return prefix + "char";
        case TypeKind::String:
            return prefix + "string";
        case TypeKind::CString:
            return prefix + "cstring";
        case TypeKind::Pointer:
            return "*" + (t.element_type ? type_to_string(*t.element_type) : "?");
        case TypeKind::Reference:
            return "&" + (t.element_type ? type_to_string(*t.element_type) : "?");
        case TypeKind::Array:
            if (t.array_size) {
                return (t.element_type ? type_to_string(*t.element_type) : "?") + "[" +
                       std::to_string(*t.array_size) + "]";
            }
            if (!t.size_param_name.empty()) {
                return (t.element_type ? type_to_string(*t.element_type) : "?") + "[" +
                       t.size_param_name + "]";
            }
            return (t.element_type ? type_to_string(*t.element_type) : "?") + "[]";
        case TypeKind::Struct:
        case TypeKind::Interface: {
            std::string result = prefix + t.name;
            if (!t.type_args.empty()) {
                result += "<";
                for (size_t i = 0; i < t.type_args.size(); ++i) {
                    if (i > 0)
                        result += ", ";
                    result += type_to_string(*t.type_args[i]);
                }
                result += ">";
            }
            return result;
        }
        case TypeKind::Generic:
            return "<" + t.name + ">";
        case TypeKind::Function: {
            // 関数ポインタ型: int(*)(int, int)
            std::string result = (t.return_type ? type_to_string(*t.return_type) : "void");
            result += "(*)";
            result += "(";
            for (size_t i = 0; i < t.param_types.size(); ++i) {
                if (i > 0)
                    result += ", ";
                result += type_to_string(*t.param_types[i]);
            }
            result += ")";
            return result;
        }
        case TypeKind::Error:
            return "<error>";
        case TypeKind::Inferred:
            return "<inferred>";
        case TypeKind::Null:
            return "null";
        case TypeKind::Posedge:
            return "posedge";
        case TypeKind::Negedge:
            return "negedge";
        case TypeKind::Wire:
            return "wire " + (t.element_type ? type_to_string(*t.element_type) : "?");
        case TypeKind::Reg:
            return "reg " + (t.element_type ? type_to_string(*t.element_type) : "?");
        case TypeKind::Union: {
            // 名前付きユニオン（typedef）はその名前を使用
            if (!t.name.empty()) {
                return prefix + t.name;
            }
            // インラインユニオン: type_argsから "int | null" 形式で表示
            if (!t.type_args.empty()) {
                std::string result;
                for (size_t i = 0; i < t.type_args.size(); ++i) {
                    if (i > 0)
                        result += " | ";
                    result += type_to_string(*t.type_args[i]);
                }
                return prefix + result;
            }
            return prefix + "<union>";
        }
        default:
            return "<unknown>";
    }
}

// 型パラメータ置換の正準実装（モノモーフィゼーション・MIRローワが共有）
TypePtr substitute_type_params(const TypePtr& type,
                               const std::unordered_map<std::string, TypePtr>& subst) {
    if (!type)
        return nullptr;

    if (!type->name.empty()) {
        auto it = subst.find(type->name);
        if (it != subst.end())
            return it->second;
    }

    // 置換対象（element_type・type_args・関数型のparam_types/return_type）を含まないリーフはそのまま共有する。
    // クローンするとUnionType等の派生ノードがスライスされ変種情報を失うため、コピー自体を避ける
    if (!type->element_type && type->type_args.empty() && type->param_types.empty() &&
        !type->return_type)
        return type;

    auto result = std::make_shared<Type>(*type);
    if (type->element_type)
        result->element_type = substitute_type_params(type->element_type, subst);
    for (auto& arg : result->type_args)
        arg = substitute_type_params(arg, subst);
    // 関数ポインタ型（int*(T, T)等）の引数・戻り値も置換する。
    // 従来は未置換のまま共有され、ジェネリック関数/メソッドの関数ポインタ引数経由の間接呼び出しが
    // LLVM検証エラー（Call parameter type does not match）になっていた
    for (auto& pt : result->param_types)
        pt = substitute_type_params(pt, subst);
    if (type->return_type)
        result->return_type = substitute_type_params(type->return_type, subst);

    // "Box<T>" 表記が名前に残っている場合は基底名へ正規化する（type_argsが真実）
    if (!result->type_args.empty()) {
        auto lt = result->name.find('<');
        if (lt != std::string::npos)
            result->name = result->name.substr(0, lt);
    }
    return result;
}

}  // namespace cm::ast
