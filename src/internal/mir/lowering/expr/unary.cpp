// MIR lowering - 単項演算式（インクリメント/デクリメント用Place構築ヘルパーを含む）

#include "internal/base/debug.hpp"
#include "internal/mir/lowering/expr.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cm::mir {

// 左辺値式からprojection付きMirPlaceを構築する（インクリメント/デクリメント用）
// 対応: 変数 / メンバアクセス（ポインタ自動deref含む） / インデックス（多次元含む） /
// デリファレンス
static bool build_place_for_incdec(ExprLowering& lowering, const hir::HirExpr& e, MirPlace& place,
                                   hir::TypePtr& out_type, LoweringContext& ctx) {
    if (const auto* var_ref = std::get_if<std::unique_ptr<hir::HirVarRef>>(&e.kind)) {
        auto var_id = ctx.resolve_variable((*var_ref)->name);
        if (!var_id) {
            return false;
        }
        place.local = *var_id;
        out_type = (*var_id < ctx.func->locals.size()) ? ctx.func->locals[*var_id].type : nullptr;
        return true;
    }
    if (const auto* member = std::get_if<std::unique_ptr<hir::HirMember>>(&e.kind)) {
        hir::TypePtr inner;
        if (!(*member)->object ||
            !build_place_for_incdec(lowering, *(*member)->object, place, inner, ctx)) {
            return false;
        }
        // ポインタ型の場合は自動デリファレンス
        if (inner && inner->kind == hir::TypeKind::Pointer) {
            place.projections.push_back(PlaceProjection::deref());
            inner = inner->element_type;
        }
        if (!inner || inner->kind != hir::TypeKind::Struct) {
            return false;
        }
        auto field_idx = ctx.get_field_index(inner->name, (*member)->member);
        if (!field_idx) {
            return false;
        }
        place.projections.push_back(PlaceProjection::field(*field_idx));
        out_type = nullptr;
        if (ctx.struct_defs && ctx.struct_defs->count(inner->name)) {
            const auto* struct_def = ctx.struct_defs->at(inner->name);
            if (*field_idx < struct_def->fields.size()) {
                out_type = struct_def->fields[*field_idx].type;
            }
        }
        return true;
    }
    if (const auto* index = std::get_if<std::unique_ptr<hir::HirIndex>>(&e.kind)) {
        hir::TypePtr inner;
        if (!(*index)->object ||
            !build_place_for_incdec(lowering, *(*index)->object, place, inner, ctx)) {
            return false;
        }
        auto push_index = [&](const hir::HirExpr& idx_expr) {
            LocalId idx = lowering.lower_expression(idx_expr, ctx);
            place.projections.push_back(PlaceProjection::index(idx));
            if (inner && inner->element_type &&
                (inner->kind == hir::TypeKind::Array || inner->kind == hir::TypeKind::Pointer)) {
                inner = inner->element_type;
            }
        };
        if (!(*index)->indices.empty()) {
            // 多次元インデックス
            for (const auto& idx_expr : (*index)->indices) {
                if (idx_expr) {
                    push_index(*idx_expr);
                }
            }
        } else if ((*index)->index) {
            push_index(*(*index)->index);
        } else {
            return false;
        }
        out_type = inner;
        return true;
    }
    if (const auto* un = std::get_if<std::unique_ptr<hir::HirUnary>>(&e.kind)) {
        // デリファレンス: (*p).v++ / (*p)++ 等
        if ((*un)->op == hir::HirUnaryOp::Deref && (*un)->operand) {
            hir::TypePtr inner;
            if (!build_place_for_incdec(lowering, *(*un)->operand, place, inner, ctx)) {
                return false;
            }
            place.projections.push_back(PlaceProjection::deref());
            out_type = (inner && inner->element_type) ? inner->element_type : nullptr;
            return true;
        }
    }
    return false;
}

// 単項演算のlowering
LocalId ExprLowering::lower_unary(const hir::HirUnary& unary, LoweringContext& ctx) {
    // ?演算子（Result/Optionのエラー伝播）:
    //   tag == 0（Ok/Some）ならペイロードを返し、
    //   それ以外（Err/None）ならユニオン値を丸ごと関数の戻り値へコピーして早期returnする（Result<T,E>とResult<U,E>は同一のタグ付きユニオン表現のため直接コピーできる）
    if (unary.op == hir::HirUnaryOp::Try) {
        LocalId operand = lower_expression(*unary.operand, ctx);

        // タグ（field 0）を読み出す
        LocalId tag = ctx.new_temp(hir::make_int());
        MirPlace tag_place{operand};
        tag_place.projections.push_back(PlaceProjection::field(0));
        ctx.push_statement(
            MirStatement::assign(MirPlace{tag}, MirRvalue::use(MirOperand::copy(tag_place))));

        BlockId cont_block = ctx.new_block();
        BlockId propagate_block = ctx.new_block();
        ctx.set_terminator(MirTerminator::switch_int(MirOperand::copy(MirPlace{tag}),
                                                     {{0, cont_block}}, propagate_block));

        // Err/None: 戻り値ローカルへユニオン値をコピーして早期return
        ctx.switch_to_block(propagate_block);
        ctx.push_statement(MirStatement::assign(
            MirPlace{ctx.func->return_local}, MirRvalue::use(MirOperand::copy(MirPlace{operand}))));
        ctx.set_terminator(MirTerminator::return_value());

        // Ok/Some: ペイロード（field 1）を取り出して継続
        ctx.switch_to_block(cont_block);
        hir::TypePtr payload_type = hir::make_int();
        if (unary.operand->type && !unary.operand->type->type_args.empty() &&
            unary.operand->type->type_args[0]) {
            payload_type = unary.operand->type->type_args[0];
        }
        LocalId result = ctx.new_temp(payload_type);
        MirPlace payload_place{operand};
        payload_place.projections.push_back(PlaceProjection::field(1));
        ctx.push_statement(MirStatement::assign(MirPlace{result},
                                                MirRvalue::use(MirOperand::copy(payload_place))));
        return result;
    }

    // インクリメント/デクリメント演算子の処理
    if (unary.op == hir::HirUnaryOp::PreInc || unary.op == hir::HirUnaryOp::PostInc ||
        unary.op == hir::HirUnaryOp::PreDec || unary.op == hir::HirUnaryOp::PostDec) {
        // 変数参照を取得
        if (auto var_ref = std::get_if<std::unique_ptr<hir::HirVarRef>>(&unary.operand->kind)) {
            auto local_opt = ctx.resolve_variable((*var_ref)->name);
            if (local_opt) {
                LocalId var_id = *local_opt;

                // PostInc/PostDecの場合、まず現在の値を保存
                LocalId result = var_id;
                if (unary.op == hir::HirUnaryOp::PostInc || unary.op == hir::HirUnaryOp::PostDec) {
                    result = ctx.new_temp(unary.operand->type);
                    ctx.push_statement(MirStatement::assign(
                        MirPlace{result}, MirRvalue::use(MirOperand::copy(MirPlace{var_id}))));
                }

                // 1を加算または減算
                LocalId one = ctx.new_temp(hir::make_int());
                MirConstant one_const;
                one_const.type = hir::make_int();
                one_const.value = int64_t(1);
                ctx.push_statement(MirStatement::assign(
                    MirPlace{one}, MirRvalue::use(MirOperand::constant(one_const))));

                LocalId new_value = ctx.new_temp(unary.operand->type);
                MirBinaryOp op =
                    (unary.op == hir::HirUnaryOp::PreInc || unary.op == hir::HirUnaryOp::PostInc)
                        ? MirBinaryOp::Add
                        : MirBinaryOp::Sub;

                // BinaryOp Rvalueを作成
                auto bin_rvalue = std::make_unique<MirRvalue>();
                bin_rvalue->kind = MirRvalue::BinaryOp;
                bin_rvalue->data =
                    MirRvalue::BinaryOpData{op, MirOperand::copy(MirPlace{var_id}),
                                            MirOperand::copy(MirPlace{one}), unary.operand->type};

                ctx.push_statement(
                    MirStatement::assign(MirPlace{new_value}, std::move(bin_rvalue)));

                // 変数を更新
                ctx.push_statement(MirStatement::assign(
                    MirPlace{var_id}, MirRvalue::use(MirOperand::copy(MirPlace{new_value}))));

                // PreInc/PreDecの場合は新しい値を返す
                if (unary.op == hir::HirUnaryOp::PreInc || unary.op == hir::HirUnaryOp::PreDec) {
                    return new_value;
                }

                return result;
            }
        }

        // 変数以外の左辺値（配列要素・構造体フィールド・デリファレンス）:
        // projection付きplaceを構築して read-modify-write を生成する。
        // （以前はここに来ると黙って const 0 に置換され、a[0]++ / s.v-- が no-op になるバグがあった）
        {
            MirPlace place{0};  // localはbuild_place_for_incdecで設定される
            hir::TypePtr place_type;
            if (unary.operand &&
                build_place_for_incdec(*this, *unary.operand, place, place_type, ctx)) {
                hir::TypePtr value_type = unary.operand->type ? unary.operand->type : place_type;

                // 旧値をロード
                LocalId old_val = ctx.new_temp(value_type);
                MirPlace load_place = place;
                ctx.push_statement(MirStatement::assign(
                    MirPlace{old_val}, MirRvalue::use(MirOperand::copy(load_place, value_type))));

                // 1を加算または減算
                LocalId one = ctx.new_temp(hir::make_int());
                MirConstant one_const;
                one_const.type = hir::make_int();
                one_const.value = int64_t(1);
                ctx.push_statement(MirStatement::assign(
                    MirPlace{one}, MirRvalue::use(MirOperand::constant(one_const))));

                LocalId new_value = ctx.new_temp(value_type);
                MirBinaryOp op =
                    (unary.op == hir::HirUnaryOp::PreInc || unary.op == hir::HirUnaryOp::PostInc)
                        ? MirBinaryOp::Add
                        : MirBinaryOp::Sub;
                auto bin_rvalue = std::make_unique<MirRvalue>();
                bin_rvalue->kind = MirRvalue::BinaryOp;
                bin_rvalue->data =
                    MirRvalue::BinaryOpData{op, MirOperand::copy(MirPlace{old_val}, value_type),
                                            MirOperand::copy(MirPlace{one}), value_type};
                ctx.push_statement(
                    MirStatement::assign(MirPlace{new_value}, std::move(bin_rvalue)));

                // placeへ書き戻し
                ctx.push_statement(MirStatement::assign(
                    std::move(place),
                    MirRvalue::use(MirOperand::copy(MirPlace{new_value}, value_type))));

                // Pre系は新しい値、Post系は旧値を返す
                if (unary.op == hir::HirUnaryOp::PreInc || unary.op == hir::HirUnaryOp::PreDec) {
                    return new_value;
                }
                return old_val;
            }
        }

        // 未対応の左辺値: 従来どおり0を返す（サイレント誤コンパイル防止のため要診断強化）
        LocalId temp = ctx.new_temp(unary.operand->type);
        MirConstant zero_const;
        zero_const.type = unary.operand->type;
        zero_const.value = int64_t(0);
        ctx.push_statement(
            MirStatement::assign(MirPlace{temp}, MirRvalue::use(MirOperand::constant(zero_const))));
        return temp;
    }

    // アドレス取得 (&x)
    if (unary.op == hir::HirUnaryOp::AddrOf) {
        // 変数参照の場合、そのアドレスを取得
        if (auto var_ref = std::get_if<std::unique_ptr<hir::HirVarRef>>(&unary.operand->kind)) {
            // 関数参照の場合、関数ポインタとして処理
            if ((*var_ref)->is_function_ref) {
                // 関数ポインタ型を設定 - operandの型を使用
                hir::TypePtr func_ptr_type = unary.operand->type;
                if (!func_ptr_type) {
                    func_ptr_type = hir::make_function_ptr(hir::make_int(), {});
                }

                LocalId result = ctx.new_temp(func_ptr_type);
                ctx.push_statement(MirStatement::assign(
                    MirPlace{result}, MirRvalue::use(MirOperand::function_ref((*var_ref)->name))));
                return result;
            }

            auto local_opt = ctx.resolve_variable((*var_ref)->name);
            if (local_opt) {
                LocalId var_id = *local_opt;

                // 関数型の場合、アドレスを取得するのではなくそのまま関数ポインタとして扱う
                if (unary.operand->type && unary.operand->type->kind == hir::TypeKind::Function) {
                    // 関数ポインタ型の値をそのまま返す
                    LocalId result = ctx.new_temp(unary.operand->type);
                    ctx.push_statement(MirStatement::assign(
                        MirPlace{result}, MirRvalue::use(MirOperand::copy(MirPlace{var_id}))));
                    return result;
                }

                // ポインタ型を作成
                hir::TypePtr ptr_type = hir::make_pointer(unary.operand->type);
                LocalId result = ctx.new_temp(ptr_type);

                // Ref Rvalueを作成（変数への参照）
                auto ref_rvalue = std::make_unique<MirRvalue>();
                ref_rvalue->kind = MirRvalue::Ref;
                ref_rvalue->data = MirRvalue::RefData{BorrowKind::Mutable, MirPlace{var_id}};

                ctx.push_statement(MirStatement::assign(MirPlace{result}, std::move(ref_rvalue)));
                return result;
            }
        }

        // オペランドが既に関数ポインタ型の場合、そのまま返す
        if (unary.operand->type && unary.operand->type->kind == hir::TypeKind::Function) {
            return lower_expression(*unary.operand, ctx);
        }

        // 一般化パス: 左辺値式（ネストしたメンバ・インデックス・デリファレンス）から
        // プロジェクション付きPlaceを構築し、実体を直接参照する。
        // 旧実装はオブジェクト部分を一時変数へコピーしてそのアドレスを取っていたため、&h.pos.x や &h.vals[i] のような2段以上のアクセスで書き込みが実体に反映されなかった。&ptr[i]（ポインタ算術になるケース）のみ後続の特別処理に委ねる
        {
            bool is_ptr_index = false;
            if (auto idx0 = std::get_if<std::unique_ptr<hir::HirIndex>>(&unary.operand->kind)) {
                const hir::TypePtr& ot = (*idx0)->object ? (*idx0)->object->type : nullptr;
                is_ptr_index = ot && ot->kind == hir::TypeKind::Pointer;
            }
            MirPlace place{0};
            hir::TypePtr place_type;
            if (!is_ptr_index &&
                build_place_for_incdec(*this, *unary.operand, place, place_type, ctx)) {
                hir::TypePtr target_type = unary.operand->type ? unary.operand->type : place_type;
                hir::TypePtr ptr_type = hir::make_pointer(target_type);
                LocalId result = ctx.new_temp(ptr_type);
                auto ref_rvalue = std::make_unique<MirRvalue>();
                ref_rvalue->kind = MirRvalue::Ref;
                ref_rvalue->data = MirRvalue::RefData{BorrowKind::Mutable, place};
                ctx.push_statement(MirStatement::assign(MirPlace{result}, std::move(ref_rvalue)));
                return result;
            }
        }

        // ポインタ型へのインデックスアクセスの場合（&ptr[i] → ptr + i）
        // これは this.data[idx] のようなケースで、dataがポインタ型の場合
        if (auto index = std::get_if<std::unique_ptr<hir::HirIndex>>(&unary.operand->kind)) {
            // オブジェクトの型を確認
            hir::TypePtr obj_type = (*index)->object ? (*index)->object->type : nullptr;

            if (obj_type && obj_type->kind == hir::TypeKind::Pointer) {
                // ポインタ値を取得
                LocalId ptr_val = lower_expression(*(*index)->object, ctx);
                // インデックス値を取得
                LocalId idx_val = lower_expression(*(*index)->index, ctx);

                // 結果の型（元のポインタ型と同じ）
                hir::TypePtr result_type = obj_type;
                LocalId result = ctx.new_temp(result_type);

                // ポインタ算術 (ptr + idx) を生成
                auto ptr_op = std::make_unique<MirOperand>();
                ptr_op->kind = MirOperand::Copy;
                ptr_op->data = MirPlace{ptr_val};

                auto idx_op = std::make_unique<MirOperand>();
                idx_op->kind = MirOperand::Copy;
                idx_op->data = MirPlace{idx_val};

                auto add_rvalue = std::make_unique<MirRvalue>();
                add_rvalue->kind = MirRvalue::BinaryOp;
                add_rvalue->data = MirRvalue::BinaryOpData{MirBinaryOp::Add, std::move(ptr_op),
                                                           std::move(idx_op), result_type};

                ctx.push_statement(MirStatement::assign(MirPlace{result}, std::move(add_rvalue)));
                return result;
            }

            // 配列型の場合は従来の処理
            // 配列を取得
            LocalId array;
            if (auto* var_ref =
                    std::get_if<std::unique_ptr<hir::HirVarRef>>(&(*index)->object->kind)) {
                auto var_id = ctx.resolve_variable((*var_ref)->name);
                if (var_id) {
                    array = *var_id;
                } else {
                    array = lower_expression(*(*index)->object, ctx);
                }
            } else {
                array = lower_expression(*(*index)->object, ctx);
            }

            // インデックスを取得
            LocalId idx = lower_expression(*(*index)->index, ctx);

            // ポインタ型を作成
            hir::TypePtr elem_type = hir::make_int();
            if ((*index)->object && (*index)->object->type &&
                (*index)->object->type->kind == hir::TypeKind::Array &&
                (*index)->object->type->element_type) {
                elem_type = (*index)->object->type->element_type;
            }
            hir::TypePtr ptr_type = hir::make_pointer(elem_type);
            LocalId result = ctx.new_temp(ptr_type);

            // プロジェクション付きPlaceへの参照
            MirPlace place{array};
            place.projections.push_back(PlaceProjection::index(idx));

            auto ref_rvalue = std::make_unique<MirRvalue>();
            ref_rvalue->kind = MirRvalue::Ref;
            ref_rvalue->data = MirRvalue::RefData{BorrowKind::Mutable, place};

            ctx.push_statement(MirStatement::assign(MirPlace{result}, std::move(ref_rvalue)));
            return result;
        }

        // メンバーアクセスの場合（&obj.field）
        if (auto member = std::get_if<std::unique_ptr<hir::HirMember>>(&unary.operand->kind)) {
            // オブジェクトを取得
            LocalId obj;
            if (auto* var_ref =
                    std::get_if<std::unique_ptr<hir::HirVarRef>>(&(*member)->object->kind)) {
                auto var_id = ctx.resolve_variable((*var_ref)->name);
                if (var_id) {
                    obj = *var_id;
                } else {
                    obj = lower_expression(*(*member)->object, ctx);
                }
            } else {
                obj = lower_expression(*(*member)->object, ctx);
            }

            // フィールドインデックスを取得
            hir::TypePtr obj_type = (*member)->object->type;
            if (!obj_type || obj_type->kind != hir::TypeKind::Struct) {
                // フォールバック
                LocalId operand = lower_expression(*unary.operand, ctx);
                hir::TypePtr ptr_type = hir::make_pointer(unary.operand->type);
                LocalId result = ctx.new_temp(ptr_type);
                auto ref_rvalue = std::make_unique<MirRvalue>();
                ref_rvalue->kind = MirRvalue::Ref;
                ref_rvalue->data = MirRvalue::RefData{BorrowKind::Mutable, MirPlace{operand}};
                ctx.push_statement(MirStatement::assign(MirPlace{result}, std::move(ref_rvalue)));
                return result;
            }

            auto field_idx = ctx.get_field_index(obj_type->name, (*member)->member);
            if (!field_idx) {
                // フォールバック
                LocalId operand = lower_expression(*unary.operand, ctx);
                hir::TypePtr ptr_type = hir::make_pointer(unary.operand->type);
                LocalId result = ctx.new_temp(ptr_type);
                auto ref_rvalue = std::make_unique<MirRvalue>();
                ref_rvalue->kind = MirRvalue::Ref;
                ref_rvalue->data = MirRvalue::RefData{BorrowKind::Mutable, MirPlace{operand}};
                ctx.push_statement(MirStatement::assign(MirPlace{result}, std::move(ref_rvalue)));
                return result;
            }

            // ポインタ型を作成
            hir::TypePtr ptr_type = hir::make_pointer(unary.operand->type);
            LocalId result = ctx.new_temp(ptr_type);

            // プロジェクション付きPlaceへの参照
            MirPlace place{obj};
            place.projections.push_back(PlaceProjection::field(*field_idx));

            auto ref_rvalue = std::make_unique<MirRvalue>();
            ref_rvalue->kind = MirRvalue::Ref;
            ref_rvalue->data = MirRvalue::RefData{BorrowKind::Mutable, place};

            ctx.push_statement(MirStatement::assign(MirPlace{result}, std::move(ref_rvalue)));
            return result;
        }

        // フォールバック：通常の評価
        LocalId operand = lower_expression(*unary.operand, ctx);
        hir::TypePtr ptr_type = hir::make_pointer(unary.operand->type);
        LocalId result = ctx.new_temp(ptr_type);

        auto ref_rvalue = std::make_unique<MirRvalue>();
        ref_rvalue->kind = MirRvalue::Ref;
        ref_rvalue->data = MirRvalue::RefData{BorrowKind::Mutable, MirPlace{operand}};

        ctx.push_statement(MirStatement::assign(MirPlace{result}, std::move(ref_rvalue)));
        return result;
    }

    // デリファレンス (*p)
    if (unary.op == hir::HirUnaryOp::Deref) {
        // ポインタをlowering
        LocalId ptr = lower_expression(*unary.operand, ctx);

        // 要素型を取得
        hir::TypePtr elem_type = hir::make_int();  // デフォルト
        if (unary.operand->type && unary.operand->type->kind == hir::TypeKind::Pointer &&
            unary.operand->type->element_type) {
            elem_type = unary.operand->type->element_type;
        } else if (ptr < ctx.func->locals.size() && ctx.func->locals[ptr].type &&
                   ctx.func->locals[ptr].type->kind == hir::TypeKind::Pointer &&
                   ctx.func->locals[ptr].type->element_type) {
            // HIRの型が無い場合（文字列補間式のパース経由等）はMIRローカルの型から導出。
            // int** の deref を int と誤推定するとLLVMでポインタが32bit幅に切り詰められ
            // クラッシュするため、要素型（ここでは int*）を正しく引き継ぐ
            elem_type = ctx.func->locals[ptr].type->element_type;
        }

        LocalId result = ctx.new_temp(elem_type);

        // Derefプロジェクションを使用
        MirPlace place{ptr};
        place.projections.push_back(PlaceProjection::deref());

        ctx.push_statement(
            MirStatement::assign(MirPlace{result}, MirRvalue::use(MirOperand::copy(place))));
        return result;
    }

    // オペランドをlowering
    LocalId operand = lower_expression(*unary.operand, ctx);

    // MIRの単項演算子に変換
    MirUnaryOp mir_op;
    switch (unary.op) {
        case hir::HirUnaryOp::Neg:
            mir_op = MirUnaryOp::Neg;
            break;
        case hir::HirUnaryOp::Not:
            mir_op = MirUnaryOp::Not;
            break;
        case hir::HirUnaryOp::BitNot:
            mir_op = MirUnaryOp::BitNot;
            break;
        default:
            mir_op = MirUnaryOp::Neg;  // フォールバック
    }

    // 結果用の一時変数（NOT の場合は bool、NEG の場合は元の型）
    hir::TypePtr operand_type = unary.operand->type;
    // HIRの型が利用できない、またはエラー型の場合、ローカル変数から型を取得
    if ((!operand_type || operand_type->is_error()) && operand < ctx.func->locals.size()) {
        operand_type = ctx.func->locals[operand].type;
    }
    if (!operand_type || operand_type->is_error()) {
        operand_type = hir::make_int();  // デフォルト
    }

    hir::TypePtr result_type = (unary.op == hir::HirUnaryOp::Not) ? hir::make_bool() : operand_type;
    LocalId result = ctx.new_temp(result_type);
    // UnaryOp Rvalueを作成
    auto unary_rvalue = std::make_unique<MirRvalue>();
    unary_rvalue->kind = MirRvalue::UnaryOp;
    unary_rvalue->data = MirRvalue::UnaryOpData{mir_op, MirOperand::copy(MirPlace{operand})};

    ctx.push_statement(MirStatement::assign(MirPlace{result}, std::move(unary_rvalue)));

    return result;
}

}  // namespace cm::mir
