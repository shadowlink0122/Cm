// 単相化 - ジェネリック呼び出しの特殊化呼び出しへの書き換え
// （monomorphization-typed-instantiation: スキャンで記録した呼び出しサイト表の引き当てのみで書き換え、
//   呼び出し名の再解析・型パラメータ名の仮定（T/U/V/W）による総称名の再構築は行わない）

#include "internal/base/debug.hpp"
#include "internal/base/target.hpp"
#include "internal/mir/lowering/mono/internal.hpp"
#include "internal/mir/lowering/mono/monomorphization.hpp"
#include "internal/mir/lowering/mono/utils.hpp"

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

// 型からtype_argsを文字列として抽出
std::vector<std::string> Monomorphization::extract_type_args_strings(const hir::TypePtr& type) {
    std::vector<std::string> result;
    if (!type)
        return result;

    for (const auto& arg : type->type_args) {
        if (arg) {
            result.push_back(get_type_name(arg));
        }
    }
    return result;
}

// ジェネリック呼び出しを特殊化呼び出しへ書き換える。
// スキャンが記録した（呼び出し元, ブロック）サイトへ特殊化シンボル名を直接引き当てる
void Monomorphization::rewrite_generic_calls(MirProgram& program, const SpecRequests& needed) {
    // 呼び出し元関数名 → MirFunction* の索引
    std::unordered_map<std::string, MirFunction*> func_index;
    for (auto& func : program.functions) {
        if (func) {
            func_index[func->name] = func.get();
        }
    }

    for (const auto& [spec_name, req] : needed) {
        for (const auto& [caller_name, block_idx] : req.call_sites) {
            auto fit = func_index.find(caller_name);
            if (fit == func_index.end())
                continue;
            MirFunction* caller = fit->second;
            if (block_idx >= caller->basic_blocks.size())
                continue;
            auto& block = caller->basic_blocks[block_idx];
            if (!block || !block->terminator || block->terminator->kind != MirTerminator::Call)
                continue;
            auto& call_data = std::get<MirTerminator::CallData>(block->terminator->data);
            if (!call_data.func || call_data.func->kind != MirOperand::FunctionRef)
                continue;
            auto& func_name = std::get<std::string>(call_data.func->data);
            if (func_name != spec_name) {
                debug_msg("MONO",
                          "Rewrote call in " + caller_name + ": " + func_name + " -> " + spec_name);
                func_name = spec_name;
            }
        }
    }

    // デストラクタ呼び出し（Base__dtor形式）の型駆動書き換え:
    // temp-drop等が基底名で発行したdtor呼び出しを、実引数（selfポインタ）のローカル型ツリーから
    // 特殊化シンボル名（arg_symbol_key）へ引き当てる。名前文字列の切り出しは行わない
    std::unordered_set<std::string> existing;
    for (const auto& func : program.functions) {
        if (func) {
            existing.insert(func->name);
        }
    }
    for (auto& func : program.functions) {
        if (!func)
            continue;
        for (auto& block : func->basic_blocks) {
            if (!block || !block->terminator || block->terminator->kind != MirTerminator::Call)
                continue;
            auto& call_data = std::get<MirTerminator::CallData>(block->terminator->data);
            if (!call_data.func || call_data.func->kind != MirOperand::FunctionRef)
                continue;
            auto& func_name = std::get<std::string>(call_data.func->data);
            if (func_name.size() <= 6 || func_name.compare(func_name.size() - 6, 6, "__dtor") != 0)
                continue;
            if (call_data.args.empty() || !call_data.args[0])
                continue;
            if (call_data.args[0]->kind != MirOperand::Copy &&
                call_data.args[0]->kind != MirOperand::Move)
                continue;
            const auto& place = std::get<MirPlace>(call_data.args[0]->data);
            if (place.local >= func->locals.size())
                continue;
            const auto& local_type = func->locals[place.local].type;
            if (!local_type || local_type->kind != hir::TypeKind::Pointer ||
                !local_type->element_type)
                continue;
            // 要素型ツリーから特殊化dtor名を生成（型引数ツリーがあればフラット規約でエンコード、
            // 無ければフラット名縮退した型名をそのまま使う）
            const auto& elem = local_type->element_type;
            std::string specialized_dtor;
            if (!elem->type_args.empty() && !tree_has_generic_param(elem)) {
                std::string base = elem->name;
                auto lt = base.find('<');
                if (lt != std::string::npos) {
                    base = base.substr(0, lt);
                }
                specialized_dtor = struct_symbol_key(base, elem->type_args) + "__dtor";
            } else if (!elem->name.empty() && elem->name.find('<') == std::string::npos) {
                specialized_dtor = elem->name + "__dtor";
            }
            if (!specialized_dtor.empty() && specialized_dtor != func_name &&
                existing.count(specialized_dtor)) {
                debug_msg("MONO",
                          "Rewriting destructor call: " + func_name + " -> " + specialized_dtor);
                func_name = specialized_dtor;
            }
        }
    }
}

}  // namespace cm::mir
