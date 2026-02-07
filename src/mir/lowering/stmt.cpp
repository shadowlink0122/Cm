#include "stmt.hpp"

#include "../../common/debug.hpp"

#include <functional>

namespace cm::mir {

// let文のlowering
void StmtLowering::lower_let(const hir::HirLet& let, LoweringContext& ctx) {
    // move初期化の場合、新しいローカルを作成せずエイリアスとして登録（真のゼロコストmove）
    // is_moveフラグはHIR loweringでMoveExprから初期化された場合に立てられる
    if (let.is_move && let.init && !let.ctor_call) {
        if (auto* var_ref = std::get_if<std::unique_ptr<hir::HirVarRef>>(&let.init->kind)) {
            if (*var_ref && !(*var_ref)->is_function_ref && !(*var_ref)->is_closure) {
                auto src_local = ctx.resolve_variable((*var_ref)->name);
                if (src_local) {
                    // 元の変数を新しい名前で再登録（エイリアス）
                    ctx.register_variable(let.name, *src_local);
                    debug_msg("mir_move_alias", "[MIR] Move alias: '" + let.name + "' -> local " +
                                                    std::to_string(*src_local) + " (same as '" +
                                                    (*var_ref)->name + "')");
                    return;
                }
            }
        }
    }

    // 新しいローカル変数を作成
    // is_const = true なら変更不可、false なら変更可能
    // is_static = true なら関数呼び出し間で値が保持される

    // enum型の場合、Tagged Union構造体型に変換
    // enum型は型名がenum_defsに登録されている
    hir::TypePtr actual_type = let.type;

    if (let.type && !let.type->name.empty() && ctx.enum_defs &&
        ctx.enum_defs->count(let.type->name)) {
        auto tagged_union_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
        tagged_union_type->name = "__TaggedUnion_" + let.type->name;
        actual_type = tagged_union_type;
    }

    LocalId local = ctx.new_local(let.name, actual_type, !let.is_const, true, let.is_static);

    // 変数をスコープに登録
    ctx.register_variable(let.name, local);

    // デバッグ: 登録した変数を確認
    if (let.type && let.type->kind == hir::TypeKind::Function) {
        debug_msg("mir_let_func_ptr",
                  "[MIR] Registered variable '" + let.name + "' as local " + std::to_string(local));
    }

    // const変数の場合、初期値が定数リテラルならその値を保存
    // これにより文字列補間でconst変数の値を直接使用できる
    if (let.is_const && let.init) {
        if (auto* lit = std::get_if<std::unique_ptr<hir::HirLiteral>>(&let.init->kind)) {
            if (*lit) {
                MirConstant const_val;
                const_val.type = let.type;
                const_val.value = (*lit)->value;
                ctx.register_const_value(let.name, const_val);
            }
        }
    }

    // static変数の場合、初期化コードは生成しない
    // LLVMバックエンドでグローバル変数としてゼロ初期化で生成される
    // インタプリタでは初回呼び出し時にのみ初期化される
    if (let.is_static) {
        // 初期化代入は生成しない
        // 注: 現在はゼロ初期化のみサポート。非ゼロ初期値は将来実装
        return;
    }

    // スライス型の変数で初期値がない場合、空のスライスを作成
    if (!let.init && let.type && let.type->kind == hir::TypeKind::Array &&
        !let.type->array_size.has_value()) {
        // 動的配列（スライス）の初期化
        hir::TypePtr elem_type = let.type->element_type ? let.type->element_type : hir::make_int();

        // 要素サイズを取得
        int64_t elem_size = 4;  // デフォルトはint
        auto elem_kind = elem_type->kind;
        if (elem_kind == hir::TypeKind::Char || elem_kind == hir::TypeKind::Bool ||
            elem_kind == hir::TypeKind::Tiny || elem_kind == hir::TypeKind::UTiny) {
            elem_size = 1;
        } else if (elem_kind == hir::TypeKind::Short || elem_kind == hir::TypeKind::UShort) {
            elem_size = 2;
        } else if (elem_kind == hir::TypeKind::Long || elem_kind == hir::TypeKind::ULong ||
                   elem_kind == hir::TypeKind::Double) {
            elem_size = 8;
        } else if (elem_kind == hir::TypeKind::Pointer || elem_kind == hir::TypeKind::String) {
            elem_size = 8;
        } else if (elem_kind == hir::TypeKind::Struct) {
            // TODO: 構造体のサイズを計算
            elem_size = 8;
        } else if (elem_kind == hir::TypeKind::Array) {
            // 多次元配列の場合、内側の配列サイズを計算
            // CmSlice構造体: data(8) + len(8) + cap(8) + elem_size(8) = 32バイト
            elem_size = sizeof(void*) * 4;  // CmSlice構造体のサイズ
        }

        // cm_slice_new(elem_size, initial_capacity) を呼び出し
        LocalId elem_size_local = ctx.new_temp(hir::make_long());
        MirConstant elem_size_const;
        elem_size_const.value = static_cast<int64_t>(elem_size);
        elem_size_const.type = hir::make_long();
        ctx.push_statement(MirStatement::assign(
            MirPlace{elem_size_local}, MirRvalue::use(MirOperand::constant(elem_size_const))));

        LocalId init_cap_local = ctx.new_temp(hir::make_long());
        MirConstant init_cap_const;
        init_cap_const.value = int64_t(0);  // 初期容量0
        init_cap_const.type = hir::make_long();
        ctx.push_statement(MirStatement::assign(
            MirPlace{init_cap_local}, MirRvalue::use(MirOperand::constant(init_cap_const))));

        // cm_slice_new呼び出し
        BlockId new_block = ctx.new_block();
        std::vector<MirOperandPtr> new_args;
        new_args.push_back(MirOperand::copy(MirPlace{elem_size_local}));
        new_args.push_back(MirOperand::copy(MirPlace{init_cap_local}));

        auto new_term = std::make_unique<MirTerminator>();
        new_term->kind = MirTerminator::Call;
        new_term->data = MirTerminator::CallData{MirOperand::function_ref("cm_slice_new"),
                                                 std::move(new_args),
                                                 MirPlace{local},
                                                 new_block,
                                                 std::nullopt,
                                                 "",
                                                 "",
                                                 false};
        ctx.set_terminator(std::move(new_term));
        ctx.switch_to_block(new_block);

        return;
    }

    // コンストラクタ呼び出しがある場合はlet.initをスキップ（コンストラクタが初期化を担当）
    if (let.init && !let.ctor_call) {
        // 配列→ポインタ暗黙変換のチェック
        // 左辺がポインタ型で右辺が配列型の場合、配列の先頭要素へのアドレスを取得
        bool is_array_to_pointer = false;
        if (let.type && let.init->type && let.type->kind == hir::TypeKind::Pointer &&
            let.init->type->kind == hir::TypeKind::Array) {
            is_array_to_pointer = true;
        }

        if (is_array_to_pointer) {
            // 配列変数への参照を取得
            if (auto* var_ref = std::get_if<std::unique_ptr<hir::HirVarRef>>(&let.init->kind)) {
                auto arr_local = ctx.resolve_variable((*var_ref)->name);
                if (arr_local) {
                    // 配列の先頭要素(&arr[0])へのRefを生成
                    // インデックス0のための一時変数
                    LocalId idx_zero = ctx.new_temp(hir::make_int());
                    MirConstant zero_const;
                    zero_const.value = int64_t(0);
                    zero_const.type = hir::make_int();
                    ctx.push_statement(MirStatement::assign(
                        MirPlace{idx_zero}, MirRvalue::use(MirOperand::constant(zero_const))));

                    // &arr[0] を生成
                    MirPlace arr_elem{*arr_local};
                    arr_elem.projections.push_back(PlaceProjection::index(idx_zero));

                    ctx.push_statement(
                        MirStatement::assign(MirPlace{local}, MirRvalue::ref(arr_elem, false)));
                } else {
                    // フォールバック: 通常のlowering
                    LocalId init_value = expr_lowering->lower_expression(*let.init, ctx);
                    ctx.push_statement(MirStatement::assign(
                        MirPlace{local}, MirRvalue::use(MirOperand::copy(MirPlace{init_value}))));
                }
            } else {
                // 変数参照でない場合は通常処理
                LocalId init_value = expr_lowering->lower_expression(*let.init, ctx);
                ctx.push_statement(MirStatement::assign(
                    MirPlace{local}, MirRvalue::use(MirOperand::copy(MirPlace{init_value}))));
            }
        } else {
            // スライスへの配列リテラル初期化をチェック
            bool is_slice_init_from_array = false;
            if (let.type && let.type->kind == hir::TypeKind::Array &&
                !let.type->array_size.has_value()) {
                // 左辺がスライス（動的配列）
                if (let.init->type && let.init->type->kind == hir::TypeKind::Array &&
                    let.init->type->array_size.has_value()) {
                    // 右辺が静的配列（配列リテラル）
                    is_slice_init_from_array = true;
                }
            }

            if (is_slice_init_from_array) {
                // 配列リテラルからスライスへの初期化
                // まず空のスライスを作成してから、各要素をpushで追加
                if (auto* arr_lit =
                        std::get_if<std::unique_ptr<hir::HirArrayLiteral>>(&let.init->kind)) {
                    const auto& elements = (*arr_lit)->elements;
                    hir::TypePtr elem_type =
                        let.type->element_type ? let.type->element_type : hir::make_int();

                    // 要素サイズを取得
                    int64_t elem_size = 4;  // デフォルトはint
                    auto elem_kind = elem_type->kind;
                    if (elem_kind == hir::TypeKind::Char || elem_kind == hir::TypeKind::Bool ||
                        elem_kind == hir::TypeKind::Tiny || elem_kind == hir::TypeKind::UTiny) {
                        elem_size = 1;
                    } else if (elem_kind == hir::TypeKind::Short ||
                               elem_kind == hir::TypeKind::UShort) {
                        elem_size = 2;
                    } else if (elem_kind == hir::TypeKind::Long ||
                               elem_kind == hir::TypeKind::ULong ||
                               elem_kind == hir::TypeKind::Double) {
                        elem_size = 8;
                    } else if (elem_kind == hir::TypeKind::Pointer ||
                               elem_kind == hir::TypeKind::String ||
                               elem_kind == hir::TypeKind::Struct) {
                        elem_size = 8;
                    } else if (elem_kind == hir::TypeKind::Array) {
                        // 多次元配列の場合、内側の配列サイズを計算
                        // CmSlice構造体: data(8) + len(8) + cap(8) + elem_size(8) = 32バイト
                        elem_size = sizeof(void*) * 4;  // CmSlice構造体のサイズ
                    }

                    // cm_slice_new(elem_size, initial_capacity) を呼び出し
                    LocalId elem_size_local_new = ctx.new_temp(hir::make_long());
                    MirConstant elem_size_const_new;
                    elem_size_const_new.value = static_cast<int64_t>(elem_size);
                    elem_size_const_new.type = hir::make_long();
                    ctx.push_statement(MirStatement::assign(
                        MirPlace{elem_size_local_new},
                        MirRvalue::use(MirOperand::constant(elem_size_const_new))));

                    LocalId init_cap_local = ctx.new_temp(hir::make_long());
                    MirConstant init_cap_const;
                    init_cap_const.value = static_cast<int64_t>(elements.size());  // 要素数で初期化
                    init_cap_const.type = hir::make_long();
                    ctx.push_statement(
                        MirStatement::assign(MirPlace{init_cap_local},
                                             MirRvalue::use(MirOperand::constant(init_cap_const))));

                    // cm_slice_new呼び出し
                    BlockId new_block = ctx.new_block();
                    std::vector<MirOperandPtr> new_args;
                    new_args.push_back(MirOperand::copy(MirPlace{elem_size_local_new}));
                    new_args.push_back(MirOperand::copy(MirPlace{init_cap_local}));

                    auto new_term = std::make_unique<MirTerminator>();
                    new_term->kind = MirTerminator::Call;
                    new_term->data =
                        MirTerminator::CallData{MirOperand::function_ref("cm_slice_new"),
                                                std::move(new_args),
                                                MirPlace{local},
                                                new_block,
                                                std::nullopt,
                                                "",
                                                "",
                                                false};
                    ctx.set_terminator(std::move(new_term));
                    ctx.switch_to_block(new_block);

                    // push関数名を決定
                    std::string push_func = "cm_slice_push_i32";
                    if (elem_kind == hir::TypeKind::Char || elem_kind == hir::TypeKind::Bool ||
                        elem_kind == hir::TypeKind::Tiny || elem_kind == hir::TypeKind::UTiny) {
                        push_func = "cm_slice_push_i8";
                    } else if (elem_kind == hir::TypeKind::Long ||
                               elem_kind == hir::TypeKind::ULong) {
                        push_func = "cm_slice_push_i64";
                    } else if (elem_kind == hir::TypeKind::Double) {
                        push_func = "cm_slice_push_f64";
                    } else if (elem_kind == hir::TypeKind::Float) {
                        push_func = "cm_slice_push_f32";
                    } else if (elem_kind == hir::TypeKind::Pointer ||
                               elem_kind == hir::TypeKind::String ||
                               elem_kind == hir::TypeKind::Struct) {
                        push_func = "cm_slice_push_ptr";
                    } else if (elem_kind == hir::TypeKind::Array) {
                        // 配列要素（多次元スライス）はスライス構造体をコピー
                        push_func = "cm_slice_push_slice";
                    }

                    // 各要素をpushで追加
                    for (const auto& elem : elements) {
                        LocalId elem_value;

                        // 要素が配列の場合、スライスに変換
                        if (elem_kind == hir::TypeKind::Array && elem->type &&
                            elem->type->array_size.has_value()) {
                            // 配列リテラルをスライスに変換
                            LocalId arr_value = expr_lowering->lower_expression(*elem, ctx);

                            // 内側の配列のサイズと要素サイズを取得
                            int64_t inner_size = elem->type->array_size.value_or(0);
                            int64_t inner_elem_size = 4;  // デフォルトはint
                            if (elem->type->element_type) {
                                auto inner_elem_kind = elem->type->element_type->kind;
                                if (inner_elem_kind == hir::TypeKind::Long ||
                                    inner_elem_kind == hir::TypeKind::ULong ||
                                    inner_elem_kind == hir::TypeKind::Double) {
                                    inner_elem_size = 8;
                                } else if (inner_elem_kind == hir::TypeKind::Char ||
                                           inner_elem_kind == hir::TypeKind::Bool ||
                                           inner_elem_kind == hir::TypeKind::Tiny ||
                                           inner_elem_kind == hir::TypeKind::UTiny) {
                                    inner_elem_size = 1;
                                } else if (inner_elem_kind == hir::TypeKind::Short ||
                                           inner_elem_kind == hir::TypeKind::UShort) {
                                    inner_elem_size = 2;
                                } else if (inner_elem_kind == hir::TypeKind::Pointer ||
                                           inner_elem_kind == hir::TypeKind::String) {
                                    inner_elem_size = 8;
                                }
                            }

                            // 配列のアドレスを取得
                            LocalId addr_local =
                                ctx.new_temp(hir::make_pointer(elem->type->element_type));
                            ctx.push_statement(MirStatement::assign(
                                MirPlace{addr_local}, MirRvalue::ref(MirPlace{arr_value}, false)));

                            // サイズ引数を作成
                            LocalId size_local = ctx.new_temp(hir::make_long());
                            MirConstant size_const;
                            size_const.value = static_cast<int64_t>(inner_size);
                            size_const.type = hir::make_long();
                            ctx.push_statement(MirStatement::assign(
                                MirPlace{size_local},
                                MirRvalue::use(MirOperand::constant(size_const))));

                            LocalId elem_size_local = ctx.new_temp(hir::make_long());
                            MirConstant elem_size_const;
                            elem_size_const.value = static_cast<int64_t>(inner_elem_size);
                            elem_size_const.type = hir::make_long();
                            ctx.push_statement(MirStatement::assign(
                                MirPlace{elem_size_local},
                                MirRvalue::use(MirOperand::constant(elem_size_const))));

                            // cm_array_to_slice呼び出し
                            LocalId slice_local = ctx.new_local("inner_slice", elem_type);
                            BlockId conv_block = ctx.new_block();

                            std::vector<MirOperandPtr> conv_args;
                            conv_args.push_back(MirOperand::copy(MirPlace{addr_local}));
                            conv_args.push_back(MirOperand::copy(MirPlace{size_local}));
                            conv_args.push_back(MirOperand::copy(MirPlace{elem_size_local}));

                            auto conv_term = std::make_unique<MirTerminator>();
                            conv_term->kind = MirTerminator::Call;
                            conv_term->data = MirTerminator::CallData{
                                MirOperand::function_ref("cm_array_to_slice"),
                                std::move(conv_args),
                                MirPlace{slice_local},
                                conv_block,
                                std::nullopt,
                                "",
                                "",
                                false};
                            ctx.set_terminator(std::move(conv_term));
                            ctx.switch_to_block(conv_block);

                            elem_value = slice_local;
                        } else {
                            elem_value = expr_lowering->lower_expression(*elem, ctx);

                            // floatスライスへのdouble要素の場合、floatにキャスト
                            // 浮動小数点リテラルはデフォルトでdoubleとして解析される
                            if (elem_kind == hir::TypeKind::Float) {
                                hir::TypePtr actual_elem_type = nullptr;
                                if (elem_value < ctx.func->locals.size()) {
                                    actual_elem_type = ctx.func->locals[elem_value].type;
                                }
                                if (actual_elem_type &&
                                    actual_elem_type->kind == hir::TypeKind::Double) {
                                    LocalId casted = ctx.new_temp(hir::make_float());
                                    ctx.push_statement(MirStatement::assign(
                                        MirPlace{casted},
                                        MirRvalue::cast(MirOperand::copy(MirPlace{elem_value}),
                                                        hir::make_float())));
                                    elem_value = casted;
                                }
                            }
                        }

                        BlockId success_block = ctx.new_block();
                        std::vector<MirOperandPtr> args;
                        args.push_back(MirOperand::copy(MirPlace{local}));
                        args.push_back(MirOperand::copy(MirPlace{elem_value}));

                        auto call_term = std::make_unique<MirTerminator>();
                        call_term->kind = MirTerminator::Call;
                        call_term->data =
                            MirTerminator::CallData{MirOperand::function_ref(push_func),
                                                    std::move(args),
                                                    std::nullopt,
                                                    success_block,
                                                    std::nullopt,
                                                    "",
                                                    "",
                                                    false};
                        ctx.set_terminator(std::move(call_term));
                        ctx.switch_to_block(success_block);
                    }
                } else {
                    // 配列リテラルでない場合（変数参照など）
                    // cm_array_to_slice を呼び出して変換
                    LocalId init_value = expr_lowering->lower_expression(*let.init, ctx);

                    // 配列のサイズと要素サイズを取得
                    int64_t array_size = let.init->type->array_size.value_or(0);
                    int64_t elem_size = 4;  // デフォルトはint32
                    if (let.init->type->element_type) {
                        auto ek = let.init->type->element_type->kind;
                        if (ek == hir::TypeKind::Char || ek == hir::TypeKind::Bool ||
                            ek == hir::TypeKind::Tiny || ek == hir::TypeKind::UTiny) {
                            elem_size = 1;
                        } else if (ek == hir::TypeKind::Long || ek == hir::TypeKind::ULong ||
                                   ek == hir::TypeKind::Double) {
                            elem_size = 8;
                        } else if (ek == hir::TypeKind::Pointer || ek == hir::TypeKind::String) {
                            elem_size = 8;
                        }
                    }

                    // 配列のアドレスを取得
                    LocalId addr_local =
                        ctx.new_temp(hir::make_pointer(let.init->type->element_type));
                    ctx.push_statement(MirStatement::assign(
                        MirPlace{addr_local}, MirRvalue::ref(MirPlace{init_value}, false)));

                    // サイズ引数を作成
                    LocalId size_local = ctx.new_temp(hir::make_long());
                    MirConstant size_const;
                    size_const.value = static_cast<int64_t>(array_size);
                    size_const.type = hir::make_long();
                    ctx.push_statement(MirStatement::assign(
                        MirPlace{size_local}, MirRvalue::use(MirOperand::constant(size_const))));

                    LocalId elem_size_local = ctx.new_temp(hir::make_long());
                    MirConstant elem_size_const;
                    elem_size_const.value = static_cast<int64_t>(elem_size);
                    elem_size_const.type = hir::make_long();
                    ctx.push_statement(MirStatement::assign(
                        MirPlace{elem_size_local},
                        MirRvalue::use(MirOperand::constant(elem_size_const))));

                    // cm_array_to_slice を呼び出す
                    BlockId success_block = ctx.new_block();
                    std::vector<MirOperandPtr> args;
                    args.push_back(MirOperand::copy(MirPlace{addr_local}));
                    args.push_back(MirOperand::copy(MirPlace{size_local}));
                    args.push_back(MirOperand::copy(MirPlace{elem_size_local}));

                    auto call_term = std::make_unique<MirTerminator>();
                    call_term->kind = MirTerminator::Call;
                    call_term->data =
                        MirTerminator::CallData{MirOperand::function_ref("cm_array_to_slice"),
                                                std::move(args),
                                                MirPlace{local},
                                                success_block,
                                                std::nullopt,
                                                "",
                                                "",
                                                false};
                    ctx.set_terminator(std::move(call_term));
                    ctx.switch_to_block(success_block);
                }
            } else {
                // 通常の初期化
                LocalId init_value = expr_lowering->lower_expression(*let.init, ctx);

                // クロージャ情報を新しい変数にコピー
                if (init_value < ctx.func->locals.size()) {
                    auto& init_decl = ctx.func->locals[init_value];
                    if (init_decl.is_closure && !init_decl.captured_locals.empty()) {
                        auto& new_decl = ctx.func->locals[local];
                        new_decl.is_closure = true;
                        new_decl.closure_func_name = init_decl.closure_func_name;
                        new_decl.captured_locals = init_decl.captured_locals;
                        debug_msg("mir_closure_copy",
                                  "[MIR] Copied closure info to local " + std::to_string(local) +
                                      " from local " + std::to_string(init_value) +
                                      ", func=" + new_decl.closure_func_name + ", captures=" +
                                      std::to_string(new_decl.captured_locals.size()));
                    }
                }

                // デバッグ: 型を確認
                if (let.type) {
                    debug_msg("mir_let_type", "[MIR] Let variable '" + let.name +
                                                  "' has type kind: " +
                                                  std::to_string(static_cast<int>(let.type->kind)));
                }

                // デバッグ: 関数ポインタ型の初期化を確認
                if (let.type && let.type->kind == hir::TypeKind::Function) {
                    debug_msg("mir_let_func_ptr", "[MIR] Function pointer initialization: local " +
                                                      std::to_string(local) + " = copy(local " +
                                                      std::to_string(init_value) + ")");

                    // 実際にステートメントを生成
                    auto stmt = MirStatement::assign(
                        MirPlace{local}, MirRvalue::use(MirOperand::copy(MirPlace{init_value})));
                    debug_msg("mir_let_func_ptr",
                              "[MIR] Created assign statement for local " + std::to_string(local));
                    ctx.push_statement(std::move(stmt));
                    debug_msg("mir_let_func_ptr", "[MIR] Pushed statement to context");

                    // 現在のブロックを確認
                    auto* block = ctx.get_current_block();
                    if (block) {
                        debug_msg("mir_let_func_ptr", "[MIR] Current block has " +
                                                          std::to_string(block->statements.size()) +
                                                          " statements");
                    } else {
                        debug_msg("mir_let_func_ptr", "[MIR] ERROR: No current block!");
                    }
                } else {
                    // デバッグ: 通常の初期化
                    if (let.name == "result") {
                        auto* block = ctx.get_current_block();
                        if (block) {
                            debug_msg("mir_result_init",
                                      "[MIR] Before 'result' init, block has " +
                                          std::to_string(block->statements.size()) + " statements");
                        }
                    }
                    ctx.push_statement(MirStatement::assign(
                        MirPlace{local}, MirRvalue::use(MirOperand::copy(MirPlace{init_value}))));
                    if (let.name == "result") {
                        auto* block = ctx.get_current_block();
                        if (block) {
                            debug_msg("mir_result_init",
                                      "[MIR] After 'result' init, block has " +
                                          std::to_string(block->statements.size()) + " statements");
                        }
                    }
                }
            }
        }
    }

    // コンストラクタ呼び出しがある場合
    if (let.ctor_call) {
        // コンストラクタ呼び出しはHirCall形式
        if (auto* call = std::get_if<std::unique_ptr<hir::HirCall>>(&let.ctor_call->kind)) {
            const auto& hir_call = **call;

            // 引数をlowering
            std::vector<MirOperandPtr> args;

            // HIRのctor_call.argsには既にthis（変数への参照）が含まれている
            // 最初の引数は変数自身への参照なので、アドレスを渡す（selfはポインタ型）
            bool first_arg = true;
            for (const auto& arg : hir_call.args) {
                if (first_arg) {
                    // 最初の引数（this/self）はアドレスを渡す
                    hir::TypePtr local_type = let.type;
                    LocalId ref_temp = ctx.new_temp(hir::make_pointer(local_type));
                    ctx.push_statement(MirStatement::assign(
                        MirPlace{ref_temp}, MirRvalue::ref(MirPlace{local}, false)));
                    args.push_back(MirOperand::copy(MirPlace{ref_temp}));
                    first_arg = false;
                } else {
                    // 残りの引数を通常通りlowering
                    LocalId arg_local = expr_lowering->lower_expression(*arg, ctx);
                    args.push_back(MirOperand::copy(MirPlace{arg_local}));
                }
            }

            // コンストラクタ関数呼び出しを生成
            BlockId success_block = ctx.new_block();
            auto func_operand = MirOperand::function_ref(hir_call.func_name);

            auto call_term = std::make_unique<MirTerminator>();
            call_term->kind = MirTerminator::Call;
            call_term->data = MirTerminator::CallData{std::move(func_operand),
                                                      std::move(args),
                                                      std::nullopt,  // コンストラクタは戻り値なし
                                                      success_block,
                                                      std::nullopt,
                                                      "",
                                                      "",
                                                      false};  // 通常の関数呼び出し
            ctx.set_terminator(std::move(call_term));
            ctx.switch_to_block(success_block);
        }
    }

    // デストラクタを持つ型の変数を登録
    if (let.type && let.type->kind == hir::TypeKind::Struct) {
        std::string type_name = let.type->name;

        // ジェネリック型の場合、マングル済み名を構築（Vector<TrackedObject> ->
        // Vector__TrackedObject）
        if (!let.type->type_args.empty()) {
            std::string mangled_name = type_name;

            // 再帰的にネストしたジェネリック型引数をマングリングするラムダ
            std::function<std::string(const hir::TypePtr&)> mangle_type_arg =
                [&](const hir::TypePtr& arg) -> std::string {
                if (!arg)
                    return "";

                std::string result;

                // 基本型名を取得
                if (!arg->name.empty()) {
                    result = arg->name;
                } else {
                    // プリミティブ型などは型を文字列化
                    result = hir::type_to_string(*arg);
                }

                // ネストしたtype_argsがある場合は再帰的に処理
                if (!arg->type_args.empty()) {
                    for (const auto& nested_arg : arg->type_args) {
                        result += "__" + mangle_type_arg(nested_arg);
                    }
                }

                return result;
            };

            for (const auto& arg : let.type->type_args) {
                mangled_name += "__" + mangle_type_arg(arg);
            }

            type_name = mangled_name;
        }

        if (ctx.has_destructor(type_name)) {
            ctx.register_destructor_var(local, type_name);
        }
    }
}

// 代入文のlowering
void StmtLowering::lower_assign(const hir::HirAssign& assign, LoweringContext& ctx) {
    if (!assign.target || !assign.value) {
        return;
    }

    // 右辺値をlowering
    LocalId rhs_value = expr_lowering->lower_expression(*assign.value, ctx);

    // 左辺値のMirPlaceを構築するヘルパー関数
    // 複雑な左辺値（c.values[0], points[0].x など）を再帰的に処理
    auto build_lvalue_place = [&](const hir::HirExpr* expr, MirPlace& place,
                                  hir::TypePtr& current_type) -> bool {
        // 再帰的にプロジェクションを構築
        std::function<bool(const hir::HirExpr*,
                           std::vector<std::function<void(MirPlace&, LoweringContext&)>>&,
                           hir::TypePtr&)>
            build_projections;
        build_projections =
            [&](const hir::HirExpr* e,
                std::vector<std::function<void(MirPlace&, LoweringContext&)>>& projections,
                hir::TypePtr& typ) -> bool {
            if (auto* var_ref = std::get_if<std::unique_ptr<hir::HirVarRef>>(&e->kind)) {
                // ベース変数
                auto var_id = ctx.resolve_variable((*var_ref)->name);
                if (var_id) {
                    place.local = *var_id;
                    if (*var_id < ctx.func->locals.size()) {
                        typ = ctx.func->locals[*var_id].type;
                    }
                    return true;
                }
                return false;
            } else if (auto* member = std::get_if<std::unique_ptr<hir::HirMember>>(&e->kind)) {
                // メンバーアクセス: object.member
                hir::TypePtr inner_type;
                if (!build_projections((*member)->object.get(), projections, inner_type)) {
                    return false;
                }

                // ポインタ型の場合、デリファレンスを追加
                if (inner_type && inner_type->kind == hir::TypeKind::Pointer) {
                    projections.push_back([](MirPlace& p, LoweringContext&) {
                        p.projections.push_back(PlaceProjection::deref());
                    });
                    inner_type = inner_type->element_type;
                }

                // フィールドプロジェクションを追加
                std::string field_name = (*member)->member;
                hir::TypePtr captured_inner_type = inner_type;
                projections.push_back([field_name, captured_inner_type, &ctx](MirPlace& p,
                                                                              LoweringContext&) {
                    if (captured_inner_type && captured_inner_type->kind == hir::TypeKind::Struct) {
                        auto field_idx = ctx.get_field_index(captured_inner_type->name, field_name);
                        if (field_idx) {
                            p.projections.push_back(PlaceProjection::field(*field_idx));
                        }
                    }
                });

                // 次の型を取得
                if (inner_type && inner_type->kind == hir::TypeKind::Struct) {
                    auto field_idx = ctx.get_field_index(inner_type->name, field_name);
                    if (field_idx && ctx.struct_defs && ctx.struct_defs->count(inner_type->name)) {
                        const auto* struct_def = ctx.struct_defs->at(inner_type->name);
                        if (*field_idx < struct_def->fields.size()) {
                            typ = struct_def->fields[*field_idx].type;
                        }
                    }
                }
                return true;
            } else if (auto* index = std::get_if<std::unique_ptr<hir::HirIndex>>(&e->kind)) {
                // インデックスアクセス: object[index] または object[i][j][k]...（多次元）
                hir::TypePtr inner_type;
                if (!build_projections((*index)->object.get(), projections, inner_type)) {
                    return false;
                }

                // 多次元配列最適化: indices が設定されている場合、全インデックスを処理
                if (!(*index)->indices.empty()) {
                    // 多次元: 全インデックスをプロジェクションとして追加
                    for (const auto& idx_expr : (*index)->indices) {
                        LocalId idx = expr_lowering->lower_expression(*idx_expr, ctx);
                        projections.push_back([idx](MirPlace& p, LoweringContext&) {
                            p.projections.push_back(PlaceProjection::index(idx));
                        });
                        // 型を更新（配列またはポインタの要素型）
                        if (inner_type && inner_type->element_type) {
                            if (inner_type->kind == hir::TypeKind::Array ||
                                inner_type->kind == hir::TypeKind::Pointer) {
                                inner_type = inner_type->element_type;
                            }
                        }
                    }
                    typ = inner_type;
                } else {
                    // 単一インデックス（後方互換性）
                    LocalId idx = expr_lowering->lower_expression(*(*index)->index, ctx);
                    projections.push_back([idx](MirPlace& p, LoweringContext&) {
                        p.projections.push_back(PlaceProjection::index(idx));
                    });
                    // 次の型を取得（配列またはポインタの要素型）
                    if (inner_type && inner_type->element_type) {
                        if (inner_type->kind == hir::TypeKind::Array ||
                            inner_type->kind == hir::TypeKind::Pointer) {
                            typ = inner_type->element_type;
                        }
                    }
                }
                return true;
            } else if (auto* unary = std::get_if<std::unique_ptr<hir::HirUnary>>(&e->kind)) {
                // デリファレンス: *ptr
                if ((*unary)->op == hir::HirUnaryOp::Deref) {
                    hir::TypePtr inner_type;
                    if (!build_projections((*unary)->operand.get(), projections, inner_type)) {
                        return false;
                    }

                    // デリファレンスプロジェクションを追加
                    projections.push_back([](MirPlace& p, LoweringContext&) {
                        p.projections.push_back(PlaceProjection::deref());
                    });

                    // 次の型を取得（ポインタの要素型）
                    if (inner_type && inner_type->kind == hir::TypeKind::Pointer &&
                        inner_type->element_type) {
                        typ = inner_type->element_type;
                    }
                    return true;
                }
                return false;
            }
            return false;
        };

        std::vector<std::function<void(MirPlace&, LoweringContext&)>> projections;
        if (!build_projections(expr, projections, current_type)) {
            return false;
        }

        // プロジェクションを適用
        for (auto& proj : projections) {
            proj(place, ctx);
        }

        return true;
    };

    // 左辺値の種類に応じて処理
    if (auto* var_ref = std::get_if<std::unique_ptr<hir::HirVarRef>>(&assign.target->kind)) {
        // 単純な変数代入
        auto lhs_opt = ctx.resolve_variable((*var_ref)->name);
        if (lhs_opt) {
            ctx.push_statement(MirStatement::assign(
                MirPlace{*lhs_opt}, MirRvalue::use(MirOperand::copy(MirPlace{rhs_value}))));
        }
    } else if (std::get_if<std::unique_ptr<hir::HirMember>>(&assign.target->kind) ||
               std::get_if<std::unique_ptr<hir::HirIndex>>(&assign.target->kind) ||
               std::get_if<std::unique_ptr<hir::HirUnary>>(&assign.target->kind)) {
        // 複雑な左辺値: メンバーアクセス、インデックスアクセス、またはデリファレンス
        // これには c.values[0], points[0].x, arr[i], obj.field, *ptr, (*ptr).x などが含まれる
        MirPlace place{0};
        hir::TypePtr current_type;

        if (build_lvalue_place(assign.target.get(), place, current_type)) {
            ctx.push_statement(
                MirStatement::assign(place, MirRvalue::use(MirOperand::copy(MirPlace{rhs_value}))));
        }
    }
    // その他の左辺値タイプは未対応
}

// return文のlowering
void StmtLowering::lower_return(const hir::HirReturn& ret, LoweringContext& ctx) {
    if (ret.value) {
        // 戻り値をlowering
        LocalId return_value = expr_lowering->lower_expression(*ret.value, ctx);

        // 戻り値をreturn用ローカル変数に代入
        ctx.push_statement(
            MirStatement::assign(MirPlace{ctx.func->return_local},
                                 MirRvalue::use(MirOperand::copy(MirPlace{return_value}))));
    }

    // 現在のスコープのdefer文を実行（逆順）
    auto defers = ctx.get_defer_stmts();
    for (const auto* defer_stmt : defers) {
        lower_statement(*defer_stmt, ctx);
    }

    // デストラクタを呼び出す（逆順）
    auto destructor_vars = ctx.get_all_destructor_vars();
    for (const auto& [local_id, type_name] : destructor_vars) {
        // ネストジェネリック型名の正規化（Vector<int> → Vector__int）
        std::string normalized_name = type_name;
        if (normalized_name.find('<') != std::string::npos) {
            std::string result;
            for (char c : normalized_name) {
                if (c == '<' || c == '>') {
                    if (c == '<')
                        result += "__";
                    // '>'は省略
                } else if (c == ',' || c == ' ') {
                    // カンマと空白は省略
                } else {
                    result += c;
                }
            }
            normalized_name = result;
        }
        std::string dtor_name = normalized_name + "__dtor";

        // デストラクタ呼び出しを生成（selfはポインタとして渡す）
        // ローカル変数の型を取得
        hir::TypePtr local_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
        local_type->name = type_name;
        LocalId ref_temp = ctx.new_temp(hir::make_pointer(local_type));
        ctx.push_statement(
            MirStatement::assign(MirPlace{ref_temp}, MirRvalue::ref(MirPlace{local_id}, false)));

        std::vector<MirOperandPtr> args;
        args.push_back(MirOperand::copy(MirPlace{ref_temp}));

        BlockId success_block = ctx.new_block();

        auto func_operand = MirOperand::function_ref(dtor_name);
        auto call_term = std::make_unique<MirTerminator>();
        call_term->kind = MirTerminator::Call;
        call_term->data = MirTerminator::CallData{std::move(func_operand),
                                                  std::move(args),
                                                  std::nullopt,  // void戻り値
                                                  success_block,
                                                  std::nullopt,
                                                  "",
                                                  "",
                                                  false};  // 通常の関数呼び出し
        ctx.set_terminator(std::move(call_term));
        ctx.switch_to_block(success_block);
    }

    // return終端命令
    ctx.set_terminator(MirTerminator::return_value());

    // 新しいブロックを作成（到達不可能だが、CFGの整合性のため）
    ctx.switch_to_block(ctx.new_block());
}

// if文のlowering
void StmtLowering::lower_if(const hir::HirIf& if_stmt, LoweringContext& ctx) {
    // 条件をlowering
    LocalId cond = expr_lowering->lower_expression(*if_stmt.cond, ctx);

    // ブロックを作成
    BlockId then_block = ctx.new_block();
    BlockId else_block = ctx.new_block();
    BlockId merge_block = ctx.new_block();

    // 条件分岐
    ctx.set_terminator(
        MirTerminator::switch_int(MirOperand::copy(MirPlace{cond}), {{1, then_block}}, else_block));

    // then部をlowering
    ctx.switch_to_block(then_block);
    for (const auto& stmt : if_stmt.then_block) {
        lower_statement(*stmt, ctx);
    }
    if (!ctx.get_current_block()->terminator) {
        ctx.set_terminator(MirTerminator::goto_block(merge_block));
    }

    // else部をlowering
    ctx.switch_to_block(else_block);
    for (const auto& stmt : if_stmt.else_block) {
        lower_statement(*stmt, ctx);
    }
    if (!ctx.get_current_block()->terminator) {
        ctx.set_terminator(MirTerminator::goto_block(merge_block));
    }

    // マージポイント
    ctx.switch_to_block(merge_block);
}

// while文のlowering
void StmtLowering::lower_while(const hir::HirWhile& while_stmt, LoweringContext& ctx) {
    // ブロックを作成
    BlockId loop_header = ctx.new_block();
    BlockId loop_body = ctx.new_block();
    BlockId loop_exit = ctx.new_block();

    // ループヘッダへジャンプ
    ctx.set_terminator(MirTerminator::goto_block(loop_header));

    // ループヘッダ（条件評価）
    ctx.switch_to_block(loop_header);
    LocalId cond = expr_lowering->lower_expression(*while_stmt.cond, ctx);
    ctx.set_terminator(
        MirTerminator::switch_int(MirOperand::copy(MirPlace{cond}), {{1, loop_body}}, loop_exit));

    // ループボディ
    ctx.switch_to_block(loop_body);
    ctx.push_loop(loop_header, loop_exit);
    for (const auto& stmt : while_stmt.body) {
        lower_statement(*stmt, ctx);
    }
    ctx.pop_loop();
    if (!ctx.get_current_block()->terminator) {
        ctx.set_terminator(MirTerminator::goto_block(loop_header));
    }

    // ループ出口
    ctx.switch_to_block(loop_exit);
}

// for文のlowering - whileループに展開
// for (init; cond; update) { body } を以下に変換:
// init; while (cond) { body; update; }
void StmtLowering::lower_for(const hir::HirFor& for_stmt, LoweringContext& ctx) {
    // 初期化部
    if (for_stmt.init) {
        lower_statement(*for_stmt.init, ctx);
    }

    // whileループとして処理
    // ブロックを作成
    BlockId loop_header = ctx.new_block();
    BlockId loop_body = ctx.new_block();
    BlockId loop_exit = ctx.new_block();

    // ループヘッダへジャンプ
    ctx.set_terminator(MirTerminator::goto_block(loop_header));

    // ループヘッダ（条件評価）
    ctx.switch_to_block(loop_header);
    if (for_stmt.cond) {
        LocalId cond = expr_lowering->lower_expression(*for_stmt.cond, ctx);
        ctx.set_terminator(MirTerminator::switch_int(MirOperand::copy(MirPlace{cond}),
                                                     {{1, loop_body}}, loop_exit));
    } else {
        // 条件なしの場合は無限ループ
        ctx.set_terminator(MirTerminator::goto_block(loop_body));
    }

    // ループボディ
    ctx.switch_to_block(loop_body);

    // continueの処理のため、更新部がある場合は特別なブロックを用意
    BlockId continue_target = loop_header;
    if (for_stmt.update) {
        // 更新式がある場合、continueは更新部を実行してからヘッダへ
        continue_target = ctx.new_block();
    }

    ctx.push_loop(loop_header, loop_exit, continue_target);

    // ループボディ用のスコープを作成（各反復でdefer文を実行）
    ctx.push_scope();

    // ボディの文を処理
    for (const auto& stmt : for_stmt.body) {
        lower_statement(*stmt, ctx);
    }

    // ループボディ終了時にdefer文を実行（逆順）
    auto defers = ctx.get_defer_stmts();
    for (const auto* defer_stmt : defers) {
        lower_statement(*defer_stmt, ctx);
    }

    ctx.pop_scope();

    // ボディの最後に更新式を追加（whileループと同じ構造）
    if (for_stmt.update && !ctx.get_current_block()->terminator) {
        // 更新式を直接lowering（結果は破棄）
        expr_lowering->lower_expression(*for_stmt.update, ctx);
    }

    ctx.pop_loop();

    // ループヘッダへ戻る
    if (!ctx.get_current_block()->terminator) {
        ctx.set_terminator(MirTerminator::goto_block(loop_header));
    }

    // continueターゲット（更新式がある場合）
    if (for_stmt.update && continue_target != loop_header) {
        ctx.switch_to_block(continue_target);
        // 更新式を直接lowering（結果は破棄）
        expr_lowering->lower_expression(*for_stmt.update, ctx);
        // ヘッダへ戻る
        ctx.set_terminator(MirTerminator::goto_block(loop_header));
    }

    // ループ出口
    ctx.switch_to_block(loop_exit);
}

// loop文のlowering
void StmtLowering::lower_loop(const hir::HirLoop& loop_stmt, LoweringContext& ctx) {
    // ブロックを作成
    BlockId loop_block = ctx.new_block();
    BlockId loop_exit = ctx.new_block();

    // ループブロックへジャンプ
    ctx.set_terminator(MirTerminator::goto_block(loop_block));

    // ループボディ
    ctx.switch_to_block(loop_block);
    ctx.push_loop(loop_block, loop_exit);
    for (const auto& stmt : loop_stmt.body) {
        lower_statement(*stmt, ctx);
    }
    ctx.pop_loop();
    if (!ctx.get_current_block()->terminator) {
        // 無限ループ
        ctx.set_terminator(MirTerminator::goto_block(loop_block));
    }

    // ループ出口（breakで到達）
    ctx.switch_to_block(loop_exit);
}

// switch文のlowering
void StmtLowering::lower_switch(const hir::HirSwitch& switch_stmt, LoweringContext& ctx) {
    // 判別式をlowering
    LocalId discriminant = expr_lowering->lower_expression(*switch_stmt.expr, ctx);

    // 各caseのブロックを作成
    std::vector<std::pair<int64_t, BlockId>> cases;
    std::vector<BlockId> case_blocks;
    for (size_t i = 0; i < switch_stmt.cases.size(); ++i) {
        // else/defaultケース（patternがnull）はスキップ
        if (!switch_stmt.cases[i].pattern) {
            case_blocks.push_back(0);  // プレースホルダー
            continue;
        }

        BlockId case_block = ctx.new_block();
        case_blocks.push_back(case_block);

        // case値を評価
        int64_t case_value = 0;

        // patternがSingleValueの場合、その値を取得
        if (switch_stmt.cases[i].pattern &&
            switch_stmt.cases[i].pattern->kind == hir::HirSwitchPattern::SingleValue &&
            switch_stmt.cases[i].pattern->value) {
            // pattern->valueから値を取得
            if (auto lit = std::get_if<std::unique_ptr<hir::HirLiteral>>(
                    &switch_stmt.cases[i].pattern->value->kind)) {
                if (*lit) {
                    auto& val = (*lit)->value;
                    if (std::holds_alternative<int64_t>(val)) {
                        case_value = std::get<int64_t>(val);
                    } else if (std::holds_alternative<char>(val)) {
                        case_value = static_cast<int64_t>(std::get<char>(val));
                    }
                }
            }
        }
        // 旧互換性のためvalueフィールドも確認
        else if (switch_stmt.cases[i].value) {
            if (auto lit = std::get_if<std::unique_ptr<hir::HirLiteral>>(
                    &switch_stmt.cases[i].value->kind)) {
                if (*lit) {
                    auto& val = (*lit)->value;
                    if (std::holds_alternative<int64_t>(val)) {
                        case_value = std::get<int64_t>(val);
                    } else if (std::holds_alternative<char>(val)) {
                        case_value = static_cast<int64_t>(std::get<char>(val));
                    }
                }
            }
        }
        cases.push_back({case_value, case_block});
    }

    // defaultブロック
    BlockId default_block = ctx.new_block();
    BlockId exit_block = ctx.new_block();

    // switch終端命令
    ctx.set_terminator(
        MirTerminator::switch_int(MirOperand::copy(MirPlace{discriminant}), cases, default_block));

    // 各caseをlowering（else/default以外）
    for (size_t i = 0; i < switch_stmt.cases.size(); ++i) {
        // else/defaultケース（patternがnull）はスキップ
        if (!switch_stmt.cases[i].pattern) {
            continue;
        }
        ctx.switch_to_block(case_blocks[i]);
        for (const auto& stmt : switch_stmt.cases[i].stmts) {
            lower_statement(*stmt, ctx);
        }
        if (!ctx.get_current_block()->terminator) {
            ctx.set_terminator(MirTerminator::goto_block(exit_block));
        }
    }

    // defaultをlowering
    ctx.switch_to_block(default_block);
    // else句（patternがnullのcase）を探して処理
    for (const auto& case_item : switch_stmt.cases) {
        if (!case_item.pattern) {  // else/defaultケース
            for (const auto& stmt : case_item.stmts) {
                lower_statement(*stmt, ctx);
            }
            break;
        }
    }
    if (!ctx.get_current_block()->terminator) {
        ctx.set_terminator(MirTerminator::goto_block(exit_block));
    }

    // 出口ブロック
    ctx.switch_to_block(exit_block);
}

// block文のlowering
void StmtLowering::lower_block(const hir::HirBlock& block, LoweringContext& ctx) {
    // 新しいスコープを開始
    ctx.push_scope();

    // ブロック内の各文をlowering
    for (const auto& stmt : block.stmts) {
        lower_statement(*stmt, ctx);
    }

    // スコープ終了時にdefer文を実行（逆順）
    auto defers = ctx.get_defer_stmts();
    for (const auto* defer_stmt : defers) {
        lower_statement(*defer_stmt, ctx);
    }

    // スコープ終了時にデストラクタを呼び出し
    emit_scope_destructors(ctx);

    // スコープを終了
    ctx.pop_scope();
}

// defer文のlowering
void StmtLowering::lower_defer(const hir::HirDefer& defer_stmt, LoweringContext& ctx) {
    // defer文を現在のスコープに登録
    // defer文は、スコープ終了時に逆順で実行される
    if (defer_stmt.body) {
        ctx.add_defer(defer_stmt.body.get());
    }
}

// スコープ終了時のデストラクタ呼び出しを生成
void StmtLowering::emit_scope_destructors(LoweringContext& ctx) {
    auto destructor_vars = ctx.get_current_scope_destructor_vars();
    for (const auto& [local_id, type_name] : destructor_vars) {
        // 登録時の型名を優先使用（ジェネリック型の場合、マングル済み名が登録されている）
        // ローカル変数の型名はモノモフィゼーション前なので不正確な場合がある
        std::string actual_type_name = type_name;

        // 登録時の型名がマングル済み（__を含む）の場合はそのまま使用
        // そうでない場合はローカル変数の型名を確認
        if (type_name.find("__") == std::string::npos && local_id < ctx.func->locals.size()) {
            const auto& local_decl = ctx.func->locals[local_id];
            if (local_decl.type && !local_decl.type->name.empty() &&
                local_decl.type->name.find("__") != std::string::npos) {
                // ローカル変数の型名がマングル済みならそれを使用
                actual_type_name = local_decl.type->name;
            }
        }

        // ネストジェネリック型名の正規化（Vector<int> → Vector__int）
        if (actual_type_name.find('<') != std::string::npos) {
            std::string result;
            for (char c : actual_type_name) {
                if (c == '<' || c == '>') {
                    if (c == '<')
                        result += "__";
                } else if (c == ',' || c == ' ') {
                    // カンマと空白は省略
                } else {
                    result += c;
                }
            }
            actual_type_name = result;
        }

        std::string dtor_name = actual_type_name + "__dtor";

        // デストラクタ呼び出しを生成（selfはポインタとして渡す）
        hir::TypePtr local_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
        local_type->name = actual_type_name;
        LocalId ref_temp = ctx.new_temp(hir::make_pointer(local_type));
        ctx.push_statement(
            MirStatement::assign(MirPlace{ref_temp}, MirRvalue::ref(MirPlace{local_id}, false)));

        std::vector<MirOperandPtr> args;
        args.push_back(MirOperand::copy(MirPlace{ref_temp}));

        BlockId success_block = ctx.new_block();

        auto func_operand = MirOperand::function_ref(dtor_name);
        auto call_term = std::make_unique<MirTerminator>();
        call_term->kind = MirTerminator::Call;
        call_term->data = MirTerminator::CallData{std::move(func_operand),
                                                  std::move(args),
                                                  std::nullopt,  // void戻り値
                                                  success_block,
                                                  std::nullopt,
                                                  "",
                                                  "",
                                                  false};
        ctx.set_terminator(std::move(call_term));
        ctx.switch_to_block(success_block);
    }
}

// インラインアセンブリのlowering
void StmtLowering::lower_asm(const hir::HirAsm& asm_stmt, LoweringContext& ctx) {
    debug_msg("mir_asm", "[MIR] lower_asm: " + asm_stmt.code +
                             " operands=" + std::to_string(asm_stmt.operands.size()));

    // オペランドを変換: 変数名 → LocalId、またはmacro/const → 定数値
    std::vector<MirStatement::MirAsmOperand> mir_operands;
    for (const auto& operand : asm_stmt.operands) {
        // HIRレベルで既に定数として解決されている場合
        if (operand.is_constant) {
            mir_operands.push_back(
                MirStatement::MirAsmOperand(operand.constraint, operand.const_value));
            debug_msg("mir_asm", "[MIR] operand: " + operand.constraint +
                                     " -> const_value=" + std::to_string(operand.const_value));
            continue;
        }

        // i/n制約の場合は優先的にconst_valueを検索
        bool isImmediateConstraint = (operand.constraint.find('i') != std::string::npos ||
                                      operand.constraint.find('n') != std::string::npos);

        if (isImmediateConstraint) {
            // i/n制約: 定数値が必要なので優先的にconst_valueを検索
            auto const_val_opt = ctx.get_const_value(operand.var_name);
            if (const_val_opt) {
                // 定数値を取得（整数のみサポート）
                int64_t val = 0;
                if (std::holds_alternative<int64_t>(const_val_opt->value)) {
                    val = std::get<int64_t>(const_val_opt->value);
                } else if (std::holds_alternative<double>(const_val_opt->value)) {
                    val = static_cast<int64_t>(std::get<double>(const_val_opt->value));
                }
                mir_operands.push_back(MirStatement::MirAsmOperand(operand.constraint, val));
                debug_msg("mir_asm", "[MIR] operand: " + operand.constraint + ":" +
                                         operand.var_name +
                                         " -> const_value=" + std::to_string(val));
                continue;  // 次のオペランドへ
            }
            // const_valueが見つからない場合はエラー（i/n制約には定数が必要）
            debug_msg("mir_asm",
                      "[MIR] WARNING: i/n constraint requires constant: " + operand.var_name);
        }

        // 変数名をローカル変数テーブルから検索
        auto local_id_opt = ctx.resolve_variable(operand.var_name);
        if (local_id_opt) {
            mir_operands.push_back(MirStatement::MirAsmOperand(operand.constraint, *local_id_opt));
            debug_msg("mir_asm", "[MIR] operand: " + operand.constraint + ":" + operand.var_name +
                                     " -> local_id=" + std::to_string(*local_id_opt));
        } else {
            // 変数が見つからない場合、macro/const定数として検索
            auto const_val_opt = ctx.get_const_value(operand.var_name);
            if (const_val_opt) {
                // 定数値を取得（整数のみサポート）
                int64_t val = 0;
                if (std::holds_alternative<int64_t>(const_val_opt->value)) {
                    val = std::get<int64_t>(const_val_opt->value);
                } else if (std::holds_alternative<double>(const_val_opt->value)) {
                    val = static_cast<int64_t>(std::get<double>(const_val_opt->value));
                }
                mir_operands.push_back(MirStatement::MirAsmOperand(operand.constraint, val));
                debug_msg("mir_asm", "[MIR] operand: " + operand.constraint + ":" +
                                         operand.var_name +
                                         " -> const_value=" + std::to_string(val));
            } else {
                debug_msg("mir_asm",
                          "[MIR] WARNING: variable or constant not found: " + operand.var_name);
            }
        }
    }

    ctx.push_statement(MirStatement::asm_stmt(asm_stmt.code, asm_stmt.is_must,
                                              std::move(mir_operands), asm_stmt.clobbers));
}

// must {} ブロックのlowering（最適化禁止）
void StmtLowering::lower_must_block(const hir::HirMustBlock& must_block, LoweringContext& ctx) {
    debug_msg("mir_must", "[MIR] lower_must_block");

    // mustブロック開始：最適化禁止フラグをON
    ctx.in_must_block = true;

    // mustブロック内の各文をlowering
    for (const auto& stmt : must_block.body) {
        lower_statement(*stmt, ctx);
    }

    // mustブロック終了：最適化禁止フラグをOFF
    ctx.in_must_block = false;
}

}  // namespace cm::mir