#include "../../common/debug.hpp"
#include "monomorphization.hpp"

namespace cm::mir {

// 関数内の呼び出しをスキャンして特殊化が必要なものを記録
void Monomorphization::scan_function_calls(
    MirFunction* func, const std::string& caller_name,
    const std::unordered_map<std::string, const hir::HirFunction*>& hir_functions,
    std::unordered_map<std::string, std::vector<std::tuple<std::string, size_t, std::string>>>&
        needed) {
    if (!func)
        return;

    // 各ブロックの終端命令をチェック
    for (const auto& block : func->basic_blocks) {
        if (!block || !block->terminator)
            continue;

        if (block->terminator->kind == MirTerminator::Call) {
            auto& call_data = std::get<MirTerminator::CallData>(block->terminator->data);

            // 呼び出される関数のHIR情報を取得
            auto it = hir_functions.find(call_data.func_name);
            if (it == hir_functions.end())
                continue;

            const hir::HirFunction* callee = it->second;

            // 各引数をチェック
            for (size_t i = 0; i < call_data.args.size() && i < callee->params.size(); ++i) {
                const auto& param = callee->params[i];

                // パラメータ型がインターフェースかチェック
                if (param.type && is_interface_type(param.type->name)) {
                    // 実引数の型を取得（簡易実装: ここでは型推論が必要）
                    // 現在の実装では、引数がローカル変数参照の場合のみ対応
                    if (call_data.args[i] && call_data.args[i]->kind == MirOperand::Copy) {
                        if (auto* place = std::get_if<MirPlace>(&call_data.args[i]->data)) {
                            // ローカル変数の型を取得
                            if (place->local < func->locals.size()) {
                                auto& local = func->locals[place->local];
                                if (local.type && local.type->kind == hir::TypeKind::Struct) {
                                    // 特殊化が必要な呼び出しを記録
                                    needed[call_data.func_name].push_back(
                                        std::make_tuple(caller_name, i, local.type->name));
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

// 特殊化関数を生成
void Monomorphization::generate_specializations(
    MirProgram& program,
    const std::unordered_map<std::string, const hir::HirFunction*>& hir_functions,
    const std::unordered_map<std::string,
                             std::vector<std::tuple<std::string, size_t, std::string>>>& needed) {
    // 生成済みの特殊化を追跡
    std::unordered_set<std::string> generated;

    for (const auto& [func_name, calls] : needed) {
        for (const auto& [caller, param_idx, actual_type] : calls) {
            // 特殊化関数名を生成
            std::string specialized_name = make_specialized_name(func_name, actual_type);

            // すでに生成済みならスキップ
            if (generated.count(specialized_name) > 0)
                continue;
            generated.insert(specialized_name);

            // 元の関数を取得
            auto it = hir_functions.find(func_name);
            if (it == hir_functions.end())
                continue;

            // 特殊化関数を生成
            auto specialized = generate_specialized_function(*it->second, actual_type, param_idx);
            if (specialized) {
                program.functions[specialized->name] = std::move(specialized);
            }

            // 呼び出し側の関数を書き換え
            if (auto caller_func_it = program.functions.find(caller);
                caller_func_it != program.functions.end()) {
                auto& caller_func = caller_func_it->second;

                // Call命令の関数名を書き換え
                for (auto& block : caller_func->basic_blocks) {
                    if (!block || !block->terminator)
                        continue;

                    if (block->terminator->kind == MirTerminator::Call) {
                        auto& call_data =
                            std::get<MirTerminator::CallData>(block->terminator->data);
                        if (call_data.func_name == func_name) {
                            // 特殊化された関数名に変更
                            call_data.func_name = specialized_name;
                        }
                    }
                }
            }
        }
    }
}

// 特殊化関数を生成（単一の関数）
MirFunctionPtr Monomorphization::generate_specialized_function(const hir::HirFunction& original,
                                                               const std::string& actual_type,
                                                               size_t /* param_idx */) {
    // 簡易実装: 元の関数をコピーして型を置換
    // TODO: 実際の実装では、パラメータ型を置換してから再度loweringが必要

    auto specialized = std::make_unique<MirFunction>();
    specialized->name = make_specialized_name(original.name, actual_type);

    // 仮実装: エントリーブロックとreturnのみ
    specialized->entry_block = 0;
    specialized->basic_blocks.push_back(std::make_unique<BasicBlock>());
    specialized->basic_blocks[0]->id = 0;
    specialized->basic_blocks[0]->terminator = MirTerminator::return_value();

    // 引数と戻り値のローカル変数
    LocalId local_id = 0;
    for (const auto& param : original.params) {
        specialized->arg_locals.push_back(local_id);
        LocalDecl local_decl(local_id, param.name, param.type, false, false);
        specialized->locals.push_back(local_decl);
        local_id++;
    }

    specialized->return_local = local_id;
    LocalDecl return_decl(local_id, "@return", original.return_type, true, false);
    specialized->locals.push_back(return_decl);

    return specialized;
}

// 元のジェネリック関数を削除
void Monomorphization::cleanup_generic_functions(
    MirProgram& program,
    const std::unordered_map<std::string,
                             std::vector<std::tuple<std::string, size_t, std::string>>>& needed) {
    // 特殊化された関数の元関数を削除
    for (const auto& [func_name, calls] : needed) {
        if (!calls.empty()) {
            // すべての呼び出しが特殊化されていれば元関数を削除
            program.functions.erase(func_name);
        }
    }
}

}  // namespace cm::mir