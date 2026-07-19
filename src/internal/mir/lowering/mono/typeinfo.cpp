// 単相化 - 型名の正規化・特殊化名の生成・特殊化型のサイズ/アライメント計算

#include "internal/base/debug.hpp"
#include "internal/base/target.hpp"
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
            const hir::HirStruct* st = nullptr;
            if (hir_struct_defs && hir_struct_defs->count(type->name)) {
                st = hir_struct_defs->at(type->name);
            }
            if (st) {
                int64_t max_align = 1;
                for (const auto& f : st->fields) {
                    max_align = std::max(max_align, calculate_specialized_type_align(f.type));
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
            const hir::HirStruct* st = nullptr;
            if (hir_struct_defs && hir_struct_defs->count(type->name)) {
                st = hir_struct_defs->at(type->name);
            } else if (type->name.find("__") != std::string::npos) {
                std::string base = type->name.substr(0, type->name.find("__"));
                if (hir_struct_defs && hir_struct_defs->count(base)) {
                    st = hir_struct_defs->at(base);
                }
            }
            if (st) {
                int64_t offset = 0;
                int64_t max_align = 1;
                for (const auto& f : st->fields) {
                    int64_t fa = calculate_specialized_type_align(f.type);
                    max_align = std::max(max_align, fa);
                    offset = align_to(offset, fa) + calculate_specialized_type_size(f.type);
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
