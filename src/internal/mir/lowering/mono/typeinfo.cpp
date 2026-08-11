// 単相化 - 型名の正規化・特殊化名の生成・特殊化型のサイズ/アライメント計算

#include "internal/base/debug.hpp"
#include "internal/base/target.hpp"
#include "internal/mir/lowering/mono_internal.hpp"
#include "internal/mir/lowering/monomorphization.hpp"
#include "internal/mir/lowering/monomorphization_utils.hpp"
#include "internal/syntax/ast/typekey.hpp"

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
        return ast::typekey::encode_type_key(arg);

    std::string base = arg->name;
    auto lt = base.find('<');
    if (lt != std::string::npos)
        base = base.substr(0, lt);
    if (arg->type_args.empty())
        return base.empty() ? ast::typekey::encode_type_key(arg) : base;
    // $エンコード名は既に正準キー
    if (base.find('$') != std::string::npos)
        return base;
    // フラット特殊化名のリーフ（先行特殊化でローカル型名が具体名化されたもの。例: Vector__int）は
    // 可逆な型ツリーへ復号してから正準キーへ再エンコードする。
    // ユーザー定義の__入り名（C8: Box__Box__int等の実在struct）は復号せずそのまま扱う
    if (base.find("__") != std::string::npos) {
        if (hir_struct_defs && hir_struct_defs->find(base) != hir_struct_defs->end())
            return base;
        auto decoded = decode_type_name(base);
        if (decoded && !decoded->type_args.empty())
            return struct_symbol_key(decoded->name, decoded->type_args);
        return base;
    }
    return struct_symbol_key(base, arg->type_args);
}

// 特殊化構造体のシンボルキーを生成する（仕様はヘッダのコメントを参照）
// 特殊化引数ツリーの正準化（mono-flat-name-elimination③）。
// 先行特殊化で具体名化されたフラット/エンコード名のリーフ（name="Vector__int"・type_args空）を可逆復号して
// 構造化ツリー（name="Vector"・type_args=[int]）へ戻す。キー計算だけでなく置換に使うツリー自体を正準化しないと、
// 特殊化本体のパラメータ/ローカル型がフラット名リーフのままcodegenへ渡り、lookup欠落でフォールバック形状に落ちる。
// ユーザー定義の__入り同名struct（C8）は復号しない
hir::TypePtr Monomorphization::normalize_spec_arg_tree(const hir::TypePtr& t) const {
    if (!t)
        return t;
    if ((t->kind == hir::TypeKind::Struct || t->kind == hir::TypeKind::TypeAlias) &&
        t->type_args.empty() &&
        (t->name.find("__") != std::string::npos || ast::typekey::is_encoded_key(t->name)) &&
        (!hir_struct_defs || hir_struct_defs->find(t->name) == hir_struct_defs->end())) {
        auto decoded = decode_type_name(t->name);
        if (decoded && !decoded->type_args.empty()) {
            // 復号結果の引数にもリーフが残り得るため再帰的に正準化する
            for (auto& a : decoded->type_args)
                a = normalize_spec_arg_tree(a);
            return decoded;
        }
        return t;
    }
    bool changed = false;
    auto result = t;
    // type_args / element_type の再帰（clone-on-write）
    std::vector<hir::TypePtr> new_args;
    new_args.reserve(t->type_args.size());
    for (const auto& a : t->type_args) {
        auto na = normalize_spec_arg_tree(a);
        if (na != a)
            changed = true;
        new_args.push_back(na);
    }
    auto new_elem = t->element_type ? normalize_spec_arg_tree(t->element_type) : nullptr;
    if (new_elem != t->element_type)
        changed = true;
    // type_argsを持つのにnameがマングル済み/表示形のstaleなツリー（name="Vector__int"・args=[int]等）は、
    // nameを素の基底へ再建する（正準ツリー=素の基底名+構造化args。staleな名前は置換後のローカル型として
    // codegenへ漏れ、lookup欠落のフォールバック形状になる）
    std::string plain_base;
    if ((t->kind == hir::TypeKind::Struct || t->kind == hir::TypeKind::TypeAlias) &&
        !t->type_args.empty() &&
        (t->name.find("__") != std::string::npos || t->name.find('$') != std::string::npos ||
         t->name.find('<') != std::string::npos) &&
        (!hir_struct_defs || hir_struct_defs->find(t->name) == hir_struct_defs->end())) {
        plain_base = t->name;
        auto lt = plain_base.find('<');
        if (lt != std::string::npos)
            plain_base = plain_base.substr(0, lt);
        plain_base = ast::typekey::spec_base_name(plain_base);
        if (plain_base != t->name)
            changed = true;
    }
    if (changed) {
        result = std::make_shared<hir::Type>(*t);
        result->type_args = std::move(new_args);
        result->element_type = new_elem;
        if (!plain_base.empty())
            result->name = plain_base;
    }
    return result;
}

// 特殊化構造体のシンボルキーを生成する（$全面化＝mono-flat-name-elimination③）。
// 常に可逆な$長さ接頭辞エンコードを使う。フラット名（base__k1__k2）はネスト特殊化で本質的に曖昧
// （Box<Box<int>>とBox<Box,int>とユーザー定義Box__Box__intが衝突）であり、曖昧性の供給源だったフラット既定を廃止した
// （$は識別子に使えない文字のためユーザー名との衝突も構造的に消える）。
// 引数キーはdecode対応のarg_symbol_key（先行特殊化で具体名化されたフラットリーフを正準へ再エンコード）で計算する。
// 関数名ドメイン（base__argkey__method）へはspec_fn_prefixのドメイン橋が変換する
std::string Monomorphization::struct_symbol_key(const std::string& base_name,
                                                const std::vector<hir::TypePtr>& type_args) const {
    if (type_args.empty())
        return base_name;
    std::vector<std::string> keys;
    keys.reserve(type_args.size());
    for (const auto& arg : type_args)
        keys.push_back(arg_symbol_key(arg));
    std::string out = base_name + "$" + std::to_string(keys.size()) + "$";
    for (const auto& k : keys)
        out += std::to_string(k.size()) + "$" + k;
    return out;
}

// 型パラメータを実引数ツリーで置換する（実装は正準APIのast::substitute_type_params）
hir::TypePtr Monomorphization::substitute_type_tree(
    const hir::TypePtr& type, const std::unordered_map<std::string, hir::TypePtr>& subst) const {
    return ast::substitute_type_params(type, subst);
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
    if (!st && ast::typekey::is_encoded_key(name)) {
        std::string base = ast::typekey::base_name_of(name);
        it = hir_struct_defs->find(base);
        if (it != hir_struct_defs->end()) {
            st = it->second;
            if (args.empty())
                args = ast::typekey::decode_type_args(name);
        }
    }

    // （曖昧なフラット特殊化名Node__int等の逆算は廃止済み。産生側が$エンコードへ正準化されている）

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
