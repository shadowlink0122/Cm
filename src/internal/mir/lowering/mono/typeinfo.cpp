// 単相化 - 型名の正規化・特殊化名の生成・特殊化型のサイズ/アライメント計算

#include "internal/base/debug.hpp"
#include "internal/base/target.hpp"
#include "internal/mir/lowering/mono/typekey.hpp"
#include "internal/mir/lowering/mono_internal.hpp"
#include "internal/mir/lowering/monomorphization.hpp"
#include "internal/mir/lowering/monomorphization_utils.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cm::mir {

// ポインタ型名を正規化
std::string Monomorphization::normalize_type_arg(const std::string& type_arg) {
    if (type_arg.empty())
        return type_arg;

    // *intのような形式をptr_intに変換
    if (type_arg[0] == '*') {
        return "ptr_" + normalize_type_arg(type_arg.substr(1));
    }

    // ネストジェネリクス対応
    auto lt_pos = type_arg.find('<');
    if (lt_pos != std::string::npos) {
        auto gt_pos = type_arg.rfind('>');
        if (gt_pos != std::string::npos && gt_pos > lt_pos) {
            std::string base_name = type_arg.substr(0, lt_pos);
            std::string type_args_str = type_arg.substr(lt_pos + 1, gt_pos - lt_pos - 1);

            // カンマで分割（ネストを考慮）
            std::vector<std::string> type_args;
            int depth = 0;
            size_t start = 0;
            for (size_t i = 0; i < type_args_str.size(); ++i) {
                if (type_args_str[i] == '<') {
                    depth++;
                } else if (type_args_str[i] == '>') {
                    depth--;
                } else if (type_args_str[i] == ',' && depth == 0) {
                    std::string arg = type_args_str.substr(start, i - start);
                    while (!arg.empty() && arg.front() == ' ')
                        arg.erase(0, 1);
                    while (!arg.empty() && arg.back() == ' ')
                        arg.pop_back();
                    type_args.push_back(arg);
                    start = i + 1;
                }
            }
            std::string last_arg = type_args_str.substr(start);
            while (!last_arg.empty() && last_arg.front() == ' ')
                last_arg.erase(0, 1);
            while (!last_arg.empty() && last_arg.back() == ' ')
                last_arg.pop_back();
            if (!last_arg.empty()) {
                type_args.push_back(last_arg);
            }

            std::string result = base_name;
            for (const auto& arg : type_args) {
                result += "__" + normalize_type_arg(arg);
            }
            return result;
        }
    }

    return type_arg;
}

// 型名から特殊化構造体名を生成
std::string Monomorphization::make_specialized_struct_name(
    const std::string& base_name, const std::vector<std::string>& type_args) {
    std::string result = base_name;
    for (const auto& arg : type_args) {
        result += "__" + normalize_type_arg(arg);
    }
    return result;
}

// 型名から特殊化関数名を生成
std::string Monomorphization::make_specialized_name(const std::string& base_name,
                                                    const std::vector<std::string>& type_args) {
    auto pos = base_name.find("<");
    auto end_pos = base_name.find(">__");

    if (pos != std::string::npos && end_pos != std::string::npos && !type_args.empty()) {
        std::string prefix = base_name.substr(0, pos);
        std::string suffix = base_name.substr(end_pos + 1);

        std::string args_str;
        for (size_t i = 0; i < type_args.size(); ++i) {
            args_str += "__" + normalize_type_arg(type_args[i]);
        }

        return prefix + args_str + suffix;
    }

    std::string result = base_name;
    for (const auto& arg : type_args) {
        result += "__" + normalize_type_arg(arg);
    }
    return result;
}

// インターフェース型かチェック
bool Monomorphization::is_interface_type(const std::string& type_name) const {
    return interface_names.count(type_name) > 0;
}

// 型引数1個分のシンボルキーを生成（既存の__フラット規約を維持する）
std::string Monomorphization::arg_symbol_key(const hir::TypePtr& arg) const {
    if (!arg)
        return "void";
    switch (arg->kind) {
        case hir::TypeKind::Pointer:
            // 既存規約の ptr_xxx 形式を維持する
            return "ptr_" + arg_symbol_key(arg->element_type);
        case hir::TypeKind::Reference:
            return "$R" + arg_symbol_key(arg->element_type);
        case hir::TypeKind::Array: {
            std::string size_str = arg->array_size ? std::to_string(*arg->array_size) : "";
            return "$A" + size_str + "$" + arg_symbol_key(arg->element_type);
        }
        default:
            break;
    }
    if (arg->is_primitive())
        return typekey::encode_type_key(arg);

    std::string base = arg->name;
    auto lt = base.find('<');
    if (lt != std::string::npos)
        base = base.substr(0, lt);
    if (arg->type_args.empty())
        return base.empty() ? typekey::encode_type_key(arg) : base;
    // 既にマングリング済みの名前（__や$を含む）はそのままシンボルキーとして扱う
    if (base.find("__") != std::string::npos || base.find('$') != std::string::npos)
        return base;
    return struct_symbol_key(base, arg->type_args);
}

// 特殊化構造体のシンボルキーを生成する（仕様はヘッダのコメントを参照）
std::string Monomorphization::struct_symbol_key(const std::string& base_name,
                                                const std::vector<hir::TypePtr>& type_args) const {
    if (type_args.empty())
        return base_name;

    std::vector<std::string> keys;
    bool simple = true;
    for (const auto& arg : type_args) {
        keys.push_back(arg_symbol_key(arg));
        if (keys.back().find('$') != std::string::npos) {
            simple = false;
        }
    }

    if (simple) {
        std::string flat = base_name;
        for (const auto& k : keys)
            flat += "__" + k;
        // C8: フラット名がユーザー定義構造体と同名になる場合のみエンコード名へ退避する
        if (!hir_struct_defs || hir_struct_defs->find(flat) == hir_struct_defs->end())
            return flat;
    }

    std::string out = base_name + "$" + std::to_string(keys.size()) + "$";
    for (const auto& k : keys)
        out += std::to_string(k.size()) + "$" + k;
    return out;
}

// フラット特殊化名の残り部分（base__以降）を型引数ツリーへ復元する
std::vector<hir::TypePtr> Monomorphization::parse_flat_type_args(
    const std::string& base_name, const std::string& remainder) const {
    // 基底の型パラメータ数を取得（不明なら各セグメントを個別引数とみなす）
    size_t param_count = 0;
    if (hir_struct_defs) {
        auto it = hir_struct_defs->find(base_name);
        if (it != hir_struct_defs->end() && it->second)
            param_count = it->second->generic_params.size();
    }

    // __でセグメント分割
    std::vector<std::string> segments;
    size_t pos = 0;
    while (pos < remainder.size()) {
        auto next = remainder.find("__", pos);
        if (next == std::string::npos) {
            segments.push_back(remainder.substr(pos));
            break;
        }
        segments.push_back(remainder.substr(pos, next - pos));
        pos = next + 2;
    }

    std::vector<hir::TypePtr> args;
    if (param_count == 1 && segments.size() > 1) {
        // ネスト特殊化（Vector__Vector__int等）: 全セグメントを1引数として結合する
        std::string joined;
        for (const auto& s : segments) {
            if (!joined.empty())
                joined += "__";
            joined += s;
        }
        args.push_back(make_type_from_name(joined));
    } else {
        for (const auto& s : segments)
            args.push_back(make_type_from_name(s));
    }
    return args;
}

// 型パラメータを実引数ツリーで置換する（名前の平坦化を行わず構造を保つ）
hir::TypePtr Monomorphization::substitute_type_tree(
    const hir::TypePtr& type, const std::unordered_map<std::string, hir::TypePtr>& subst) const {
    if (!type)
        return nullptr;

    if (!type->name.empty()) {
        auto it = subst.find(type->name);
        if (it != subst.end())
            return it->second;
    }

    auto result = std::make_shared<hir::Type>(*type);
    if (type->element_type)
        result->element_type = substitute_type_tree(type->element_type, subst);
    for (auto& arg : result->type_args)
        arg = substitute_type_tree(arg, subst);

    // "Box<T>" 表記が名前に残っている場合は基底名へ正規化する（type_argsが真実）
    if (!result->type_args.empty()) {
        auto lt = result->name.find('<');
        if (lt != std::string::npos)
            result->name = result->name.substr(0, lt);
    }
    return result;
}

// 型ツリー内に未解決のジェネリック型パラメータが残っているか
bool Monomorphization::tree_has_generic_param(const hir::TypePtr& type) const {
    if (!type)
        return false;
    if (type->kind == hir::TypeKind::Generic && type->type_args.empty())
        return true;
    if (all_generic_param_names.count(type->name) > 0 && type->type_args.empty() &&
        !type->is_primitive() && type->kind != hir::TypeKind::Pointer &&
        type->kind != hir::TypeKind::Array)
        return true;
    if (type->element_type && tree_has_generic_param(type->element_type))
        return true;
    for (const auto& arg : type->type_args) {
        if (tree_has_generic_param(arg))
            return true;
    }
    return false;
}

// 特殊化名から置換済みフィールド型を復元する。
// 完全一致（ユーザー定義・ジェネリック基底）、'$'エンコード名、フラット特殊化名の順で解決する
std::optional<std::vector<hir::TypePtr>> Monomorphization::resolve_struct_field_types(
    const hir::TypePtr& type) const {
    if (!type || !hir_struct_defs)
        return std::nullopt;

    const hir::HirStruct* st = nullptr;
    std::vector<hir::TypePtr> args = type->type_args;
    const std::string& name = type->name;

    // 1. 完全一致（ユーザー定義構造体・ジェネリック基底名）
    auto it = hir_struct_defs->find(name);
    if (it != hir_struct_defs->end())
        st = it->second;

    // 2. '$'エンコード名（Box$1$3$int等）から基底名と型引数を復元
    if (!st && typekey::is_encoded_key(name)) {
        std::string base = typekey::base_name_of(name);
        it = hir_struct_defs->find(base);
        if (it != hir_struct_defs->end()) {
            st = it->second;
            if (args.empty())
                args = typekey::decode_type_args(name);
        }
    }

    // 3. フラット特殊化名（Node__int等）から基底名と型引数を復元
    if (!st && name.find("__") != std::string::npos) {
        std::string base = name.substr(0, name.find("__"));
        it = hir_struct_defs->find(base);
        if (it != hir_struct_defs->end() && it->second && !it->second->generic_params.empty()) {
            st = it->second;
            if (args.empty())
                args = parse_flat_type_args(base, name.substr(base.size() + 2));
        }
    }

    if (!st)
        return std::nullopt;

    std::unordered_map<std::string, hir::TypePtr> subst;
    for (size_t i = 0; i < st->generic_params.size() && i < args.size(); ++i)
        subst[st->generic_params[i].name] = args[i];

    std::vector<hir::TypePtr> fields;
    for (const auto& f : st->fields)
        fields.push_back(subst.empty() ? f.type : substitute_type_tree(f.type, subst));
    return fields;
}

// 特殊化された型のサイズを計算
// フィールドのアライメントを計算する（calculate_specialized_type_size と対で使用）
int64_t Monomorphization::calculate_specialized_type_align(const hir::TypePtr& type) const {
    if (!type)
        return 8;
    switch (type->kind) {
        case hir::TypeKind::Bool:
        case hir::TypeKind::Tiny:
        case hir::TypeKind::UTiny:
        case hir::TypeKind::Char:
            return 1;
        case hir::TypeKind::Short:
        case hir::TypeKind::UShort:
            return 2;
        case hir::TypeKind::Int:
        case hir::TypeKind::UInt:
        case hir::TypeKind::Float:
        case hir::TypeKind::UFloat:
            return 4;
        case hir::TypeKind::Pointer:
        case hir::TypeKind::Reference:
        case hir::TypeKind::String:
            // ポインタ幅はターゲット依存（wasm32/baremetal-armは4）
            return cm::target_pointer_size();
        case hir::TypeKind::Struct: {
            // 特殊化名・型引数を考慮した置換済みフィールド型で計算する（sizeと同一基準）
            auto fields = resolve_struct_field_types(type);
            if (fields) {
                int64_t max_align = 1;
                for (const auto& f : *fields) {
                    max_align = std::max(max_align, calculate_specialized_type_align(f));
                }
                return max_align;
            }
            return 8;
        }
        case hir::TypeKind::Array:
            return calculate_specialized_type_align(type->element_type);
        default:
            return 8;
    }
}

int64_t Monomorphization::calculate_specialized_type_size(const hir::TypePtr& type) const {
    if (!type)
        return 8;

    auto align_to = [](int64_t offset, int64_t align) {
        return (offset + align - 1) / align * align;
    };

    switch (type->kind) {
        case hir::TypeKind::Bool:
        case hir::TypeKind::Tiny:
        case hir::TypeKind::UTiny:
        case hir::TypeKind::Char:
            return 1;
        case hir::TypeKind::Short:
        case hir::TypeKind::UShort:
            return 2;
        case hir::TypeKind::Int:
        case hir::TypeKind::UInt:
        case hir::TypeKind::Float:
        case hir::TypeKind::UFloat:
            return 4;
        case hir::TypeKind::Long:
        case hir::TypeKind::ULong:
        case hir::TypeKind::Double:
        case hir::TypeKind::UDouble:
            return 8;
        case hir::TypeKind::Pointer:
        case hir::TypeKind::Reference:
        case hir::TypeKind::String:
            // ポインタ幅はターゲット依存（wasm32/baremetal-armは4）
            return cm::target_pointer_size();
        case hir::TypeKind::Struct: {
            // 自然アライメントのCレイアウトで計算する（従来のフィールド数×8はcodegenの実レイアウトとずれ、memcpyサイズ・要素ストライドの不一致で隣接データを破壊していた）
            // 特殊化名（フラット名・'$'エンコード名）は型引数を復元し置換済みフィールドで計算する（C9）
            auto fields = resolve_struct_field_types(type);
            if (fields) {
                int64_t offset = 0;
                int64_t max_align = 1;
                for (const auto& f : *fields) {
                    int64_t fa = calculate_specialized_type_align(f);
                    max_align = std::max(max_align, fa);
                    offset = align_to(offset, fa) + calculate_specialized_type_size(f);
                }
                int64_t size = align_to(offset, max_align);
                return size > 0 ? size : 8;
            }
            return 8;
        }
        case hir::TypeKind::Array:
            if (type->element_type && type->array_size.has_value()) {
                return calculate_specialized_type_size(type->element_type) *
                       type->array_size.value();
            }
            return 8;
        default:
            return 8;
    }
}

}  // namespace cm::mir
