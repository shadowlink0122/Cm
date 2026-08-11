// 型キーの可逆エンコーディング実装（仕様は typekey.hpp を参照）

#include "typekey.hpp"

#include "typedef.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cm::ast::typekey {

namespace {

// プリミティブ型の正規名（エンコードにそのまま使う）
std::optional<std::string> canonical_primitive_name(ast::TypeKind kind) {
    switch (kind) {
        case ast::TypeKind::Void:
            return "void";
        case ast::TypeKind::Bool:
            return "bool";
        case ast::TypeKind::Tiny:
            return "tiny";
        case ast::TypeKind::UTiny:
            return "utiny";
        case ast::TypeKind::Short:
            return "short";
        case ast::TypeKind::UShort:
            return "ushort";
        case ast::TypeKind::Int:
            return "int";
        case ast::TypeKind::UInt:
            return "uint";
        case ast::TypeKind::Long:
            return "long";
        case ast::TypeKind::ULong:
            return "ulong";
        case ast::TypeKind::ISize:
            return "isize";
        case ast::TypeKind::USize:
            return "usize";
        case ast::TypeKind::Float:
            return "float";
        case ast::TypeKind::UFloat:
            return "ufloat";
        case ast::TypeKind::Double:
            return "double";
        case ast::TypeKind::UDouble:
            return "udouble";
        case ast::TypeKind::Char:
            return "char";
        case ast::TypeKind::String:
            return "string";
        case ast::TypeKind::CString:
            return "cstring";
        default:
            return std::nullopt;
    }
}

// 正規名からプリミティブ型を復元する（該当しなければ nullptr）
ast::TypePtr make_primitive_from_name(const std::string& name) {
    struct Entry {
        const char* name;
        ast::TypeKind kind;
    };
    static const Entry kTable[] = {
        {"void", ast::TypeKind::Void},       {"bool", ast::TypeKind::Bool},
        {"tiny", ast::TypeKind::Tiny},       {"utiny", ast::TypeKind::UTiny},
        {"short", ast::TypeKind::Short},     {"ushort", ast::TypeKind::UShort},
        {"int", ast::TypeKind::Int},         {"uint", ast::TypeKind::UInt},
        {"long", ast::TypeKind::Long},       {"ulong", ast::TypeKind::ULong},
        {"isize", ast::TypeKind::ISize},     {"usize", ast::TypeKind::USize},
        {"float", ast::TypeKind::Float},     {"ufloat", ast::TypeKind::UFloat},
        {"double", ast::TypeKind::Double},   {"udouble", ast::TypeKind::UDouble},
        {"char", ast::TypeKind::Char},       {"string", ast::TypeKind::String},
        {"cstring", ast::TypeKind::CString},
    };
    for (const auto& e : kTable) {
        if (name == e.name) {
            auto t = std::make_shared<ast::Type>(e.kind);
            t->name = e.name;
            return t;
        }
    }
    return nullptr;
}

// キー全体を1つの型としてデコードする（不正な形式は nullptr）
ast::TypePtr decode_whole(std::string_view s);

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

ast::TypePtr decode_whole(std::string_view s) {
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
            return marker == 'P' ? ast::make_pointer(elem) : ast::make_reference(elem);
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
                return ast::make_array(elem, static_cast<uint32_t>(*size));
            return ast::make_array(elem);
        }
        if (marker == 'U') {
            // "$U" 変種個数 '$' の後に各変種を「<長さ>'$'<エンコード>」で連結（変種順=タグ順）
            size_t pos = 2;
            auto argc = read_number(s, pos);
            if (!argc || pos >= s.size() || s[pos] != '$')
                return nullptr;
            ++pos;
            std::vector<ast::TypePtr> variants;
            for (size_t i = 0; i < *argc; ++i) {
                auto len = read_number(s, pos);
                if (!len || pos >= s.size() || s[pos] != '$')
                    return nullptr;
                ++pos;
                if (pos + *len > s.size())
                    return nullptr;
                auto v = decode_whole(s.substr(pos, *len));
                if (!v)
                    return nullptr;
                variants.push_back(v);
                pos += *len;
            }
            if (pos != s.size())
                return nullptr;
            auto t = std::make_shared<ast::Type>(ast::TypeKind::Union);
            t->type_args = std::move(variants);
            return t;
        }
        return nullptr;
    }

    // 基底名を '$' 直前まで読み取る
    auto dollar = s.find('$');
    if (dollar == std::string_view::npos) {
        std::string name(s);
        if (auto prim = make_primitive_from_name(name))
            return prim;
        return ast::make_named(name);
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

    std::vector<ast::TypePtr> args;
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

    auto t = ast::make_named(base);
    t->type_args = std::move(args);
    return t;
}

}  // namespace

std::string encode_type_key(const ast::TypePtr& type) {
    if (!type)
        return "";

    switch (type->kind) {
        case ast::TypeKind::Pointer:
            return "$P" + (type->element_type ? encode_type_key(type->element_type) : "void");
        case ast::TypeKind::Reference:
            return "$R" + (type->element_type ? encode_type_key(type->element_type) : "void");
        case ast::TypeKind::Array: {
            std::string size_str =
                type->array_size ? std::to_string(*type->array_size) : std::string();
            return "$A" + size_str + "$" +
                   (type->element_type ? encode_type_key(type->element_type) : "void");
        }
        case ast::TypeKind::Union: {
            // 変種構造でエンコードする（typedef名・表示名によらず同一ユニオンは単一キーへ収束）。
            // 変種はtype_args形式とUnionType::variants形式の両対応（union_variant_types）。変種未解決は名前ベースへフォールバック
            auto variants = ast::union_variant_types(type);
            if (!variants.empty()) {
                std::string out = "$U" + std::to_string(variants.size()) + "$";
                for (const auto& v : variants) {
                    std::string enc = v ? encode_type_key(v) : "void";
                    out += std::to_string(enc.size()) + "$" + enc;
                }
                return out;
            }
            break;
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

std::string make_struct_key(const std::string& base_name, const std::vector<ast::TypePtr>& args) {
    if (args.empty())
        return base_name;
    std::string out = base_name + "$" + std::to_string(args.size()) + "$";
    for (const auto& arg : args) {
        std::string enc = arg ? encode_type_key(arg) : "void";
        out += std::to_string(enc.size()) + "$" + enc;
    }
    return out;
}

ast::TypePtr decode_type_key(const std::string& key) {
    return decode_whole(key);
}

bool is_encoded_key(const std::string& key) {
    return key.find('$') != std::string::npos;
}

std::string arg_key_from_tree(const ast::TypePtr& arg) {
    if (!arg) {
        return "void";
    }
    switch (arg->kind) {
        case ast::TypeKind::Pointer:
            return "ptr_" + arg_key_from_tree(arg->element_type);
        case ast::TypeKind::Reference:
            return "$R" + arg_key_from_tree(arg->element_type);
        case ast::TypeKind::Array: {
            std::string size_str = arg->array_size ? std::to_string(*arg->array_size) : "";
            return "$A" + size_str + "$" + arg_key_from_tree(arg->element_type);
        }
        case ast::TypeKind::Union:
            // ユニオン型引数は変種構造の$Uキー（typedef名と表示名の分裂を防ぐ）。変種未解決は名前へフォールバック
            if (!ast::union_variant_types(arg).empty()) {
                return encode_type_key(arg);
            }
            break;
        default:
            break;
    }
    if (arg->is_primitive()) {
        return encode_type_key(arg);
    }
    std::string base = arg->name;
    auto lt = base.find('<');
    if (lt != std::string::npos) {
        base = base.substr(0, lt);
    }
    if (arg->type_args.empty()) {
        return base.empty() ? encode_type_key(arg) : base;
    }
    if (base.find("__") != std::string::npos || base.find('$') != std::string::npos) {
        return base;
    }
    return struct_key_from_tree(base, arg->type_args);
}

std::string struct_key_from_tree(const std::string& base_name,
                                 const std::vector<ast::TypePtr>& type_args) {
    if (type_args.empty()) {
        return base_name;
    }
    std::vector<std::string> keys;
    keys.reserve(type_args.size());
    for (const auto& arg : type_args) {
        keys.push_back(arg_key_from_tree(arg));
    }
    std::string out = base_name + "$" + std::to_string(keys.size()) + "$";
    for (const auto& k : keys) {
        out += std::to_string(k.size()) + "$" + k;
    }
    return out;
}

std::string spec_fn_prefix(const std::string& struct_key) {
    if (!is_encoded_key(struct_key)) {
        return struct_key;
    }
    const std::string base = base_name_of(struct_key);
    auto args = decode_type_args(struct_key);
    if (args.empty()) {
        return struct_key;
    }
    std::string out = base;
    for (const auto& a : args) {
        out += "__" + arg_key_from_tree(a);
    }
    return out;
}

std::string fn_prefix_from_tree(const ast::Type& type) {
    // 型引数の無い型は名前をそのまま関数名接頭辞とする（表示形<...>だけの名前は基底へ切る）
    if (type.type_args.empty()) {
        auto lt = type.name.find('<');
        return lt == std::string::npos ? type.name : type.name.substr(0, lt);
    }
    std::string base = type.name;
    auto lt = base.find('<');
    if (lt != std::string::npos) {
        base = base.substr(0, lt);
    }
    base = spec_base_name(base);
    return spec_fn_prefix(struct_key_from_tree(base, type.type_args));
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

std::vector<ast::TypePtr> decode_type_args(const std::string& key) {
    if (!is_encoded_key(key) || (!key.empty() && key[0] == '$'))
        return {};
    auto decoded = decode_type_key(key);
    if (!decoded)
        return {};
    return decoded->type_args;
}

std::string display_name(const ast::TypePtr& type) {
    if (!type)
        return "";
    switch (type->kind) {
        case ast::TypeKind::Pointer:
            return "*" + display_name(type->element_type);
        case ast::TypeKind::Reference:
            return "&" + display_name(type->element_type);
        case ast::TypeKind::Array: {
            std::string size_str = type->array_size ? std::to_string(*type->array_size) : "";
            return display_name(type->element_type) + "[" + size_str + "]";
        }
        case ast::TypeKind::Union: {
            // 変種を " | " で連結（typedef名があればそちらを優先して可読にする）
            if (!type->name.empty() && type->name.find('|') == std::string::npos) {
                return type->name;
            }
            auto variants = ast::union_variant_types(type);
            if (!variants.empty()) {
                std::string out;
                for (size_t i = 0; i < variants.size(); ++i) {
                    if (i > 0)
                        out += " | ";
                    out += display_name(variants[i]);
                }
                return out;
            }
            break;
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

}  // namespace cm::ast::typekey
