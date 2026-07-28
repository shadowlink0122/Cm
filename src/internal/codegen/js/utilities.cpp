#include "builtins.hpp"
#include "codegen.hpp"
#include "runtime.hpp"
#include "types.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <unordered_set>

namespace cm::codegen::js {

using ast::TypeKind;

// Cmの型をTypeScriptの型注釈へ写像する。
// 数値は全てnumber、charも数値表現（ランタイムのchar表現に一致）、stringはstring、boolはboolean、
// 構造体・enumは名前（interface宣言を別途出力）、関数型は (a: number, ...) => R、配列はT[]、ポインタ/参照は指す先の型で近似する。
std::string JSCodeGen::tsType(const hir::Type* type) const {
    if (!type) {
        return "any";
    }
    switch (type->kind) {
        case TypeKind::Void:
            return "void";
        case TypeKind::Bool:
            return "boolean";
        case TypeKind::Tiny:
        case TypeKind::Short:
        case TypeKind::Int:
        case TypeKind::Long:
        case TypeKind::UTiny:
        case TypeKind::UShort:
        case TypeKind::UInt:
        case TypeKind::ULong:
        case TypeKind::ISize:
        case TypeKind::USize:
        case TypeKind::Float:
        case TypeKind::Double:
        case TypeKind::UFloat:
        case TypeKind::UDouble:
        case TypeKind::Char:
            return "number";
        case TypeKind::String:
        case TypeKind::CString:
            return "string";
        case TypeKind::Pointer:
        case TypeKind::Reference:
            // JS/TSにポインタは無い。ランタイム表現はfat pointerオブジェクト・配列decay・構造体参照が混在し単一の型に定まらないためanyで安全側に倒す
            return "any";
        case TypeKind::Array:
            return type->element_type ? (tsType(type->element_type.get()) + "[]") : "any[]";
        case TypeKind::Function: {
            std::string sig = "(";
            for (size_t i = 0; i < type->param_types.size(); ++i) {
                if (i > 0) {
                    sig += ", ";
                }
                sig += "a" + std::to_string(i) + ": " + tsType(type->param_types[i].get());
            }
            sig += ") => ";
            sig += type->return_type ? tsType(type->return_type.get()) : "void";
            return sig;
        }
        case TypeKind::Struct: {
            // export interface を実際に出力した素の構造体のみ型名として使う。
            // コンパイラ内部型（__TaggedUnion_*）・ジェネリックのマングリング名（Foo__int）・
            // ジェネリックパラメータ名（T等でstruct_map_に無いもの）はinterface宣言が無いためanyにする
            if (!type->name.empty() && struct_map_.count(type->name) > 0) {
                return sanitizeIdentifier(type->name);
            }
            return "any";
        }
        case TypeKind::Interface:
            // interface型はfat object {data, vtable}のランタイム表現でinterface宣言も出力しないためany
            return "any";
        case TypeKind::Generic:
            return "any";
        case TypeKind::TypeAlias:
            return type->element_type
                       ? tsType(type->element_type.get())
                       : (type->name.empty() ? "any" : sanitizeIdentifier(type->name));
        case TypeKind::Null:
            return "null";
        default:
            // Generic/Union/LiteralUnion/Inferred/Error等はanyで安全側に倒す
            return "any";
    }
}

std::string JSCodeGen::tsAnnotation(const hir::Type* type) const {
    if (!options_.emitTypeScript) {
        return "";
    }
    return ": " + tsType(type);
}

void JSCodeGen::emitStructInterface(const mir::MirStruct& st) {
    if (!options_.emitTypeScript) {
        return;
    }
    emitter_.emitLine("export interface " + sanitizeIdentifier(st.name) + " {");
    emitter_.increaseIndent();
    for (const auto& field : st.fields) {
        emitter_.emitLine(formatStructFieldKey(st, field.name) + tsAnnotation(field.type.get()) +
                          ";");
    }
    emitter_.decreaseIndent();
    emitter_.emitLine("}");
    emitter_.emitLine();
}

bool JSCodeGen::isCssStruct(const std::string& struct_name) const {
    auto it = struct_map_.find(struct_name);
    return it != struct_map_.end() && it->second && it->second->is_css;
}

bool JSCodeGen::structIsForeignObject(const std::string& struct_name) const {
    auto it = struct_map_.find(struct_name);
    if (it == struct_map_.end() || !it->second) {
        return false;
    }
    // 関数型フィールドを1つでも持つ構造体はJSメソッドを束ねた外部オブジェクトとみなす
    for (const auto& field : it->second->fields) {
        if (field.type && field.type->kind == ast::TypeKind::Function) {
            return true;
        }
    }
    return false;
}

std::unordered_set<std::string> JSCodeGen::collectUsedRuntimeHelpers(
    const std::string& code) const {
    std::unordered_set<std::string> used;
    const std::string prefix = "__cm_";

    size_t pos = 0;
    while (true) {
        pos = code.find(prefix, pos);
        if (pos == std::string::npos) {
            break;
        }
        size_t start = pos;
        size_t end = pos + prefix.size();
        while (end < code.size()) {
            char c = code[end];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                c == '_') {
                end++;
            } else {
                break;
            }
        }
        if (end > start) {
            used.insert(code.substr(start, end - start));
        }
        pos = end;
    }

    return used;
}

void JSCodeGen::expandRuntimeHelperDependencies(std::unordered_set<std::string>& used) const {
    if (used.count("__cm_web_set_html") || used.count("__cm_web_append_html")) {
        used.insert("__cm_dom_root");
    }
    if (used.count("__cm_output")) {
        used.insert("__cm_output_element");
    }
    if (used.count("__cm_format") || used.count("__cm_format_string")) {
        // __cm_formatのspec無し経路が数値整形に__cm_fmt_doubleを使う
        used.insert("__cm_fmt_double");
    }
}

std::string JSCodeGen::toKebabCase(const std::string& name) const {
    std::string result;
    result.reserve(name.size());
    for (char c : name) {
        result += (c == '_') ? '-' : c;
    }
    return result;
}

std::string JSCodeGen::formatStructFieldKey(const mir::MirStruct& st,
                                            const std::string& field_name) const {
    if (!st.is_css) {
        return sanitizeIdentifier(field_name);
    }
    std::string kebab = toKebabCase(field_name);
    return "\"" + escapeString(kebab) + "\"";
}

std::string JSCodeGen::mapExternJsName(const std::string& name) const {
    std::string result = name;
    std::replace(result.begin(), result.end(), '_', '.');
    return result;
}

std::string JSCodeGen::getStructDefaultValue(const hir::Type& type) const {
    if (type.kind != ast::TypeKind::Struct) {
        return jsDefaultValue(type);
    }
    auto it = struct_map_.find(type.name);
    if (it == struct_map_.end() || !it->second || it->second->fields.empty()) {
        return "{}";
    }
    const auto* mirStruct = it->second;
    std::string result = "{ ";
    for (size_t i = 0; i < mirStruct->fields.size(); ++i) {
        if (i > 0)
            result += ", ";
        std::string key = mirStruct->is_css
                              ? formatStructFieldKey(*mirStruct, mirStruct->fields[i].name)
                              : sanitizeIdentifier(mirStruct->fields[i].name);
        std::string val;
        if (mirStruct->fields[i].type && mirStruct->fields[i].type->kind == ast::TypeKind::Struct) {
            // ネスト構造体：再帰的にデフォルト値を生成
            val = getStructDefaultValue(*mirStruct->fields[i].type);
        } else if (mirStruct->fields[i].type) {
            val = jsDefaultValue(*mirStruct->fields[i].type);
        } else {
            val = "null";
        }
        result += key + ": " + val;
    }
    result += " }";
    return result;
}

}  // namespace cm::codegen::js
