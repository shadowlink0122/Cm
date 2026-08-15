// call.cpp - 関数呼び出しのlowering extract_named_placeholders, lower_call

#include "internal/base/debug.hpp"
#include "internal/hir/lowering/fwd.hpp"
#include "internal/mir/lowering/expr.hpp"
#include "internal/mir/lowering/layout.hpp"
#include "internal/syntax/lexer/lexer.hpp"
#include "internal/syntax/parser/parser.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cm::mir {

// 関数呼び出しのlowering
LocalId ExprLowering::lower_call(const hir::HirCall& call, const hir::TypePtr& result_type,
                                 LoweringContext& ctx) {
    // builtin特別処理（各カテゴリのtry_lower_*が該当すれば処理して返す）
    if (auto lowered = try_lower_println(call, result_type, ctx)) {
        return *lowered;
    }
    if (auto lowered = try_lower_slice_builtin(call, result_type, ctx)) {
        return *lowered;
    }

    // 通常の関数呼び出し
    std::vector<MirOperandPtr> args;

    // Bug#10修正: ptr->method()後の書き戻し情報
    // deref_tempへの変更を*ptrに書き戻すために記録
    struct PtrWriteback {
        LocalId deref_temp;
        LocalId ptr_var;
    };
    std::optional<PtrWriteback> pending_writeback;

    // メソッド呼び出しかどうかを判定
    // 関数名が "TypeName__MethodName" の形式の場合
    bool is_method_call = false;
    auto double_underscore_pos = call.func_name.find("__");
    if (double_underscore_pos != std::string::npos && !call.args.empty()) {
        std::string type_name = call.func_name.substr(0, double_underscore_pos);
        // 構造体名として有効かチェック
        if (ctx.struct_defs && ctx.struct_defs->count(type_name) > 0) {
            is_method_call = true;
        } else if (ctx.enum_defs && ctx.enum_defs->count(type_name) > 0) {
            // Q5: enumのinherent implメソッド（Shape__area10等）もメソッド呼び出しとして扱う。タグ付きenumのselfはポインタ渡しのため、値渡しのままだと被呼側のデリファレンスで壊れていた（値enumのselfはInt kindでアドレス経路に乗らず従来どおり）
            is_method_call = true;
        }
    }

    // ジェネリック構造体メソッドの特殊化名補正。
    // 文字列補間式のパース経由などHIRに型引数情報が無い場合、呼び出し名がBox__get のように型引数なしで構成され未定義シンボル参照になる。
    // レシーバ変数のMIRローカル型は特殊化済み名（Box__int）を持つため、
    // そこから Box__int__get を再構成する
    std::string effective_call_name = call.func_name;
    if (is_method_call) {
        std::string type_name = call.func_name.substr(0, double_underscore_pos);
        std::string method_part = call.func_name.substr(double_underscore_pos + 2);
        hir::TypePtr recv_type = nullptr;
        const auto& self_arg = call.args[0];
        if (auto vr = std::get_if<std::unique_ptr<hir::HirVarRef>>(&self_arg->kind)) {
            if (auto lid = ctx.resolve_variable((*vr)->name)) {
                if (*lid < ctx.func->locals.size()) {
                    recv_type = ctx.func->locals[*lid].type;
                }
            }
        } else if (auto un = std::get_if<std::unique_ptr<hir::HirUnary>>(&self_arg->kind)) {
            // (*p).method(): ポインタ変数の要素型から取得
            if ((*un)->op == hir::HirUnaryOp::Deref && (*un)->operand) {
                if (auto vr2 =
                        std::get_if<std::unique_ptr<hir::HirVarRef>>(&(*un)->operand->kind)) {
                    if (auto lid = ctx.resolve_variable((*vr2)->name)) {
                        if (*lid < ctx.func->locals.size()) {
                            auto t = ctx.func->locals[*lid].type;
                            if (t && t->kind == hir::TypeKind::Pointer) {
                                recv_type = t->element_type;
                            }
                        }
                    }
                }
            }
        }
        // レシーバ型名が「型名__」で始まる特殊化名なら呼び出し名を補正
        if (recv_type && !recv_type->name.empty() &&
            recv_type->name.size() > type_name.size() + 2 &&
            recv_type->name.compare(0, type_name.size() + 2, type_name + "__") == 0) {
            effective_call_name = recv_type->name + "__" + method_part;
        }
    }

    for (size_t i = 0; i < call.args.size(); ++i) {
        const auto& arg = call.args[i];

        // cm_array_to_sliceの要素サイズ引数（第3引数）はHIRの埋め込み値を使わず、
        // 第1引数のポインタ要素型からlayout APIで再計算する（Z2）。
        // HIR層の手書きサイズはshort/tiny/構造体/ターゲット依存ポインタ幅を誤っており、
        // 変換後スライスのヘッダstride依存操作（wasmの文字列要素読み等）が崩れていた
        if (call.func_name == "cm_array_to_slice" && i == 2 && call.args.size() == 3 &&
            call.args[0] && call.args[0]->type &&
            call.args[0]->type->kind == hir::TypeKind::Pointer) {
            const int64_t stride = layout::array_elem_stride(ctx, call.args[0]->type->element_type);
            LocalId stride_local = ctx.new_temp(hir::make_long());
            MirConstant stride_const;
            stride_const.value = stride;
            stride_const.type = hir::make_long();
            ctx.push_statement(MirStatement::assign(
                MirPlace{stride_local}, MirRvalue::use(MirOperand::constant(stride_const))));
            args.push_back(MirOperand::copy(MirPlace{stride_local}));
            continue;
        }

        // メソッド呼び出しの第1引数（self）は特別な処理が必要
        // selfが変数参照の場合、コピーを避けて直接元の変数への参照を取る
        if (is_method_call && i == 0) {
            hir::TypePtr arg_type = arg->type;

            // Q5: 値enumレシーバのselfは常に値渡しへ正規化する。表現はintで被呼側も値selfを受けるが、宣言戻り値型経由などでは未解決のStruct kind名（Color等）のまま届き、構造体扱いのアドレス渡しに乗って表現が割れる
            if (arg_type && !arg_type->name.empty() && ctx.enum_defs &&
                ctx.enum_defs->count(arg_type->name) > 0 &&
                (!ctx.tagged_union_names || ctx.tagged_union_names->count(arg_type->name) == 0)) {
                LocalId arg_local = lower_expression(*arg, ctx);
                args.push_back(MirOperand::copy(MirPlace{arg_local}));
                continue;
            }

            // 引数が構造体型の場合、アドレスを取得
            if (arg_type && arg_type->kind == hir::TypeKind::Struct) {
                // Bug#10修正: ptr->method() パターン検出
                // パーサーが -> を Deref(ptr) に変換するため、self引数が HirUnary(Deref, HirVarRef(ptr)) の形式になる。
                // Deref結果への参照を作成して渡す（通常のs.method()と同じRef方式）。
                if (auto unary_ptr = std::get_if<std::unique_ptr<hir::HirUnary>>(&arg->kind)) {
                    const auto& unary = **unary_ptr;
                    if (unary.op == hir::HirUnaryOp::Deref && unary.operand) {
                        // Derefのオペランドが変数参照かチェック
                        if (auto inner_var_ref = std::get_if<std::unique_ptr<hir::HirVarRef>>(
                                &unary.operand->kind)) {
                            const auto& var_ref = **inner_var_ref;
                            auto ptr_var_opt = ctx.resolve_variable(var_ref.name);
                            if (ptr_var_opt) {
                                // ポインタをDerefして構造体コピーを取得
                                LocalId ptr_var = *ptr_var_opt;
                                LocalId deref_temp = ctx.new_temp(arg_type);
                                MirPlace deref_place{ptr_var};
                                deref_place.projections.push_back(PlaceProjection::deref());
                                ctx.push_statement(MirStatement::assign(
                                    MirPlace{deref_temp},
                                    MirRvalue::use(MirOperand::copy(deref_place))));
                                // 構造体コピーへの参照を作成
                                LocalId ref_temp = ctx.new_temp(hir::make_pointer(arg_type));
                                ctx.push_statement(MirStatement::assign(
                                    MirPlace{ref_temp},
                                    MirRvalue::ref(MirPlace{deref_temp}, false)));
                                args.push_back(MirOperand::copy(MirPlace{ref_temp}));
                                // 書き戻し情報を記録
                                pending_writeback = PtrWriteback{deref_temp, ptr_var};
                                debug_msg("mir_call",
                                          "[MIR] Bug#10: ptr->method() self: deref+ref "
                                          "local " +
                                              std::to_string(ptr_var) + " via deref_temp " +
                                              std::to_string(deref_temp) + " for " +
                                              call.func_name);
                                continue;
                            }
                        }
                        // Derefのオペランドが変数参照でない場合（式の場合）
                        // 式を評価してDeref結果を取得し、参照を渡す
                        if (unary.operand->type &&
                            unary.operand->type->kind == hir::TypeKind::Pointer) {
                            LocalId ptr_local = lower_expression(*unary.operand, ctx);
                            LocalId deref_temp = ctx.new_temp(arg_type);
                            MirPlace deref_place{ptr_local};
                            deref_place.projections.push_back(PlaceProjection::deref());
                            ctx.push_statement(MirStatement::assign(
                                MirPlace{deref_temp},
                                MirRvalue::use(MirOperand::copy(deref_place))));
                            LocalId ref_temp = ctx.new_temp(hir::make_pointer(arg_type));
                            ctx.push_statement(MirStatement::assign(
                                MirPlace{ref_temp}, MirRvalue::ref(MirPlace{deref_temp}, false)));
                            args.push_back(MirOperand::copy(MirPlace{ref_temp}));
                            // 書き戻し情報を記録
                            pending_writeback = PtrWriteback{deref_temp, ptr_local};
                            debug_msg("mir_call",
                                      "[MIR] Bug#10: ptr->method() self: passing evaluated "
                                      "deref+ref for " +
                                          call.func_name);
                            continue;
                        }
                    }
                }

                // 引数がHirVarRefかどうかチェック
                if (auto var_ref_ptr = std::get_if<std::unique_ptr<hir::HirVarRef>>(&arg->kind)) {
                    const auto& var_ref = **var_ref_ptr;
                    // 元の変数を直接参照（コピーを避ける）
                    auto original_var_opt = ctx.resolve_variable(var_ref.name);
                    if (original_var_opt) {
                        LocalId original_var = *original_var_opt;
                        LocalId ref_temp = ctx.new_temp(hir::make_pointer(arg_type));
                        ctx.push_statement(MirStatement::assign(
                            MirPlace{ref_temp}, MirRvalue::ref(MirPlace{original_var}, false)));
                        args.push_back(MirOperand::copy(MirPlace{ref_temp}));
                        continue;
                    }
                }

                // フィールド・インデックス・デリファレンスのチェーンがレシーバの場合は、
                // 場所化（lower_place）して実体のアドレスをselfとして渡す。
                // 従来は下のフォールバックで一時コピーへの参照が渡り、w.c.bump()のような
                // フィールドレシーバのメソッド内フィールド変異が呼び出し元に反映されなかった
                {
                    MirPlace recv_place{0};
                    hir::TypePtr recv_type = nullptr;
                    if (lower_place(arg.get(), ctx, recv_place, recv_type)) {
                        LocalId ref_temp = ctx.new_temp(hir::make_pointer(arg_type));
                        ctx.push_statement(MirStatement::assign(MirPlace{ref_temp},
                                                                MirRvalue::ref(recv_place, false)));
                        args.push_back(MirOperand::copy(MirPlace{ref_temp}));
                        continue;
                    }
                }

                // フォールバック: 通常のlower_expressionを使用（関数戻り値等の右辺値レシーバ）
                LocalId arg_local = lower_expression(*arg, ctx);
                LocalId ref_temp = ctx.new_temp(hir::make_pointer(arg_type));
                ctx.push_statement(MirStatement::assign(
                    MirPlace{ref_temp}, MirRvalue::ref(MirPlace{arg_local}, false)));
                args.push_back(MirOperand::copy(MirPlace{ref_temp}));
            } else {
                LocalId arg_local = lower_expression(*arg, ctx);
                args.push_back(MirOperand::copy(MirPlace{arg_local}));
            }
        } else {
            hir::TypePtr arg_type = arg->type;

            // パラメータ宣言型を解決する（名前付き関数はHIR定義、間接呼び出し・関数ポインタは関数型注釈から）。
            // 配列decay判定とパラメータ型への暗黙変換の両方で使う
            hir::TypePtr param_type = nullptr;
            if (ctx.hir_func_defs) {
                auto fit = ctx.hir_func_defs->find(call.func_name);
                if (fit == ctx.hir_func_defs->end()) {
                    fit = ctx.hir_func_defs->find(effective_call_name);
                }
                if (fit != ctx.hir_func_defs->end() && fit->second &&
                    i < fit->second->params.size()) {
                    param_type = fit->second->params[i].type;
                }
            }
            if (!param_type) {
                hir::TypePtr fn_type = nullptr;
                if (call.indirect_callee && call.indirect_callee->type) {
                    fn_type = call.indirect_callee->type;
                } else if (call.is_indirect) {
                    if (auto vid = ctx.resolve_variable(call.func_name)) {
                        if (*vid < ctx.func->locals.size()) {
                            fn_type = ctx.func->locals[*vid].type;
                        }
                    }
                }
                fn_type = fn_type ? ctx.resolve_typedef(fn_type) : nullptr;
                if (fn_type && fn_type->kind == hir::TypeKind::Function &&
                    i < fn_type->param_types.size()) {
                    param_type = fn_type->param_types[i];
                }
            }
            hir::TypePtr resolved_param = param_type ? ctx.resolve_typedef(param_type) : nullptr;
            const bool param_is_slice = resolved_param &&
                                        resolved_param->kind == hir::TypeKind::Array &&
                                        !resolved_param->array_size.has_value();

            // 固定サイズ配列の変数参照をポインタに自動変換（array decay）
            // C言語セマンティクス: 配列を関数に渡すとポインタにdecayする
            // 注意: スライス（動的配列、array_sizeなし）はすでに参照型なのでdecay不要。
            // パラメータがスライス型の場合はdecayせず、後段のcoerce_fixed_array_to_sliceでヒープスライスへ実体化する（Y5: 生ポインタをCmSlice*として誤読しゴミ値になっていた）
            if (arg_type && arg_type->kind == hir::TypeKind::Array &&
                arg_type->array_size.has_value() && !param_is_slice) {
                // 変数参照の場合、アドレスを取得
                if (auto var_ref_ptr = std::get_if<std::unique_ptr<hir::HirVarRef>>(&arg->kind)) {
                    const auto& var_ref = **var_ref_ptr;
                    auto original_var_opt = ctx.resolve_variable(var_ref.name);
                    if (original_var_opt) {
                        LocalId original_var = *original_var_opt;
                        // 配列の要素型へのポインタを作成
                        hir::TypePtr elem_type =
                            arg_type->element_type ? arg_type->element_type : hir::make_int();
                        LocalId ref_temp = ctx.new_temp(hir::make_pointer(elem_type));
                        ctx.push_statement(MirStatement::assign(
                            MirPlace{ref_temp}, MirRvalue::ref(MirPlace{original_var}, false)));
                        args.push_back(MirOperand::copy(MirPlace{ref_temp}));
                        continue;
                    }
                }
            }

            LocalId arg_local = lower_expression(*arg, ctx);
            if (param_type) {
                // 変換統一ドライバ第1段: パラメータ型への暗黙変換（B2/Y1〜Y3/Y5）をcoerce_to_expected 1系統で適用する
                arg_local = ctx.coerce_to_expected(arg_local, param_type);
            }
            args.push_back(MirOperand::copy(MirPlace{arg_local}));
        }
    }

    // 結果用の一時変数（型チェッカーが推論した型を使用）
    hir::TypePtr actual_result_type = result_type ? result_type : hir::make_int();
    LocalId result = ctx.new_temp(actual_result_type);

    // Call終端命令（現在のブロックを終端）
    BlockId success_block = ctx.new_block();

    // 関数オペランドを作成
    MirOperandPtr func_operand;
    std::vector<MirOperandPtr> capture_args;  // クロージャのキャプチャ引数

    if (call.indirect_callee) {
        // 関数型フィールド等、式の値を呼び出す。メンバ式ならPlace（obj.field）のまま呼び出し先にし、
        // JSバックエンドが obj.field(args) を直接出力してthis束縛を保持できるようにする
        bool lowered_as_place = false;
        if (auto member_ptr =
                std::get_if<std::unique_ptr<hir::HirMember>>(&call.indirect_callee->kind)) {
            MirPlace callee_place{0};
            hir::TypePtr callee_type;
            if (get_member_place(**member_ptr, ctx, callee_place, callee_type)) {
                func_operand =
                    MirOperand::copy(std::move(callee_place), call.indirect_callee->type);
                lowered_as_place = true;
            }
        }
        if (!lowered_as_place) {
            LocalId callee_local = lower_expression(*call.indirect_callee, ctx);
            func_operand = MirOperand::copy(MirPlace{callee_local}, call.indirect_callee->type);
        }
    } else if (call.is_indirect) {
        // 関数ポインタ経由の呼び出し: 変数から関数ポインタを取得
        auto var_id = ctx.resolve_variable(call.func_name);
        if (var_id) {
            debug_msg("mir_func_ptr_call", "[MIR] Resolved variable '" + call.func_name +
                                               "' to local " + std::to_string(*var_id));
            // クロージャかどうかチェック
            auto& local_decl = ctx.func->locals[*var_id];
            debug_msg("mir_closure_check",
                      "[MIR] Local " + std::to_string(*var_id) +
                          " is_closure=" + std::to_string(local_decl.is_closure) +
                          " captured_locals=" + std::to_string(local_decl.captured_locals.size()) +
                          " closure_func_name=" + local_decl.closure_func_name);
            if (local_decl.is_closure && !local_decl.captured_locals.empty()) {
                // クロージャ: 実際の関数名を使い、キャプチャ引数を追加
                func_operand = MirOperand::function_ref(local_decl.closure_func_name);

                // キャプチャされた変数を引数の先頭に追加
                for (LocalId cap_local : local_decl.captured_locals) {
                    capture_args.push_back(MirOperand::copy(MirPlace{cap_local}));
                }
            } else {
                func_operand = MirOperand::copy(MirPlace{*var_id});
            }
        } else {
            // 変数が見つからない場合は直接関数参照として処理
            // extern関数などは変数ではなく関数として登録されている
            func_operand = MirOperand::function_ref(effective_call_name);
        }
    } else {
        // 直接呼び出し: 関数参照を使用
        func_operand = MirOperand::function_ref(effective_call_name);
    }

    // デフォルト引数の補完。
    // 通常はHIR loweringが適用するが、文字列補間式のミニパイプライン経由の呼び出しでは関数定義情報が無く未補完のまま到達するため、
    // ここでHIR関数定義のデフォルト式を評価して不足分を追加する
    if (ctx.hir_func_defs && !call.func_name.empty()) {
        auto fit = ctx.hir_func_defs->find(call.func_name);
        if (fit != ctx.hir_func_defs->end() && fit->second) {
            const auto* hf = fit->second;
            for (size_t di = call.args.size(); di < hf->params.size(); ++di) {
                if (hf->params[di].default_value) {
                    LocalId dv = lower_expression(*hf->params[di].default_value, ctx);
                    // 変換統一ドライバ第1段: デフォルト値もcoerce_to_expected 1系統でパラメータ型へ変換する（B2/Y3/Y5）
                    dv = ctx.coerce_to_expected(dv, hf->params[di].type);
                    args.push_back(MirOperand::copy(MirPlace{dv}));
                }
            }
        }
    }

    // キャプチャ引数を通常の引数の前に挿入
    if (!capture_args.empty()) {
        std::vector<MirOperandPtr> all_args;
        for (auto& cap_arg : capture_args) {
            all_args.push_back(std::move(cap_arg));
        }
        for (auto& arg : args) {
            all_args.push_back(std::move(arg));
        }
        args = std::move(all_args);
    }

    // Call終端命令を手動で作成
    auto call_term = std::make_unique<MirTerminator>();
    call_term->kind = MirTerminator::Call;

    // インターフェースメソッド呼び出しかどうかを判定
    // 関数名が "TypeName__MethodName" の形式で、TypeNameがインターフェースの場合
    std::string interface_name;
    std::string method_name;
    bool is_virtual = false;

    auto underscore_pos = call.func_name.find("__");
    if (underscore_pos != std::string::npos) {
        std::string type_name = call.func_name.substr(0, underscore_pos);
        method_name = call.func_name.substr(underscore_pos + 2);

        // コンテキストのインターフェース名セットをチェック
        if (ctx.interface_names && ctx.interface_names->count(type_name) > 0) {
            interface_name = type_name;
            is_virtual = true;
        }
    }

    MirTerminator::CallData call_data{
        std::move(func_operand),
        std::move(args),
        MirPlace{result},  // 戻り値の格納先
        success_block,
        std::nullopt,    // unwind無し
        std::string(),   // interface_name
        std::string(),   // method_name
        false,           // is_virtual
        false,           // is_tail_call
        call.is_awaited  // is_awaited
    };

    if (is_virtual) {
        call_data.interface_name = interface_name;
        call_data.method_name = method_name;
        call_data.is_virtual = true;
    }

    call_term->data = std::move(call_data);
    ctx.set_terminator(std::move(call_term));

    // 次のブロックへ移動
    ctx.switch_to_block(success_block);

    // map/filter系ビルトインの結果はデータ所有権を持つ新規確保スライス。
    // 文末のdropパス対象として登録する（C12のスライス一時）
    if (call.func_name.rfind("__builtin_array_map", 0) == 0 ||
        call.func_name.rfind("__builtin_array_filter", 0) == 0 ||
        call.func_name == "__builtin_string_chars") {
        ctx.note_slice_temp(result);
    }

    // Bug#10修正: ptr->method()後の書き戻し
    // メソッドがderef_temp(コピー)を変更した場合、*ptrに書き戻す
    if (pending_writeback) {
        MirPlace deref_place{pending_writeback->ptr_var};
        deref_place.projections.push_back(PlaceProjection::deref());
        ctx.push_statement(MirStatement::assign(
            deref_place,
            MirRvalue::use(MirOperand::copy(MirPlace{pending_writeback->deref_temp}))));
        debug_msg("mir_call", "[MIR] Bug#10: writeback deref_temp " +
                                  std::to_string(pending_writeback->deref_temp) +
                                  " -> *ptr local " + std::to_string(pending_writeback->ptr_var) +
                                  " for " + call.func_name);
    }

    return result;
}

// メンバーアクセスのlowering

}  // namespace cm::mir
