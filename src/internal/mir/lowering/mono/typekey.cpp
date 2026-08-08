// 型キーの可逆エンコーディング実装（仕様は typekey.hpp を参照）

#include "typekey.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cm::mir::typekey {

namespace {

// プリミティブ型の正規名（エンコードにそのまま使う）
std::optional<std::string> canonical_primitive_name(hir::TypeKind kind) {
    switch (kind) {
        case hir::TypeKind::Void:
            return "void";
        case hir::TypeKind::Bool:
            return "bool";
        case hir::TypeKind::Tiny:
            return "tiny";
        case hir::TypeKind::UTiny:
            return "utiny";
        case hir::TypeKind::Short:
            return "short";
        case hir::TypeKind::UShort:
            return "ushort";
        case hir::TypeKind::Int:
            return "int";
        case hir::TypeKind::UInt:
            return "uint";
        case hir::TypeKind::Long:
            return "long";
        case hir::TypeKind::ULong:
            return "ulong";
        case hir::TypeKind::ISize:
            return "isize";
        case hir::TypeKind::USize:
            return "usize";
        case hir::TypeKind::Float:
            return "float";
        case hir::TypeKind::UFloat:
            return "ufloat";
        case hir::TypeKind::Double:
            return "double";
        case hir::TypeKind::UDouble:
            return "udouble";
        case hir::TypeKind::Char:
            return "char";
        case hir::TypeKind::String:
            return "string";
        case hir::TypeKind::CString:
            return "cstring";
        default:
            return std::nullopt;
    }
}

// 正規名からプリミティブ型を復元する（該当しなければ nullptr）
hir::TypePtr make_primitive_from_name(const std::string& name) {
    struct Entry {
        const char* name;
        hir::TypeKind kind;
    };
    static const Entry kTable[] = {
        {"void", hir::TypeKind::Void},       {"bool", hir::TypeKind::Bool},
        {"tiny", hir::TypeKind::Tiny},       {"utiny", hir::TypeKind::UTiny},
        {"short", hir::TypeKind::Short},     {"ushort", hir::TypeKind::UShort},
        {"int", hir::TypeKind::Int},         {"uint", hir::TypeKind::UInt},
        {"long", hir::TypeKind::Long},       {"ulong", hir::TypeKind::ULong},
        {"isize", hir::TypeKind::ISize},     {"usize", hir::TypeKind::USize},
        {"float", hir::TypeKind::Float},     {"ufloat", hir::TypeKind::UFloat},
        {"double", hir::TypeKind::Double},   {"udouble", hir::TypeKind::UDouble},
        {"char", hir::TypeKind::Char},       {"string", hir::TypeKind::String},
        {"cstring", hir::TypeKind::CString},
    };
    for (const auto& e : kTable) {
        if (name == e.name) {
            auto t = std::make_shared<hir::Type>(e.kind);
            t->name = e.name;
            return t;
        }
    }
    return nullptr;
}

// キー全体を1つの型としてデコードする（不正な形式は nullptr）
hir::TypePtr decode_whole(std::string_view s);

// 数字列を読み取る（数字が無ければ nullopt）
std::optional<size_t> read_number(std::string_view s, size_t& pos) {
    size_t start = pos;
    size_t value = 0;
    while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
        value = value * 10 + static_cast<size_t>(s[pos] - '0');
        ++pos;
    }
    if (pos == start)
        return std::nullopt;
    return value;
}

hir::TypePtr decode_whole(std::string_view s) {
    if (s.empty())
        return nullptr;

    // マーカー付き派生型（ポインタ・参照・配列）
    if (s[0] == '$') {
        if (s.size() < 2)
            return nullptr;
        char marker = s[1];
        if (marker == 'P' || marker == 'R') {
            auto elem = decode_whole(s.substr(2));
            if (!elem)
                return nullptr;
            return marker == 'P' ? hir::make_pointer(elem) : hir::make_reference(elem);
        }
        if (marker == 'A') {
            size_t pos = 2;
            auto size = read_number(s, pos);
            if (pos >= s.size() || s[pos] != '$')
                return nullptr;
            ++pos;
            auto elem = decode_whole(s.substr(pos));
            if (!elem)
                return nullptr;
            if (size)
                return hir::make_array(elem, static_cast<uint32_t>(*size));
            return hir::make_array(elem);
        }
        return nullptr;
    }

    // 基底名を '$' 直前まで読み取る
    auto dollar = s.find('$');
    if (dollar == std::string_view::npos) {
        std::string name(s);
        if (auto prim = make_primitive_from_name(name))
            return prim;
        return hir::make_named(name);
    }

    std::string base(s.substr(0, dollar));
    if (base.empty())
        return nullptr;

    // 引数個数と各引数（<長さ>'$'<エンコード>）を読み取る
    size_t pos = dollar + 1;
    auto argc = read_number(s, pos);
    if (!argc || pos >= s.size() || s[pos] != '$')
        return nullptr;
    ++pos;

    std::vector<hir::TypePtr> args;
    for (size_t i = 0; i < *argc; ++i) {
        auto len = read_number(s, pos);
        if (!len || pos >= s.size() || s[pos] != '$')
            return nullptr;
        ++pos;
        if (pos + *len > s.size())
            return nullptr;
        auto arg = decode_whole(s.substr(pos, *len));
        if (!arg)
            return nullptr;
        args.push_back(arg);
        pos += *len;
    }

    // 末尾に余りがあれば不正
    if (pos != s.size())
        return nullptr;

    auto t = hir::make_named(base);
    t->type_args = std::move(args);
    return t;
}

}  // namespace

std::string encode_type_key(const hir::TypePtr& type) {
    if (!type)
        return "";

    switch (type->kind) {
        case hir::TypeKind::Pointer:
            return "$P" + (type->element_type ? encode_type_key(type->element_type) : "void");
        case hir::TypeKind::Reference:
            return "$R" + (type->element_type ? encode_type_key(type->element_type) : "void");
        case hir::TypeKind::Array: {
            std::string size_str =
                type->array_size ? std::to_string(*type->array_size) : std::string();
            return "$A" + size_str + "$" +
                   (type->element_type ? encode_type_key(type->element_type) : "void");
        }
        default:
            break;
    }

    if (auto prim = canonical_primitive_name(type->kind))
        return *prim;

    // 基底名: "Box<int>" のような表記が name に残っている場合は '<' より前を基底とする
    std::string base = type->name;
    auto lt = base.find('<');
    if (lt != std::string::npos)
        base = base.substr(0, lt);

    if (type->type_args.empty())
        return base;

    return make_struct_key(base, type->type_args);
}

std::string make_struct_key(const std::string& base_name, const std::vector<hir::TypePtr>& args) {
    if (args.empty())
        return base_name;
    std::string out = base_name + "$" + std::to_string(args.size()) + "$";
    for (const auto& arg : args) {
        std::string enc = arg ? encode_type_key(arg) : "void";
        out += std::to_string(enc.size()) + "$" + enc;
    }
    return out;
}

hir::TypePtr decode_type_key(const std::string& key) {
    return decode_whole(key);
}

bool is_encoded_key(const std::string& key) {
    return key.find('$') != std::string::npos;
}

std::string spec_base_name(const std::string& name) {
    if (is_encoded_key(name)) {
        return base_name_of(name);
    }
    auto us = name.find("__");
    if (us != std::string::npos && us > 0) {
        return name.substr(0, us);
    }
    return name;
}

std::string base_name_of(const std::string& key) {
    if (!key.empty() && key[0] == '$')
        return key;  // 派生型マーカーには基底名が無い
    auto dollar = key.find('$');
    if (dollar == std::string::npos)
        return key;
    return key.substr(0, dollar);
}

std::vector<hir::TypePtr> decode_type_args(const std::string& key) {
    if (!is_encoded_key(key) || (!key.empty() && key[0] == '$'))
        return {};
    auto decoded = decode_type_key(key);
    if (!decoded)
        return {};
    return decoded->type_args;
}

std::string display_name(const hir::TypePtr& type) {
    if (!type)
        return "";
    switch (type->kind) {
        case hir::TypeKind::Pointer:
            return "*" + display_name(type->element_type);
        case hir::TypeKind::Reference:
            return "&" + display_name(type->element_type);
        case hir::TypeKind::Array: {
            std::string size_str = type->array_size ? std::to_string(*type->array_size) : "";
            return display_name(type->element_type) + "[" + size_str + "]";
        }
        default:
            break;
    }
    if (auto prim = canonical_primitive_name(type->kind))
        return *prim;
    std::string base = type->name;
    auto lt = base.find('<');
    if (lt != std::string::npos)
        base = base.substr(0, lt);
    if (type->type_args.empty())
        return base;
    std::string out = base + "<";
    for (size_t i = 0; i < type->type_args.size(); ++i) {
        if (i > 0)
            out += ", ";
        out += display_name(type->type_args[i]);
    }
    out += ">";
    return out;
}

std::string display_name(const std::string& key) {
    if (!is_encoded_key(key))
        return key;
    auto decoded = decode_type_key(key);
    if (!decoded)
        return key;
    return display_name(decoded);
}

}  // namespace cm::mir::typekey
