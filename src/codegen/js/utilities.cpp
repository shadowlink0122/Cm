#include "builtins.hpp"
#include "codegen.hpp"
#include "runtime.hpp"
#include "types.hpp"

#include <algorithm>
#include <iostream>

namespace cm::codegen::js {

using ast::TypeKind;

bool JSCodeGen::isCssStruct(const std::string& struct_name) const {
    auto it = struct_map_.find(struct_name);
    return it != struct_map_.end() && it->second && it->second->is_css;
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
