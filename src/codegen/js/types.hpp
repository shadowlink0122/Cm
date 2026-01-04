#pragma once

#include "../../frontend/ast/types.hpp"
#include "../../hir/types.hpp"

#include <string>
#include <unordered_set>

namespace cm::codegen::js {

using ast::TypeKind;

// JSの予約語
inline const std::unordered_set<std::string>& getJSReservedWords() {
    static const std::unordered_set<std::string> reserved = {
        "break",      "case",    "catch",   "continue",  "debugger", "default",    "delete",
        "do",         "else",    "finally", "for",       "function", "if",         "in",
        "instanceof", "new",     "return",  "switch",    "this",     "throw",      "try",
        "typeof",     "var",     "void",    "while",     "with",     "class",      "const",
        "enum",       "export",  "extends", "import",    "super",    "implements", "interface",
        "let",        "package", "private", "protected", "public",   "static",     "yield",
        "undefined",  "null",    "true",    "false",     "NaN",      "Infinity"};
    return reserved;
}

// 識別子をJS用にサニタイズ
inline std::string sanitizeIdentifier(const std::string& name) {
    std::string result = name;

    // "@" を "_at_" に置換
    size_t pos;
    while ((pos = result.find("@")) != std::string::npos) {
        result.replace(pos, 1, "_at_");
    }

    // "::" を "__" に置換
    while ((pos = result.find("::")) != std::string::npos) {
        result.replace(pos, 2, "__");
    }

    // "<", ">", ",", " " を "_" に置換
    const std::string unsafe = "<>, ";
    for (char c : unsafe) {
        while ((pos = result.find(c)) != std::string::npos) {
            result.replace(pos, 1, "_");
        }
    }

    // 予約語の場合はプレフィックスを追加
    if (getJSReservedWords().count(result)) {
        result = "_cm_" + result;
    }

    return result;
}

// 文字列をエスケープ
inline std::string escapeString(const std::string& str) {
    std::string result;
    for (char c : str) {
        switch (c) {
            case '\"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                result += c;
        }
    }
    return result;
}

// JS型名を取得
inline std::string jsTypeName(const hir::Type& type) {
    if (type.is_integer() || type.is_floating()) {
        return "number";
    }
    switch (type.kind) {
        case TypeKind::Bool:
            return "boolean";
        case TypeKind::Char:
        case TypeKind::String:
        case TypeKind::CString:
            return "string";
        case TypeKind::Struct:
        case TypeKind::Interface:
            return "object";
        case TypeKind::Array:
        case TypeKind::Pointer:
            return "Array";
        default:
            return "any";
    }
}

// JS型のデフォルト値を取得
inline std::string jsDefaultValue(const hir::Type& type) {
    if (type.is_integer()) {
        return "0";
    }
    if (type.is_floating()) {
        return "0.0";
    }
    switch (type.kind) {
        case TypeKind::Bool:
            return "false";
        case TypeKind::Char:
        case TypeKind::String:
        case TypeKind::CString:
            return "\"\"";
        case TypeKind::Struct:
            return "{}";
        case TypeKind::Interface:
            // インターフェース型はfat object {data, vtable}としてnullで初期化
            return "{data: null, vtable: null}";
        case TypeKind::Array:
            // 配列の要素型に応じた初期化
            if (type.array_size && *type.array_size > 0 && type.element_type) {
                std::string elemDefault = jsDefaultValue(*type.element_type);
                if (type.element_type->kind == TypeKind::Struct) {
                    // 構造体の配列：各要素をコンストラクタで初期化
                    return "Array.from({length: " + std::to_string(*type.array_size) + "}, () => " +
                           elemDefault + ")";
                } else {
                    // プリミティブ型の配列：fillで初期化
                    return "Array(" + std::to_string(*type.array_size) + ").fill(" + elemDefault +
                           ")";
                }
            }
            return "[]";
        case TypeKind::Pointer:
        case TypeKind::Reference:
            return "null";
        default:
            return "null";
    }
}

}  // namespace cm::codegen::js
