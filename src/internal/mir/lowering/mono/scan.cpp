// 単相化 - ジェネリック呼び出しのスキャンと型引数の構造的単一化による推論
// （monomorphization-typed-instantiation: 特殊化の同定は型ノードで行い、名前からの型逆算は
//   ローワリングが埋め込んだマングル済み呼び出し名の復元境界decode_type_nameの1箇所に限定する）

#include "internal/base/debug.hpp"
#include "internal/base/target.hpp"
#include "internal/mir/lowering/mono/internal.hpp"
#include "internal/mir/lowering/mono/monomorphization.hpp"
#include "internal/mir/lowering/mono/utils.hpp"
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

namespace {

// 型ツリーが指定のジェネリックパラメータ名そのものか
bool is_generic_param_of(const hir::TypePtr& t, const hir::HirFunction* callee) {
    if (!t || !callee) {
        return false;
    }
    for (const auto& gp : callee->generic_params) {
        if (t->name == gp.name) {
            return true;
        }
    }
    return false;
}

}  // namespace

// パラメータ型と実引数型の構造的単一化。
// 名前ベース推論（T[]→"int[]"文字列の切り出し・Pair__int__stringの再分解）を置換し、
// Pointer/Arrayの要素型・Structのtype_argsを再帰的に照合して型パラメータを束縛する
void Monomorphization::unify_type_param(
    const hir::TypePtr& param_type, const hir::TypePtr& arg_type, const hir::HirFunction* callee,
    std::unordered_map<std::string, hir::TypePtr>& inferred) const {
    if (!param_type || !arg_type) {
        return;
    }

    // パラメータが型パラメータそのもの（T ← 実引数型）
    if (is_generic_param_of(param_type, callee)) {
        // 自己推論（T=T）と未解決ジェネリック残存の束縛は無意味なので除外する
        if (!tree_has_generic_param(arg_type) && !arg_type->is_error() &&
            inferred.find(param_type->name) == inferred.end()) {
            inferred[param_type->name] = arg_type;
            debug_msg("MONO", "Unified " + param_type->name + " = " + get_type_name(arg_type));
        }
        return;
    }

    // ポインタ/配列（スライス含む）: 要素型を再帰照合
    if ((param_type->kind == hir::TypeKind::Pointer || param_type->kind == hir::TypeKind::Array ||
         param_type->kind == hir::TypeKind::Reference) &&
        param_type->element_type) {
        hir::TypePtr arg_elem;
        if (arg_type->kind == param_type->kind && arg_type->element_type) {
            arg_elem = arg_type->element_type;
        } else if (param_type->kind == hir::TypeKind::Pointer &&
                   arg_type->kind == hir::TypeKind::Struct) {
            // 実引数側がフラット名構造体（Node__Item等）へ縮退している場合は復元して照合する
            arg_elem = nullptr;
        }
        if (arg_elem) {
            unify_type_param(param_type->element_type, arg_elem, callee, inferred);
        }
        if (arg_elem || param_type->kind != hir::TypeKind::Pointer) {
            return;
        }
    }

    // 構造体: type_argsを対で照合する。実引数側に型引数ツリーが無い場合（フラット名縮退）は
    // decode_type_nameで復元してから照合する
    hir::TypePtr arg_struct = arg_type;
    if (param_type->kind == hir::TypeKind::Pointer && param_type->element_type) {
        // パラメータがポインタで実引数が構造体名のみ（*が名前に埋まったケース）: 要素型で照合
        unify_type_param(param_type->element_type, arg_struct, callee, inferred);
        return;
    }
    if (!param_type->type_args.empty()) {
        if (arg_struct->type_args.empty() && !arg_struct->name.empty()) {
            arg_struct = decode_type_name(arg_struct->name);
        }
        if (arg_struct && !arg_struct->type_args.empty()) {
            for (size_t i = 0; i < param_type->type_args.size() && i < arg_struct->type_args.size();
                 ++i) {
                unify_type_param(param_type->type_args[i], arg_struct->type_args[i], callee,
                                 inferred);
            }
        }
    }
}

// フラット名/表示名を型ツリーへ復元する単一の境界。
// ローワリングが呼び出し名やローカル型名に埋め込んだマングル済み名（Vector__int / Vector<int> /
// ptr_int / *int / int）をここでのみ型ノードへ戻す（他の箇所での名前解析は禁止）
hir::TypePtr Monomorphization::decode_type_name(const std::string& name) const {
    if (name.empty()) {
        return nullptr;
    }
    // *T 形式（ローワリングの表示形）
    if (name.front() == '*') {
        auto elem = decode_type_name(name.substr(1));
        auto t = hir::make_pointer(elem);
        t->name = "ptr_" + (elem ? elem->name : std::string("void"));
        return t;
    }
    // T* 形式
    if (name.size() > 1 && name.back() == '*') {
        auto elem = decode_type_name(name.substr(0, name.size() - 1));
        auto t = hir::make_pointer(elem);
        t->name = "ptr_" + (elem ? elem->name : std::string("void"));
        return t;
    }
    // T[] 形式（スライス）
    if (name.size() > 2 && name.compare(name.size() - 2, 2, "[]") == 0) {
        auto elem = decode_type_name(name.substr(0, name.size() - 2));
        auto t = std::make_shared<hir::Type>(hir::TypeKind::Array);
        t->element_type = elem;
        t->name = name;
        return t;
    }
    // $エンコード名（typekey）は可逆復号を最優先する（フラット文法は本質的に曖昧なため、逆算ヒューリスティックへは渡さない）
    if (ast::typekey::is_encoded_key(name)) {
        const std::string base = ast::typekey::base_name_of(name);
        auto args = ast::typekey::decode_type_args(name);
        if (!args.empty()) {
            auto t = std::make_shared<hir::Type>(hir::TypeKind::Struct);
            t->name = base;
            t->type_args = std::move(args);
            return t;
        }
    }
    // 表示形（Vector<int>）・プリミティブ・ptr_xxx・非ジェネリック名は既存デコーダで復元
    // （曖昧なフラット特殊化名Vector__int等の逆算は廃止済み。産生側が$エンコード/表示形へ正準化されている）
    return make_type_from_name(name);
}

// 特殊化関数名を生成（名前生成の終端。型引数はarg_symbol_keyの__フラット規約でエンコードする）
std::string Monomorphization::make_specialized_name(
    const std::string& base_name, const std::vector<hir::TypePtr>& type_args) const {
    auto pos = base_name.find("<");
    auto end_pos = base_name.find(">__");

    std::string args_str;
    for (const auto& arg : type_args) {
        args_str += "__" + arg_symbol_key(arg);
    }

    if (pos != std::string::npos && end_pos != std::string::npos && !type_args.empty()) {
        // implメソッド形（Vector<T>__init → Vector__int__init）
        return base_name.substr(0, pos) + args_str + base_name.substr(end_pos + 1);
    }
    return base_name + args_str;
}

// ジェネリック関数呼び出しをスキャンし、特殊化要求（型引数ツリー+呼び出しサイト）を収集する
void Monomorphization::scan_generic_calls(
    MirFunction* func, const std::unordered_set<std::string>& generic_funcs,
    const std::unordered_map<std::string, const hir::HirFunction*>& hir_functions,
    SpecRequests& needed) {
    if (!func)
        return;

    auto record = [&](const std::string& generic_name, std::vector<hir::TypePtr> type_args,
                      size_t block_idx) {
        // 置換に使うツリーを正準化（フラット名リーフの復号）。キーとツリーの両方が正準になる
        for (auto& a : type_args)
            a = normalize_spec_arg_tree(a);
        const std::string spec_name = make_specialized_name(generic_name, type_args);
        auto& req = needed[spec_name];
        if (req.generic_name.empty()) {
            req.generic_name = generic_name;
            req.type_args = std::move(type_args);
        }
        req.call_sites.push_back({func->name, block_idx});
    };

    for (size_t block_idx = 0; block_idx < func->basic_blocks.size(); ++block_idx) {
        auto& block = func->basic_blocks[block_idx];
        if (!block || !block->terminator || block->terminator->kind != MirTerminator::Call)
            continue;

        auto& call_data = std::get<MirTerminator::CallData>(block->terminator->data);
        if (!call_data.func || call_data.func->kind != MirOperand::FunctionRef)
            continue;

        const auto& func_name = std::get<std::string>(call_data.func->data);

        // 1. 総称シンボル名そのものの呼び出し: 実引数型・戻り値格納先型から構造的単一化で推論する
        if (generic_funcs.count(func_name) > 0) {
            auto it = hir_functions.find(func_name);
            if (it != hir_functions.end()) {
                auto type_args = infer_type_args(func, call_data, it->second);
                if (!type_args.empty()) {
                    debug_msg("MONO", "Scanned call in " + func->name + " to " + func_name +
                                          " with type args: " + get_type_name(type_args[0]));
                    record(func_name, std::move(type_args), block_idx);
                } else {
                    debug_msg("MONO", "WARNING: Could not infer type args for " + func_name +
                                          " in " + func->name);
                }
            }
            continue;
        }

        // 2〜4. ローワリングがマングル済み名を埋め込んだ呼び出し
        //   表示形:   Container<int>__print / HashMap<int, int>__ctor_1
        //   フラット: Vector__int__init / HashMap__int__int__put
        // 総称シンボル（Base<...>__suffix）と基底名+サフィックスで照合し、
        // 型引数部分はdecode_type_nameで型ツリーへ復元する
        for (const auto& generic_name : generic_funcs) {
            auto g_angle = generic_name.find('<');
            auto g_close = generic_name.find(">__");
            if (g_angle == std::string::npos || g_close == std::string::npos)
                continue;

            const std::string base_name = generic_name.substr(0, g_angle);
            const std::string suffix = generic_name.substr(g_close + 1);  // "__print" / "__ctor_1"
            const size_t num_params =
                split_type_args(generic_name.substr(g_angle + 1, g_close - g_angle - 1)).size();

            auto it = hir_functions.find(generic_name);
            if (it == hir_functions.end())
                continue;

            std::vector<hir::TypePtr> type_args;

            // 表示形: Base<args>__suffix
            auto f_angle = func_name.find('<');
            if (f_angle != std::string::npos && func_name.substr(0, f_angle) == base_name) {
                auto f_close = func_name.find(">__");
                if (f_close != std::string::npos && func_name.substr(f_close + 1) == suffix) {
                    auto arg_strs =
                        split_type_args(func_name.substr(f_angle + 1, f_close - f_angle - 1));
                    if (arg_strs.size() == num_params) {
                        for (const auto& s : arg_strs) {
                            type_args.push_back(decode_type_name(s));
                        }
                    }
                }
            }
            // フラット形: Base__args__suffixTail（suffixTail = suffixの先頭__を除いた部分）。
            // argsセグメントには表示形（Vector<TrackedItem>等）が混在し得るため、復元はdecode_type_nameに委ねる
            else if (func_name.size() > base_name.size() + 2 &&
                     func_name.compare(0, base_name.size() + 2, base_name + "__") == 0) {
                const std::string suffix_tail = suffix.substr(2);  // "print" / "ctor_1"
                const std::string remaining = func_name.substr(base_name.size() + 2);
                if (remaining.size() > suffix_tail.size() + 2 &&
                    remaining.compare(remaining.size() - suffix_tail.size(), suffix_tail.size(),
                                      suffix_tail) == 0 &&
                    remaining.compare(remaining.size() - suffix_tail.size() - 2, 2, "__") == 0) {
                    const std::string args_part =
                        remaining.substr(0, remaining.size() - suffix_tail.size() - 2);
                    if (num_params == 1) {
                        // 型パラメータ1個: 残り全体を1引数として復元（ネスト対応: Vector__int等）
                        auto arg = decode_type_name(args_part);
                        // 基底の型パラメータ名がそのまま残っている呼び出し（T__method等の
                        // 未特殊化本体）からの要求は生成しない
                        if (arg && !tree_has_generic_param(arg)) {
                            type_args.push_back(std::move(arg));
                        }
                    } else {
                        // 複数型パラメータ: __区切りの各セグメントを1引数として復元
                        std::vector<std::string> parts;
                        size_t pos = 0;
                        while (pos <= args_part.size()) {
                            auto next = args_part.find("__", pos);
                            if (next == std::string::npos) {
                                parts.push_back(args_part.substr(pos));
                                break;
                            }
                            parts.push_back(args_part.substr(pos, next - pos));
                            pos = next + 2;
                        }
                        if (parts.size() == num_params) {
                            bool all_ok = true;
                            for (const auto& s : parts) {
                                auto arg = decode_type_name(s);
                                if (!arg || tree_has_generic_param(arg)) {
                                    all_ok = false;
                                    break;
                                }
                                type_args.push_back(std::move(arg));
                            }
                            if (!all_ok) {
                                type_args.clear();
                            }
                        }
                    }
                }
            }

            // 件数不一致（曖昧なフラット名等）は要求を記録しない。無置換特殊化の常時検査が下流の検出網になる（無言破棄の痕跡はデバッグログへ残す）
            if (type_args.size() != num_params) {
                debug_msg("MONO", "scan: type-arg count mismatch, call site dropped: " + func_name);
            }
            if (type_args.size() == num_params && !type_args.empty()) {
                debug_msg("MONO", "Found mangled call to " + func_name + " matching generic " +
                                      generic_name);
                record(generic_name, std::move(type_args), block_idx);
                break;
            }
        }
    }
}

// 呼び出しサイトの実引数型・戻り値格納先型から型パラメータを構造的単一化で推論する。
// MIRローカルの型ツリー（typed-hir-single-sourceで非null保証）を直接使い、型名文字列の切り出しは行わない
std::vector<hir::TypePtr> Monomorphization::infer_type_args(
    const MirFunction* caller, const MirTerminator::CallData& call_data,
    const hir::HirFunction* callee) {
    std::vector<hir::TypePtr> result;
    if (!callee || callee->generic_params.empty())
        return result;

    std::unordered_map<std::string, hir::TypePtr> inferred;

    // 実引数の型ツリーを取得するヘルパ
    auto arg_type_of = [&](const MirOperandPtr& arg) -> hir::TypePtr {
        if (!arg)
            return nullptr;
        if (arg->kind == MirOperand::Copy || arg->kind == MirOperand::Move) {
            if (auto* place = std::get_if<MirPlace>(&arg->data)) {
                if (place->local < caller->locals.size()) {
                    return caller->locals[place->local].type;
                }
            }
        } else if (arg->kind == MirOperand::Constant) {
            if (auto* constant = std::get_if<MirConstant>(&arg->data)) {
                return constant->type;
            }
        }
        return nullptr;
    };

    // 各パラメータ型 × 実引数型の構造的単一化
    for (size_t i = 0; i < callee->params.size() && i < call_data.args.size(); ++i) {
        const auto& param = callee->params[i];
        if (!param.type)
            continue;
        auto arg_type = arg_type_of(call_data.args[i]);
        if (!arg_type)
            continue;
        // 実引数がフラット/エンコード名縮退の構造体の場合は復元してから照合する
        if (arg_type->kind == hir::TypeKind::Struct && arg_type->type_args.empty() &&
            (arg_type->name.find("__") != std::string::npos ||
             ast::typekey::is_encoded_key(arg_type->name))) {
            if (auto decoded = decode_type_name(arg_type->name)) {
                arg_type = decoded;
            }
        }
        unify_type_param(param.type, arg_type, callee, inferred);
    }

    // 戻り値型からの推論（Item got = get_data(node) → T = Item）
    if (callee->return_type && call_data.destination &&
        call_data.destination->local < caller->locals.size()) {
        const auto& dest_type = caller->locals[call_data.destination->local].type;
        if (dest_type) {
            unify_type_param(callee->return_type, dest_type, callee, inferred);
        }
    }

    // 各型パラメータの推論結果を収集
    for (const auto& generic_param : callee->generic_params) {
        auto it = inferred.find(generic_param.name);
        if (it != inferred.end()) {
            result.push_back(it->second);
        } else {
            // 推論できなかった場合の既定int（従来互換。typed-hirの型保証下では原則到達しない）
            result.push_back(make_type_from_name("int"));
            debug_msg("MONO",
                      "WARNING: Could not infer " + generic_param.name + ", defaulting to int");
        }
    }

    return result;
}

}  // namespace cm::mir
