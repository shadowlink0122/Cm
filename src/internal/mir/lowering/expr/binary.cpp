// MIR lowering - 二項演算式

#include "internal/base/debug.hpp"
#include "internal/mir/lowering/expr.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cm::mir {

LocalId ExprLowering::lower_binary(const hir::HirBinary& bin, LoweringContext& ctx) {
    // 代入演算の処理
    if (bin.op == hir::HirBinaryOp::Assign) {
        // Bug#14修正: 右辺が配列リテラルの場合、temp経由のcopyを避けて
        // 直接ターゲット変数の各インデックスに要素を書き込む。
        // temp経由copyでは構造体要素の配列で正しくコピーされない問題がある。
        if (auto* arr_lit_ptr =
                std::get_if<std::unique_ptr<hir::HirArrayLiteral>>(&bin.rhs->kind)) {
            const auto& arr_lit = **arr_lit_ptr;

            // 左辺が単純な変数参照の場合
            if (auto* var_ref = std::get_if<std::unique_ptr<hir::HirVarRef>>(&bin.lhs->kind)) {
                auto lhs_opt = ctx.resolve_variable((*var_ref)->name);
                if (lhs_opt) {
                    MirPlace base_place{*lhs_opt};
                    for (size_t i = 0; i < arr_lit.elements.size(); ++i) {
                        LocalId elem_value = lower_expression(*arr_lit.elements[i], ctx);

                        // インデックス用の定数を変数に格納
                        LocalId idx_local = ctx.new_temp(hir::make_int());
                        MirConstant idx_const;
                        idx_const.value = static_cast<int64_t>(i);
                        idx_const.type = hir::make_int();
                        ctx.push_statement(MirStatement::assign(
                            MirPlace{idx_local}, MirRvalue::use(MirOperand::constant(idx_const))));

                        // ターゲットの配列要素への代入を生成
                        MirPlace elem_place = base_place;
                        elem_place.projections.push_back(PlaceProjection::index(idx_local));
                        ctx.push_statement(MirStatement::assign(
                            elem_place, MirRvalue::use(MirOperand::copy(MirPlace{elem_value}))));
                    }
                    // 最後の要素の値を返す（代入式の戻り値）
                    if (!arr_lit.elements.empty()) {
                        return *lhs_opt;
                    }
                }
            }
        }

        // 右辺を先に評価
        // 配列リテラルRHSは代入先の型を期待型として渡す（`h.vs = []` のような空リテラルが要素型int既定に落ちるのを防ぐ）
        hir::TypePtr assign_target_type = bin.lhs ? bin.lhs->type : nullptr;
        if ((!assign_target_type || assign_target_type->kind != hir::TypeKind::Array) && bin.lhs) {
            if (auto* mem = std::get_if<std::unique_ptr<hir::HirMember>>(&bin.lhs->kind)) {
                const auto& obj = (*mem)->object;
                if (obj && obj->type && obj->type->kind == hir::TypeKind::Struct &&
                    ctx.struct_defs && ctx.struct_defs->count(obj->type->name)) {
                    const auto* struct_def = ctx.struct_defs->at(obj->type->name);
                    for (const auto& f : struct_def->fields) {
                        if (f.name == (*mem)->member) {
                            assign_target_type = f.type;
                            break;
                        }
                    }
                }
            }
        }

        LocalId rhs_value;
        if (auto* rhs_arr_lit = std::get_if<std::unique_ptr<hir::HirArrayLiteral>>(&bin.rhs->kind);
            rhs_arr_lit && assign_target_type && assign_target_type->kind == hir::TypeKind::Array) {
            rhs_value = lower_array_literal(**rhs_arr_lit, assign_target_type, ctx);
        } else {
            rhs_value = lower_expression(*bin.rhs, ctx);
        }

        // 左辺値のMirPlaceを構築するヘルパー関数
        // 複雑な左辺値（c.values[0], points[0].x など）を再帰的に処理
        std::function<bool(const hir::HirExpr*, MirPlace&, hir::TypePtr&)> build_lvalue_place;
        build_lvalue_place = [&](const hir::HirExpr* expr, MirPlace& place,
                                 hir::TypePtr& current_type) -> bool {
            if (auto* var_ref = std::get_if<std::unique_ptr<hir::HirVarRef>>(&expr->kind)) {
                // ベース変数
                auto var_id = ctx.resolve_variable((*var_ref)->name);
                if (var_id) {
                    place.local = *var_id;
                    if (*var_id < ctx.func->locals.size()) {
                        current_type = ctx.func->locals[*var_id].type;
                    }
                    return true;
                }
                return false;
            } else if (auto* member = std::get_if<std::unique_ptr<hir::HirMember>>(&expr->kind)) {
                // メンバーアクセス: object.member
                hir::TypePtr inner_type;
                if (!build_lvalue_place((*member)->object.get(), place, inner_type)) {
                    return false;
                }

                // ポインタ型の場合、デリファレンスを追加
                if (inner_type && inner_type->kind == hir::TypeKind::Pointer) {
                    place.projections.push_back(PlaceProjection::deref());
                    inner_type = inner_type->element_type;
                }

                // フィールドプロジェクションを追加
                std::string field_name = (*member)->member;
                if (inner_type && inner_type->kind == hir::TypeKind::Struct) {
                    auto field_idx = ctx.get_field_index(inner_type->name, field_name);
                    if (field_idx) {
                        place.projections.push_back(PlaceProjection::field(*field_idx));

                        // 次の型を取得
                        // ジェネリック構造体の場合はベース構造体名で検索し、フィールド型がジェネリックパラメータなら置換する
                        std::string lookup_name = inner_type->name;
                        if (ctx.struct_defs && ctx.struct_defs->count(lookup_name)) {
                            const auto* struct_def = ctx.struct_defs->at(lookup_name);
                            if (*field_idx < struct_def->fields.size()) {
                                hir::TypePtr field_type = struct_def->fields[*field_idx].type;

                                // フィールド型がジェネリックパラメータの場合、type_argsから置換
                                // 例: Node<Item>のfield "data: T" → T=Item
                                if (field_type && field_type->kind == hir::TypeKind::Generic &&
                                    !inner_type->type_args.empty()) {
                                    // ジェネリックパラメータ名を型引数にマッピング
                                    for (size_t i = 0; i < struct_def->generic_params.size() &&
                                                       i < inner_type->type_args.size();
                                         ++i) {
                                        if (struct_def->generic_params[i].name ==
                                            field_type->name) {
                                            field_type = inner_type->type_args[i];
                                            break;
                                        }
                                    }
                                }
                                current_type = field_type;
                            }
                        }
                        return true;
                    }
                }
                return false;
            } else if (auto* index = std::get_if<std::unique_ptr<hir::HirIndex>>(&expr->kind)) {
                // インデックスアクセス: object[index] または object[i][j][k]...（多次元）
                hir::TypePtr inner_type;
                if (!build_lvalue_place((*index)->object.get(), place, inner_type)) {
                    return false;
                }

                // 多次元配列最適化: indices が設定されている場合、全インデックスを処理
                if (!(*index)->indices.empty()) {
                    // 多次元: 全インデックスをプロジェクションとして追加
                    for (const auto& idx_expr : (*index)->indices) {
                        LocalId idx = lower_expression(*idx_expr, ctx);
                        place.projections.push_back(PlaceProjection::index(idx));
                        // 型を更新（配列またはポインタの要素型）
                        if (inner_type && inner_type->element_type) {
                            if (inner_type->kind == hir::TypeKind::Array ||
                                inner_type->kind == hir::TypeKind::Pointer) {
                                inner_type = inner_type->element_type;
                            }
                        }
                    }
                    current_type = inner_type;
                } else {
                    // 単一インデックス（後方互換性）
                    LocalId idx = lower_expression(*(*index)->index, ctx);
                    place.projections.push_back(PlaceProjection::index(idx));
                    // 次の型を取得（配列またはポインタの要素型）
                    if (inner_type && inner_type->element_type) {
                        if (inner_type->kind == hir::TypeKind::Array ||
                            inner_type->kind == hir::TypeKind::Pointer) {
                            current_type = inner_type->element_type;
                        }
                    }
                }
                return true;
            } else if (auto* unary = std::get_if<std::unique_ptr<hir::HirUnary>>(&expr->kind)) {
                // デリファレンス: *ptr
                if ((*unary)->op == hir::HirUnaryOp::Deref) {
                    hir::TypePtr inner_type;
                    if (!build_lvalue_place((*unary)->operand.get(), place, inner_type)) {
                        // 通常のポインタ式の場合
                        LocalId ptr = lower_expression(*(*unary)->operand, ctx);
                        place.local = ptr;
                        place.projections.push_back(PlaceProjection::deref());

                        // 要素型を取得
                        if ((*unary)->operand->type &&
                            (*unary)->operand->type->kind == hir::TypeKind::Pointer &&
                            (*unary)->operand->type->element_type) {
                            current_type = (*unary)->operand->type->element_type;
                        }
                        return true;
                    }

                    // ネストした左辺値の場合
                    place.projections.push_back(PlaceProjection::deref());

                    // 要素型を取得
                    if (inner_type && inner_type->kind == hir::TypeKind::Pointer &&
                        inner_type->element_type) {
                        current_type = inner_type->element_type;
                    }
                    return true;
                }
            }
            return false;
        };

        // 左辺値のMirPlaceを構築
        MirPlace place{0};
        hir::TypePtr current_type;

        if (build_lvalue_place(bin.lhs.get(), place, current_type)) {
            // スライスへのインデックス書き込みはCmSlice*への直接GEPになり不正（SIGBUS）なため、要素ポインタ経由のデリファレンス格納へ正規化する
            hir::TypePtr walk_type = nullptr;
            if (place.local < ctx.func->locals.size()) {
                walk_type = ctx.func->locals[place.local].type;
            }
            for (size_t pi = 0; pi < place.projections.size(); ++pi) {
                const auto& proj = place.projections[pi];
                bool is_slice_base = walk_type && walk_type->kind == hir::TypeKind::Array &&
                                     !walk_type->array_size.has_value();
                if (proj.kind == ProjectionKind::Index && is_slice_base) {
                    // スライス値（CmSlice*）を一時変数へロード
                    MirPlace slice_place = place;
                    slice_place.projections.resize(pi);
                    LocalId slice_local = ctx.new_temp(walk_type);
                    ctx.push_statement(MirStatement::assign(
                        MirPlace{slice_local}, MirRvalue::use(MirOperand::copy(slice_place))));

                    // 要素ポインタを取得
                    hir::TypePtr elem_type =
                        walk_type->element_type ? walk_type->element_type : hir::make_int();
                    LocalId elem_ptr = ctx.new_temp(hir::make_pointer(elem_type));
                    BlockId next_block = ctx.new_block();
                    std::vector<MirOperandPtr> ep_args;
                    ep_args.push_back(MirOperand::copy(MirPlace{slice_local}));
                    ep_args.push_back(MirOperand::copy(MirPlace{proj.index_local}));
                    auto ep_term = std::make_unique<MirTerminator>();
                    ep_term->kind = MirTerminator::Call;
                    ep_term->data = MirTerminator::CallData{
                        MirOperand::function_ref("cm_slice_get_element_ptr"),
                        std::move(ep_args),
                        MirPlace{elem_ptr},
                        next_block,
                        std::nullopt,
                        "",
                        "",
                        false};
                    ctx.set_terminator(std::move(ep_term));
                    ctx.switch_to_block(next_block);

                    // 残りのプロジェクションをデリファレンス基点へ付け替える
                    MirPlace new_place{elem_ptr};
                    new_place.projections.push_back(PlaceProjection::deref());
                    for (size_t rest = pi + 1; rest < place.projections.size(); ++rest) {
                        new_place.projections.push_back(place.projections[rest]);
                    }
                    place = new_place;
                    walk_type = elem_type;
                    pi = 0;  // 新しいplaceの先頭（deref）から再走査
                    continue;
                }
                // 型を追跡
                if (proj.kind == ProjectionKind::Index || proj.kind == ProjectionKind::Deref) {
                    walk_type = walk_type ? walk_type->element_type : nullptr;
                } else if (proj.kind == ProjectionKind::Field) {
                    if (walk_type && walk_type->kind == hir::TypeKind::Struct && ctx.struct_defs &&
                        ctx.struct_defs->count(walk_type->name)) {
                        const auto* sd = ctx.struct_defs->at(walk_type->name);
                        walk_type = proj.field_id < sd->fields.size()
                                        ? sd->fields[proj.field_id].type
                                        : nullptr;
                    } else {
                        walk_type = nullptr;
                    }
                }
            }

            // ユニオン型の左辺への変種値の代入はCast（ユニオン構築）を経由して
            // タグ+ペイロードを書き込む（直接storeするとタグ未設定になり、
            // `as` のタグ検査パニックや `is` の誤判定になる）
            hir::TypePtr lhs_resolved = ctx.resolve_typedef(
                current_type ? current_type
                             : (place.projections.empty() && place.local < ctx.func->locals.size()
                                    ? ctx.func->locals[place.local].type
                                    : nullptr));
            hir::TypePtr rhs_resolved = (rhs_value < ctx.func->locals.size())
                                            ? ctx.resolve_typedef(ctx.func->locals[rhs_value].type)
                                            : nullptr;
            if (lhs_resolved && lhs_resolved->kind == hir::TypeKind::Union &&
                (!rhs_resolved || rhs_resolved->kind != hir::TypeKind::Union)) {
                ctx.push_statement(MirStatement::assign(
                    place, MirRvalue::cast(MirOperand::copy(MirPlace{rhs_value}), lhs_resolved)));
                return rhs_value;
            }

            ctx.push_statement(
                MirStatement::assign(place, MirRvalue::use(MirOperand::copy(MirPlace{rhs_value}))));
            return rhs_value;
        }

        // その他の左辺（エラー）は評価済みの右辺値を返す
        return rhs_value;
    }

    // 論理演算 (AND/OR) - 短絡評価を実装
    if (bin.op == hir::HirBinaryOp::And) {
        // AND演算の短絡評価
        // 左辺を評価
        LocalId lhs = lower_expression(*bin.lhs, ctx);

        // 結果を格納する変数
        LocalId result = ctx.new_temp(hir::make_bool());

        // ブロックを作成
        BlockId eval_rhs = ctx.new_block();  // 右辺を評価するブロック
        BlockId skip_rhs = ctx.new_block();  // 右辺をスキップするブロック（結果はfalse）
        BlockId merge = ctx.new_block();  // 結果を統合するブロック

        // 左辺がtrueなら右辺を評価、falseならスキップ
        ctx.set_terminator(
            MirTerminator::switch_int(MirOperand::copy(MirPlace{lhs}), {{1, eval_rhs}}, skip_rhs));

        // 右辺を評価するブロック
        ctx.switch_to_block(eval_rhs);
        LocalId rhs = lower_expression(*bin.rhs, ctx);
        // 結果は右辺の値（左辺は既にtrue）
        ctx.push_statement(MirStatement::assign(MirPlace{result},
                                                MirRvalue::use(MirOperand::copy(MirPlace{rhs}))));
        ctx.set_terminator(MirTerminator::goto_block(merge));

        // 右辺をスキップするブロック（左辺がfalse）
        ctx.switch_to_block(skip_rhs);
        // 結果はfalse
        MirConstant false_const;
        false_const.type = hir::make_bool();
        false_const.value = false;
        ctx.push_statement(MirStatement::assign(MirPlace{result},
                                                MirRvalue::use(MirOperand::constant(false_const))));
        ctx.set_terminator(MirTerminator::goto_block(merge));

        // マージブロック
        ctx.switch_to_block(merge);

        return result;
    }

    if (bin.op == hir::HirBinaryOp::Or) {
        // OR演算の短絡評価
        // 左辺を評価
        LocalId lhs = lower_expression(*bin.lhs, ctx);

        // 結果を格納する変数
        LocalId result = ctx.new_temp(hir::make_bool());

        // ブロックを作成
        BlockId skip_rhs = ctx.new_block();  // 右辺をスキップするブロック（結果はtrue）
        BlockId eval_rhs = ctx.new_block();  // 右辺を評価するブロック
        BlockId merge = ctx.new_block();     // 結果を統合するブロック

        // 左辺がtrueならスキップ、falseなら右辺を評価
        ctx.set_terminator(
            MirTerminator::switch_int(MirOperand::copy(MirPlace{lhs}), {{1, skip_rhs}}, eval_rhs));

        // 右辺をスキップするブロック（左辺がtrue）
        ctx.switch_to_block(skip_rhs);
        // 結果はtrue
        MirConstant true_const;
        true_const.type = hir::make_bool();
        true_const.value = true;
        ctx.push_statement(MirStatement::assign(MirPlace{result},
                                                MirRvalue::use(MirOperand::constant(true_const))));
        ctx.set_terminator(MirTerminator::goto_block(merge));

        // 右辺を評価するブロック
        ctx.switch_to_block(eval_rhs);
        LocalId rhs = lower_expression(*bin.rhs, ctx);
        // 結果は右辺の値（左辺は既にfalse）
        ctx.push_statement(MirStatement::assign(MirPlace{result},
                                                MirRvalue::use(MirOperand::copy(MirPlace{rhs}))));
        ctx.set_terminator(MirTerminator::goto_block(merge));

        // マージブロック
        ctx.switch_to_block(merge);

        return result;
    }

    // 通常の二項演算
    // 左辺と右辺をlowering
    LocalId lhs = lower_expression(*bin.lhs, ctx);
    LocalId rhs = lower_expression(*bin.rhs, ctx);

    // 構造体の比較演算子の特別処理（with による自動実装）
    if (bin.op == hir::HirBinaryOp::Eq || bin.op == hir::HirBinaryOp::Ne) {
        // 左辺が構造体型かチェック
        if (bin.lhs->type && bin.lhs->type->kind == hir::TypeKind::Struct) {
            std::string type_name = bin.lhs->type->name;

            // impl_info で Eq が実装されているかチェック
            auto& current_impl_info = get_impl_info();
            auto type_it = current_impl_info.find(type_name);
            if (type_it != current_impl_info.end()) {
                // 任意のインターフェース（Eq）で op_eq が実装されているかチェック
                std::string op_func_name;
                for (const auto& [iface_name, func_name] : type_it->second) {
                    // Eq インターフェースの実装を探す
                    if (iface_name == "Eq" || func_name.find("__op_eq") != std::string::npos) {
                        op_func_name = type_name + "__op_eq";
                        break;
                    }
                }

                if (!op_func_name.empty()) {
                    // 自動生成された演算子関数を呼び出す
                    // Point__op_eq(self, other) - 両方とも値渡し

                    // 結果用変数
                    LocalId result = ctx.new_temp(hir::make_bool());
                    BlockId success_block = ctx.new_block();

                    // 引数を準備（両方とも値渡し）
                    std::vector<MirOperandPtr> args;
                    args.push_back(MirOperand::copy(MirPlace{lhs}));  // self (値)
                    args.push_back(MirOperand::copy(MirPlace{rhs}));  // other (値)

                    // 関数呼び出し
                    auto func_operand = MirOperand::function_ref(op_func_name);
                    auto call_term = std::make_unique<MirTerminator>();
                    call_term->kind = MirTerminator::Call;
                    call_term->data = MirTerminator::CallData{std::move(func_operand),
                                                              std::move(args),
                                                              MirPlace{result},
                                                              success_block,
                                                              std::nullopt,
                                                              "",
                                                              "",
                                                              false};  // 通常の関数呼び出し
                    ctx.set_terminator(std::move(call_term));
                    ctx.switch_to_block(success_block);

                    // != の場合は結果を反転
                    if (bin.op == hir::HirBinaryOp::Ne) {
                        LocalId neg_result = ctx.new_temp(hir::make_bool());
                        auto unary_rvalue = std::make_unique<MirRvalue>();
                        unary_rvalue->kind = MirRvalue::UnaryOp;
                        unary_rvalue->data = MirRvalue::UnaryOpData{
                            MirUnaryOp::Not, MirOperand::copy(MirPlace{result})};
                        ctx.push_statement(
                            MirStatement::assign(MirPlace{neg_result}, std::move(unary_rvalue)));
                        return neg_result;
                    }

                    return result;
                }
            }
        }
    }

    // 構造体の順序演算子の特別処理（with Ord による自動実装）
    if (bin.op == hir::HirBinaryOp::Lt || bin.op == hir::HirBinaryOp::Le ||
        bin.op == hir::HirBinaryOp::Gt || bin.op == hir::HirBinaryOp::Ge) {
        // 左辺が構造体型かチェック
        if (bin.lhs->type && bin.lhs->type->kind == hir::TypeKind::Struct) {
            std::string type_name = bin.lhs->type->name;

            // impl_info で Ord が実装されているかチェック
            auto& current_impl_info = get_impl_info();
            auto type_it = current_impl_info.find(type_name);
            if (type_it != current_impl_info.end()) {
                // Ord インターフェースで op_lt が実装されているかチェック
                std::string op_func_name;
                for (const auto& [iface_name, func_name] : type_it->second) {
                    if (iface_name == "Ord" || func_name.find("__op_lt") != std::string::npos) {
                        op_func_name = type_name + "__op_lt";
                        break;
                    }
                }

                if (!op_func_name.empty()) {
                    LocalId result = ctx.new_temp(hir::make_bool());
                    BlockId success_block = ctx.new_block();

                    // a > b は b < a、a <= b は !(b < a) なので Gt と Le で引数を入れ替え、
                    // <= と >= は結果を反転する
                    std::vector<MirOperandPtr> args;
                    if (bin.op == hir::HirBinaryOp::Lt || bin.op == hir::HirBinaryOp::Ge) {
                        // a < b: __op_lt(a, b) / a >= b: !__op_lt(a, b)
                        args.push_back(MirOperand::copy(MirPlace{lhs}));
                        args.push_back(MirOperand::copy(MirPlace{rhs}));
                    } else {
                        // a > b: __op_lt(b, a) / a <= b: !__op_lt(b, a)
                        args.push_back(MirOperand::copy(MirPlace{rhs}));
                        args.push_back(MirOperand::copy(MirPlace{lhs}));
                    }

                    auto func_operand = MirOperand::function_ref(op_func_name);
                    auto call_term = std::make_unique<MirTerminator>();
                    call_term->kind = MirTerminator::Call;
                    call_term->data = MirTerminator::CallData{std::move(func_operand),
                                                              std::move(args),
                                                              MirPlace{result},
                                                              success_block,
                                                              std::nullopt,
                                                              "",
                                                              "",
                                                              false};  // 通常の関数呼び出し
                    ctx.set_terminator(std::move(call_term));
                    ctx.switch_to_block(success_block);

                    // <= と >= は !(b < a) と !(a < b) を計算
                    if (bin.op == hir::HirBinaryOp::Le || bin.op == hir::HirBinaryOp::Ge) {
                        // 結果を反転
                        LocalId neg_result = ctx.new_temp(hir::make_bool());
                        auto unary_rvalue = std::make_unique<MirRvalue>();
                        unary_rvalue->kind = MirRvalue::UnaryOp;
                        unary_rvalue->data = MirRvalue::UnaryOpData{
                            MirUnaryOp::Not, MirOperand::copy(MirPlace{result})};
                        ctx.push_statement(
                            MirStatement::assign(MirPlace{neg_result}, std::move(unary_rvalue)));
                        return neg_result;
                    }

                    return result;
                }
            }
        }
    }

    // 構造体の算術演算子の特別処理（impl for Add/Sub/Mul/Div/Mod）
    if (bin.op == hir::HirBinaryOp::Add || bin.op == hir::HirBinaryOp::Sub ||
        bin.op == hir::HirBinaryOp::Mul || bin.op == hir::HirBinaryOp::Div ||
        bin.op == hir::HirBinaryOp::Mod) {
        if (bin.lhs->type && bin.lhs->type->kind == hir::TypeKind::Struct) {
            std::string type_name = bin.lhs->type->name;

            // 対応するインターフェース名を決定
            std::string iface_name;
            std::string op_suffix;
            switch (bin.op) {
                case hir::HirBinaryOp::Add:
                    iface_name = "Add";
                    op_suffix = "op_add";
                    break;
                case hir::HirBinaryOp::Sub:
                    iface_name = "Sub";
                    op_suffix = "op_sub";
                    break;
                case hir::HirBinaryOp::Mul:
                    iface_name = "Mul";
                    op_suffix = "op_mul";
                    break;
                case hir::HirBinaryOp::Div:
                    iface_name = "Div";
                    op_suffix = "op_div";
                    break;
                case hir::HirBinaryOp::Mod:
                    iface_name = "Mod";
                    op_suffix = "op_mod";
                    break;
                default:
                    break;
            }

            // impl_infoでインターフェースが実装されているかチェック
            auto& current_impl_info = get_impl_info();
            auto type_it = current_impl_info.find(type_name);
            if (type_it != current_impl_info.end()) {
                std::string op_func_name;
                for (const auto& [iname, func_name] : type_it->second) {
                    if (iname == iface_name ||
                        func_name.find("__" + op_suffix) != std::string::npos) {
                        op_func_name = type_name + "__" + op_suffix;
                        break;
                    }
                }

                if (!op_func_name.empty()) {
                    // 戻り値型は構造体型（演算子の戻り値型）
                    auto result_type = bin.lhs->type;
                    LocalId result = ctx.new_temp(result_type);
                    BlockId success_block = ctx.new_block();

                    // 引数を準備（両方とも値渡し）
                    std::vector<MirOperandPtr> args;
                    args.push_back(MirOperand::copy(MirPlace{lhs}));  // self (値)
                    args.push_back(MirOperand::copy(MirPlace{rhs}));  // other (値)

                    // 関数呼び出し
                    auto func_operand = MirOperand::function_ref(op_func_name);
                    auto call_term = std::make_unique<MirTerminator>();
                    call_term->kind = MirTerminator::Call;
                    call_term->data = MirTerminator::CallData{std::move(func_operand),
                                                              std::move(args),
                                                              MirPlace{result},
                                                              success_block,
                                                              std::nullopt,
                                                              "",
                                                              "",
                                                              false};
                    ctx.set_terminator(std::move(call_term));
                    ctx.switch_to_block(success_block);

                    return result;
                }
            }
        }
    }

    // 文字列連結の特別処理
    if (bin.op == hir::HirBinaryOp::Add) {
        bool lhs_is_string = bin.lhs->type && bin.lhs->type->kind == hir::TypeKind::String;
        bool rhs_is_string = bin.rhs->type && bin.rhs->type->kind == hir::TypeKind::String;

        // どちらかが文字列型の場合、文字列連結として処理
        if (lhs_is_string || rhs_is_string) {
            std::vector<MirOperandPtr> args;

            // 左辺を文字列に変換（必要な場合）
            if (lhs_is_string) {
                args.push_back(MirOperand::copy(MirPlace{lhs}));
            } else {
                LocalId str_lhs = convert_to_string(lhs, bin.lhs->type, ctx);
                args.push_back(MirOperand::copy(MirPlace{str_lhs}));
            }

            // 右辺を文字列に変換（必要な場合）
            if (rhs_is_string) {
                args.push_back(MirOperand::copy(MirPlace{rhs}));
            } else {
                LocalId str_rhs = convert_to_string(rhs, bin.rhs->type, ctx);
                args.push_back(MirOperand::copy(MirPlace{str_rhs}));
            }

            // 文字列連結
            LocalId result = ctx.new_temp(hir::make_string());
            BlockId concat_success = ctx.new_block();

            auto concat_func_operand = MirOperand::function_ref("cm_string_concat");

            auto concat_call_term = std::make_unique<MirTerminator>();
            concat_call_term->kind = MirTerminator::Call;
            concat_call_term->data = MirTerminator::CallData{std::move(concat_func_operand),
                                                             std::move(args),
                                                             MirPlace{result},
                                                             concat_success,
                                                             std::nullopt,
                                                             "",
                                                             "",
                                                             false};  // 通常の関数呼び出し
            ctx.set_terminator(std::move(concat_call_term));
            ctx.switch_to_block(concat_success);

            return result;
        }
    }

    // MIRの二項演算子に変換
    MirBinaryOp mir_op;
    switch (bin.op) {
        case hir::HirBinaryOp::Add:
            mir_op = MirBinaryOp::Add;
            break;
        case hir::HirBinaryOp::Sub:
            mir_op = MirBinaryOp::Sub;
            break;
        case hir::HirBinaryOp::Mul:
            mir_op = MirBinaryOp::Mul;
            break;
        case hir::HirBinaryOp::Div:
            mir_op = MirBinaryOp::Div;
            break;
        case hir::HirBinaryOp::Mod:
            mir_op = MirBinaryOp::Mod;
            break;
        case hir::HirBinaryOp::BitAnd:
            mir_op = MirBinaryOp::BitAnd;
            break;
        case hir::HirBinaryOp::BitOr:
            mir_op = MirBinaryOp::BitOr;
            break;
        case hir::HirBinaryOp::BitXor:
            mir_op = MirBinaryOp::BitXor;
            break;
        case hir::HirBinaryOp::Shl:
            mir_op = MirBinaryOp::Shl;
            break;
        case hir::HirBinaryOp::Shr:
            mir_op = MirBinaryOp::Shr;
            break;
        case hir::HirBinaryOp::Eq:
            mir_op = MirBinaryOp::Eq;
            break;
        case hir::HirBinaryOp::Ne:
            mir_op = MirBinaryOp::Ne;
            break;
        case hir::HirBinaryOp::Lt:
            mir_op = MirBinaryOp::Lt;
            break;
        case hir::HirBinaryOp::Le:
            mir_op = MirBinaryOp::Le;
            break;
        case hir::HirBinaryOp::Gt:
            mir_op = MirBinaryOp::Gt;
            break;
        case hir::HirBinaryOp::Ge:
            mir_op = MirBinaryOp::Ge;
            break;
        default:
            // 未実装の演算子
            mir_op = MirBinaryOp::Add;  // プレースホルダー
    }

    // 結果型を決定
    // 比較演算子 -> bool
    // 算術演算子 -> 左辺の型（または型昇格）
    hir::TypePtr result_type;
    bool is_comparison =
        (mir_op == MirBinaryOp::Eq || mir_op == MirBinaryOp::Ne || mir_op == MirBinaryOp::Lt ||
         mir_op == MirBinaryOp::Le || mir_op == MirBinaryOp::Gt || mir_op == MirBinaryOp::Ge);

    if (is_comparison) {
        result_type = hir::make_bool();
    } else {
        // 算術演算の型昇格
        // float + double -> double, int + double -> double, etc.
        auto lhs_type = bin.lhs->type;
        auto rhs_type = bin.rhs->type;

        // HIRの型が利用できない、またはエラー型の場合、ローカル変数から型を取得（operator実装内の式など、型チェッカーが型を設定しない場合に対応）
        if ((!lhs_type || lhs_type->is_error()) && lhs < ctx.func->locals.size()) {
            lhs_type = ctx.func->locals[lhs].type;
        }
        if ((!rhs_type || rhs_type->is_error()) && rhs < ctx.func->locals.size()) {
            rhs_type = ctx.func->locals[rhs].type;
        }
        // ローカルの型もエラー型の場合は「不明」として扱い、エラー型が結果型に伝播しないようにする（int既定へフォールバック）。
        // 文字列補間式のパース経由など、型チェッカを通らないHIRで発生する
        if (lhs_type && lhs_type->is_error()) {
            lhs_type = nullptr;
        }
        if (rhs_type && rhs_type->is_error()) {
            rhs_type = nullptr;
        }

        if (lhs_type && rhs_type) {
            // doubleがあればdouble
            if (lhs_type->kind == hir::TypeKind::Double ||
                rhs_type->kind == hir::TypeKind::Double) {
                result_type = hir::make_double();
            }
            // floatがあればfloat
            else if (lhs_type->kind == hir::TypeKind::Float ||
                     rhs_type->kind == hir::TypeKind::Float) {
                result_type = hir::make_float();
            }
            // longがあればlong（unsigned区別: Bug2修正）
            else if (lhs_type->kind == hir::TypeKind::Long ||
                     rhs_type->kind == hir::TypeKind::Long ||
                     lhs_type->kind == hir::TypeKind::ULong ||
                     rhs_type->kind == hir::TypeKind::ULong) {
                // 片方でもULongならulong型を維持
                if (lhs_type->kind == hir::TypeKind::ULong ||
                    rhs_type->kind == hir::TypeKind::ULong) {
                    result_type = hir::make_ulong();
                } else {
                    result_type = hir::make_long();
                }
            }
            // 整数型のinteger promotion: 大きい方の型に昇格
            else {
                // 型のサイズ優先度を求めるヘルパー
                // int/uint(32bit) > short/ushort(16bit) > tiny/utiny(8bit)
                auto type_rank = [](hir::TypeKind kind) -> int {
                    switch (kind) {
                        case hir::TypeKind::Int:
                        case hir::TypeKind::UInt:
                            return 3;
                        case hir::TypeKind::Short:
                        case hir::TypeKind::UShort:
                            return 2;
                        case hir::TypeKind::Tiny:
                        case hir::TypeKind::UTiny:
                            return 1;
                        default:
                            return 3;  // デフォルトはint相当
                    }
                };
                int lhs_rank = type_rank(lhs_type->kind);
                int rhs_rank = type_rank(rhs_type->kind);
                if (lhs_rank >= rhs_rank) {
                    result_type = lhs_type;
                } else {
                    result_type = rhs_type;
                }
            }
        } else if (lhs_type) {
            result_type = lhs_type;
        } else if (rhs_type) {
            result_type = rhs_type;
        } else {
            result_type = hir::make_int();
        }
    }

    // 結果用の一時変数
    LocalId result = ctx.new_temp(result_type);

    // BinaryOp Rvalueを作成（ポインタ演算の場合は型情報を含める）
    auto bin_rvalue = std::make_unique<MirRvalue>();
    bin_rvalue->kind = MirRvalue::BinaryOp;
    bin_rvalue->data = MirRvalue::BinaryOpData{mir_op, MirOperand::copy(MirPlace{lhs}),
                                               MirOperand::copy(MirPlace{rhs}), result_type};

    ctx.push_statement(MirStatement::assign(MirPlace{result}, std::move(bin_rvalue)));

    return result;
}

}  // namespace cm::mir
