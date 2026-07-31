// MIR lowering - アクセス式（メンバアクセス・Place取得・インデックス）

#include "internal/base/debug.hpp"
#include "internal/mir/lowering/expr.hpp"
#include "internal/mir/lowering/slice_dispatch.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cm::mir {

// 二項演算のlowering
LocalId ExprLowering::lower_member(const hir::HirMember& member, LoweringContext& ctx) {
    // ネストしたメンバーアクセスを検出して、プロジェクションを連結する
    std::vector<std::pair<std::string, std::string>> field_chain;  // (struct_name, field_name)
    const hir::HirExpr* current = member.object.get();

    // 最初のメンバー（自身）を追加
    auto initial_type = member.object->type;
    field_chain.push_back({initial_type ? initial_type->name : "", member.member});

    // フィールドチェーンを構築（最も内側から外側へ）
    while (auto* inner_member = std::get_if<std::unique_ptr<hir::HirMember>>(&current->kind)) {
        auto obj_type = (*inner_member)->object->type;
        field_chain.push_back({obj_type ? obj_type->name : "", (*inner_member)->member});
        current = (*inner_member)->object.get();
    }

    // ベースオブジェクトを取得
    // 変数参照の場合は直接その変数のLocalIdを使用してコピーを避ける
    // lower_expressionだと一時変数にコピーされてしまい、プロジェクションが失われる
    LocalId object;
    if (auto* var_ref = std::get_if<std::unique_ptr<hir::HirVarRef>>(&current->kind)) {
        auto var_id = ctx.resolve_variable((*var_ref)->name);
        if (var_id) {
            object = *var_id;
        } else {
            // 変数が見つからない場合はlower_expressionにフォールバック
            object = lower_expression(*current, ctx);
        }
    } else {
        object = lower_expression(*current, ctx);
    }
    hir::TypePtr obj_type = current->type;

    // MIRのローカル変数の型を取得（selfの場合はポインタ型）
    hir::TypePtr mir_type = nullptr;
    if (object < ctx.func->locals.size()) {
        mir_type = ctx.func->locals[object].type;
    }

    // ポインタ型の場合、デリファレンスが必要
    // HIRの型ではなくMIRの型でポインタかどうかを判定（selfは暗黙的にポインタ）
    bool needs_deref = false;
    if (mir_type && mir_type->kind == hir::TypeKind::Pointer) {
        needs_deref = true;
        obj_type = mir_type->element_type;  // ポインタの先の型を使用
    } else if (!obj_type || obj_type->kind != hir::TypeKind::Struct) {
        // HIRの型が未設定または構造体でない場合、MIRの型から推論
        if (mir_type) {
            obj_type = mir_type;
        }
    }

    // 単純enum（ペイロードなし、int表現）の __tag は値そのもの。
    // Tagged Union化されないenum変数への c.__tag は恒等アクセスとして扱う（enum比較のHIR書き換えが一律に __tag 抽出を挿入するため）
    if (member.member == "__tag" && (!obj_type || obj_type->kind != hir::TypeKind::Struct)) {
        if (!needs_deref) {
            return object;
        }
        // ポインタ経由（(*p).__tag）はデリファレンスした値を返す
        LocalId result = ctx.new_temp(obj_type ? obj_type : hir::make_int());
        MirPlace src{object};
        src.projections.push_back(PlaceProjection::deref());
        ctx.push_statement(
            MirStatement::assign(MirPlace{result}, MirRvalue::use(MirOperand::copy(src))));
        return result;
    }

    if (!obj_type || obj_type->kind != hir::TypeKind::Struct) {
        debug_msg("MIR",
                  "Error: Member access on non-struct type for member '" + member.member + "'");
        return ctx.new_temp(hir::make_error());
    }

    // フィールドチェーンを逆順にしてプロジェクションを構築
    MirPlace place{object};

    // ポインタの場合、最初にデリファレンス
    if (needs_deref) {
        place.projections.push_back(PlaceProjection::deref());
    }

    hir::TypePtr current_type = obj_type;

    for (auto it = field_chain.rbegin(); it != field_chain.rend(); ++it) {
        const std::string& field_name = it->second;

        // チェーン途中の単純enumメンバへの __tag も恒等（プロジェクション追加なし）
        if (field_name == "__tag" && current_type && current_type->kind != hir::TypeKind::Struct) {
            continue;
        }

        if (!current_type || current_type->kind != hir::TypeKind::Struct) {
            debug_msg("MIR", "Error: Non-struct type in member chain");
            return ctx.new_temp(hir::make_error());
        }

        // ジェネリック構造体の場合、ベース名で検索（例: Node<Item> -> Node, Pair__int -> Pair）
        std::string base_name = current_type->name;

        // マングリング済み名前（__を含む）の場合、ベース名を抽出
        size_t mangled_pos = base_name.find("__");
        std::string original_base = base_name;
        if (mangled_pos != std::string::npos) {
            base_name = base_name.substr(0, mangled_pos);
        }

        // Tagged Union構造体の特別処理
        // __TaggedUnion_*の場合、または enum_defs に登録されている enum型の場合
        // __tagはfield[0]、__payloadはfield[1]
        std::optional<size_t> field_idx = std::nullopt;
        bool is_tagged_union = (current_type->name.find("__TaggedUnion_") == 0);

        // enum_defs に登録されている場合もTagged Unionとして扱う
        if (!is_tagged_union && ctx.enum_defs && ctx.enum_defs->count(current_type->name)) {
            is_tagged_union = true;
        }

        if (is_tagged_union) {
            if (field_name == "__tag") {
                field_idx = 0;
            } else if (field_name == "__payload") {
                field_idx = 1;
            }
        }

        if (!field_idx) {
            field_idx = ctx.get_field_index(base_name, field_name);
        }
        if (!field_idx) {
            // マングリング前の名前でも見つからない場合、元の名前で再試行
            field_idx = ctx.get_field_index(original_base, field_name);
            if (!field_idx) {
                debug_msg("MIR", "Error: Field '" + field_name + "' not found in struct '" +
                                     base_name + "'");
                return ctx.new_temp(hir::make_error());
            }
        }

        place.projections.push_back(PlaceProjection::field(*field_idx));

        // 次のフィールドの型を取得
        // ジェネリック構造体の場合type_argsを保持し、フィールド型置換に使用
        if (ctx.struct_defs && ctx.struct_defs->count(base_name)) {
            const auto* struct_def = ctx.struct_defs->at(base_name);
            if (*field_idx < struct_def->fields.size()) {
                hir::TypePtr field_type = struct_def->fields[*field_idx].type;

                // フィールド型がジェネリックパラメータの場合、type_argsから置換
                // 注: HIRでTがStruct扱いになる場合があるため、generic_params名で照合
                if (field_type && !current_type->type_args.empty()) {
                    for (size_t j = 0; j < struct_def->generic_params.size() &&
                                       j < current_type->type_args.size();
                         ++j) {
                        // field_typeの名前がgeneric_paramsに一致する場合、置換
                        if (struct_def->generic_params[j].name == field_type->name) {
                            field_type = current_type->type_args[j];
                            break;
                        }
                    }
                }
                // type_argsが空でもマングリング済み名前（Pair__int等）から型引数を抽出
                else if (field_type && current_type->type_args.empty() &&
                         current_type->name.find("__") != std::string::npos) {
                    // マングリング名から型引数を抽出
                    std::vector<std::string> extracted_args;
                    std::string name = current_type->name;
                    size_t pos = name.find("__");
                    while (pos != std::string::npos) {
                        size_t next = name.find("__", pos + 2);
                        if (next == std::string::npos) {
                            extracted_args.push_back(name.substr(pos + 2));
                        } else {
                            extracted_args.push_back(name.substr(pos + 2, next - pos - 2));
                        }
                        pos = next;
                    }
                    // generic_paramsと照合して置換
                    for (size_t j = 0;
                         j < struct_def->generic_params.size() && j < extracted_args.size(); ++j) {
                        if (struct_def->generic_params[j].name == field_type->name) {
                            // 抽出した型名から具体型を作成
                            const std::string& type_name = extracted_args[j];
                            hir::TypePtr concrete_type;
                            if (type_name == "int") {
                                concrete_type = hir::make_int();
                            } else if (type_name == "uint") {
                                concrete_type = hir::make_uint();
                            } else if (type_name == "long") {
                                concrete_type = hir::make_long();
                            } else if (type_name == "ulong") {
                                concrete_type = hir::make_ulong();
                            } else if (type_name == "double") {
                                concrete_type = hir::make_double();
                            } else if (type_name == "float") {
                                concrete_type = hir::make_float();
                            } else if (type_name == "bool") {
                                concrete_type = hir::make_bool();
                            } else if (type_name == "string") {
                                concrete_type = hir::make_string();
                            } else {
                                // その他は構造体として扱う
                                concrete_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
                                concrete_type->name = type_name;
                            }
                            field_type = concrete_type;
                            break;
                        }
                    }
                }
                current_type = field_type;
            } else {
                current_type = hir::make_int();
            }
        } else {
            current_type = hir::make_int();
        }
    }

    // 最終的なフィールドの型で一時変数を作成
    // 重要: shared_ptrの参照共有で後から型が変更される問題を回避するため、型をディープコピーする
    hir::TypePtr final_type = current_type;
    if (current_type &&
        (current_type->kind == hir::TypeKind::Int || current_type->kind == hir::TypeKind::UInt ||
         current_type->kind == hir::TypeKind::Long || current_type->kind == hir::TypeKind::Float ||
         current_type->kind == hir::TypeKind::Double ||
         current_type->kind == hir::TypeKind::Bool)) {
        // プリミティブ型は新しいインスタンスを作成
        final_type = std::make_shared<hir::Type>(*current_type);
    } else if (current_type && current_type->kind == hir::TypeKind::Struct) {
        // 構造体型も新しいインスタンスを作成
        final_type = std::make_shared<hir::Type>(*current_type);
    }

    LocalId result = ctx.new_temp(final_type);

    ctx.push_statement(
        MirStatement::assign(MirPlace{result}, MirRvalue::use(MirOperand::copy(place))));

    return result;
}

namespace {
// 固定長配列かどうか。スライスはdimensionsに0が入るケースがあるためarray_sizeと次元値の両方で判別する
bool is_fixed_array_type(const cm::hir::TypePtr& t) {
    return t && t->kind == cm::hir::TypeKind::Array &&
           (t->array_size.has_value() || (!t->dimensions.empty() && t->dimensions[0] > 0));
}
}  // namespace

// 唯一の場所化API（type-resolution-simplification 領域2）。
// 従来はresolve_receiver_place（レシーバ）・get_member_place（メンバ）・assign.cppのbuild_projections（代入左辺値）が同じチェーン場所化を並行実装しており、スライス降下の修正が経路間で伝播しなかった（N1修正の読み経路に対するW2の書き経路再発）。
bool ExprLowering::lower_place(const hir::HirExpr* expr, LoweringContext& ctx, MirPlace& out_place,
                               hir::TypePtr& out_type) {
    if (!expr)
        return false;

    if (auto* var = std::get_if<std::unique_ptr<hir::HirVarRef>>(&expr->kind)) {
        auto local_opt = ctx.resolve_variable((*var)->name);
        if (!local_opt)
            return false;
        out_place = MirPlace{*local_opt};
        out_type =
            (*local_opt < ctx.func->locals.size()) ? ctx.func->locals[*local_opt].type : nullptr;
        return true;
    }

    if (auto* mem = std::get_if<std::unique_ptr<hir::HirMember>>(&expr->kind)) {
        return get_member_place(**mem, ctx, out_place, out_type);
    }

    if (auto* idx = std::get_if<std::unique_ptr<hir::HirIndex>>(&expr->kind)) {
        MirPlace base{0};
        hir::TypePtr base_type = nullptr;
        if (!lower_place((*idx)->object.get(), ctx, base, base_type))
            return false;
        hir::TypePtr cur = base_type ? ctx.resolve_typedef(base_type) : nullptr;

        // 添字式を収集（単一index or 多次元indices）
        std::vector<const hir::HirExprPtr*> index_exprs;
        if (!(*idx)->indices.empty()) {
            for (const auto& ie : (*idx)->indices) {
                index_exprs.push_back(&ie);
            }
        } else {
            index_exprs.push_back(&(*idx)->index);
        }

        for (const auto* iep : index_exprs) {
            if (!iep || !*iep || !cur)
                return false;
            const bool is_array = cur->kind == hir::TypeKind::Array;
            const bool is_pointer = cur->kind == hir::TypeKind::Pointer;
            if (!is_array && !is_pointer)
                return false;
            auto elem = cur->element_type ? ctx.resolve_typedef(cur->element_type) : nullptr;
            const bool is_slice = is_array && !is_fixed_array_type(cur);
            // スライス要素が内側スライスの場合、ヘッダは外側dataバッファへインライン格納されているため生index投影ではなく参照版subsliceで降下する（W2/H10第3段）。これで読み・書き・レシーバ変異のすべてが格納中の実体へ届く
            const bool elem_is_inline_slice = is_slice && elem &&
                                              elem->kind == hir::TypeKind::Array &&
                                              !elem->array_size.has_value();
            LocalId iv = lower_expression(**iep, ctx);
            if (elem_is_inline_slice) {
                // ベースに投影が残っている場合はヘッダポインタを一時へ取り出してから降下する
                LocalId slice_local;
                if (base.projections.empty()) {
                    slice_local = base.local;
                } else {
                    slice_local = ctx.new_temp(cur);
                    ctx.push_statement(MirStatement::assign(
                        MirPlace{slice_local}, MirRvalue::use(MirOperand::copy(base))));
                }
                LocalId inner = ctx.new_temp(elem);
                BlockId success_block = ctx.new_block();
                std::vector<MirOperandPtr> args;
                args.push_back(MirOperand::copy(MirPlace{slice_local}));
                args.push_back(MirOperand::copy(MirPlace{iv}));
                auto call_term = std::make_unique<MirTerminator>();
                call_term->kind = MirTerminator::Call;
                call_term->data =
                    MirTerminator::CallData{MirOperand::function_ref("cm_slice_get_subslice_ref"),
                                            std::move(args),
                                            MirPlace{inner},
                                            success_block,
                                            std::nullopt,
                                            "",
                                            "",
                                            false};
                ctx.set_terminator(std::move(call_term));
                ctx.switch_to_block(success_block);
                base = MirPlace{inner};
            } else {
                // 固定長配列・ポインタ・スライス（非インラインスライス要素）はIndex投影で場所化する。スライスへのIndex投影はcodegenがCmSliceヘッダ経由で要素アドレスを計算する
                base.projections.push_back(PlaceProjection::index(iv));
            }
            cur = elem;
        }
        out_place = base;
        out_type = cur;
        return true;
    }

    if (auto* unary = std::get_if<std::unique_ptr<hir::HirUnary>>(&expr->kind)) {
        // デリファレンス: *ptr
        if ((*unary)->op == hir::HirUnaryOp::Deref) {
            MirPlace base{0};
            hir::TypePtr base_type = nullptr;
            if (!lower_place((*unary)->operand.get(), ctx, base, base_type))
                return false;
            base.projections.push_back(PlaceProjection::deref());
            auto ptr_t = base_type ? ctx.resolve_typedef(base_type) : nullptr;
            out_place = base;
            out_type =
                (ptr_t && ptr_t->kind == hir::TypeKind::Pointer) ? ptr_t->element_type : nullptr;
            return true;
        }
        return false;
    }

    return false;
}

// メソッドレシーバの場所化（H10）。実体はlower_placeへの委譲
bool ExprLowering::resolve_receiver_place(const hir::HirExpr* expr, LoweringContext& ctx,
                                          MirPlace& out_place, hir::TypePtr& out_type) {
    return lower_place(expr, ctx, out_place, out_type);
}

// メンバアクセスの場所化（lower_placeのHirMember枝）
bool ExprLowering::get_member_place(const hir::HirMember& member, LoweringContext& ctx,
                                    MirPlace& out_place, hir::TypePtr& out_type) {
    MirPlace base{0};
    hir::TypePtr base_type = nullptr;
    if (!lower_place(member.object.get(), ctx, base, base_type))
        return false;

    auto obj_type = base_type ? ctx.resolve_typedef(base_type) : nullptr;
    // ポインタ経由のメンバアクセスはDerefを挟んでpointeeを基点にする（implメソッドのself=*Structを含む）
    while (obj_type && obj_type->kind == hir::TypeKind::Pointer && obj_type->element_type) {
        base.projections.push_back(PlaceProjection::deref());
        obj_type = ctx.resolve_typedef(obj_type->element_type);
    }
    if (!obj_type || obj_type->kind != hir::TypeKind::Struct)
        return false;

    auto field_idx = ctx.get_field_index(obj_type->name, member.member);
    if (!field_idx)
        return false;
    base.projections.push_back(PlaceProjection::field(*field_idx));

    // フィールド型を取得（ジェネリック構造体はtype_argsでパラメータ名を置換する）
    hir::TypePtr field_type = nullptr;
    if (ctx.struct_defs && ctx.struct_defs->count(obj_type->name)) {
        const auto* struct_def = ctx.struct_defs->at(obj_type->name);
        if (*field_idx < struct_def->fields.size()) {
            field_type = struct_def->fields[*field_idx].type;
            if (field_type && !obj_type->type_args.empty()) {
                for (size_t i = 0;
                     i < struct_def->generic_params.size() && i < obj_type->type_args.size(); ++i) {
                    if (struct_def->generic_params[i].name == field_type->name) {
                        field_type = obj_type->type_args[i];
                        break;
                    }
                }
            }
        }
    }
    out_place = base;
    out_type = field_type ? field_type : hir::make_int();
    return true;
}

// 配列インデックスのlowering
LocalId ExprLowering::lower_index(const hir::HirIndex& index_expr, LoweringContext& ctx) {
    // オブジェクトをlowering
    LocalId array = 0;
    bool array_is_set = false;

    // インデックス対象の配列を指す「場所（MirPlace）」。可能な限り配列全体のコピーを避ける。
    // struct.arrayField[i] のようなアクセスで配列フィールド全体をtempへコピーすると、
    // 固定長配列 [N x T] の巨大なload/storeが生成され、SROA→InstCombineが超線形に膨張する。
    MirPlace base_place{0};
    bool have_base_place = false;

    // objectが変数参照の場合は直接その変数を使用（配列のコピーを防ぐ）
    if (auto* var_ref = std::get_if<std::unique_ptr<hir::HirVarRef>>(&index_expr.object->kind)) {
        auto var_id = ctx.resolve_variable((*var_ref)->name);
        if (var_id) {
            array = *var_id;
            array_is_set = true;
        } else {
            array = lower_expression(*index_expr.object, ctx);
            array_is_set = true;
        }
    } else if (auto* mem = std::get_if<std::unique_ptr<hir::HirMember>>(&index_expr.object->kind)) {
        // struct.arrayField[i]: メンバアクセスの場所を直接取得して配列全体のコピーを避ける
        MirPlace mp{0};
        hir::TypePtr mt;
        if (get_member_place(**mem, ctx, mp, mt)) {
            base_place = mp;
            have_base_place = true;
        } else {
            array = lower_expression(*index_expr.object, ctx);
            array_is_set = true;
        }
    } else {
        array = lower_expression(*index_expr.object, ctx);
        array_is_set = true;
    }

    // 多次元配列最適化: indices が設定されている場合、複数のIndex projectionを生成
    // これにより一時変数（行コピー）を回避し、LLVMのベクトル化が可能になる
    bool is_multi_dim = !index_expr.indices.empty();

    // インデックスをlowering
    std::vector<LocalId> index_locals;
    if (is_multi_dim) {
        // 多次元: 全インデックスを収集
        for (const auto& idx_expr : index_expr.indices) {
            index_locals.push_back(lower_expression(*idx_expr, ctx));
        }
    } else {
        // 単一インデックス（後方互換性）
        index_locals.push_back(lower_expression(*index_expr.index, ctx));
    }

    // 要素型を取得（最内側の要素型まで辿る）
    hir::TypePtr elem_type = hir::make_int();  // デフォルト
    bool is_slice = false;
    hir::TypePtr current_type = nullptr;

    if (index_expr.object && index_expr.object->type) {
        current_type = index_expr.object->type;
        // 多次元配列またはポインタの場合、インデックス数の深さまで要素型を辿る
        for (size_t i = 0; i < index_locals.size() && current_type; ++i) {
            if (current_type->kind == hir::TypeKind::Array) {
                is_slice = !current_type->array_size.has_value();
                if (current_type->element_type) {
                    current_type = current_type->element_type;
                } else {
                    break;
                }
            } else if (current_type->kind == hir::TypeKind::Pointer) {
                // ポインタ型の場合も要素型を取得（ptr[i]アクセス）
                if (current_type->element_type) {
                    current_type = current_type->element_type;
                } else {
                    break;
                }
            } else if (current_type->kind == hir::TypeKind::String) {
                current_type = hir::make_char();
                break;
            } else {
                break;
            }
        }
        elem_type = current_type ? current_type : hir::make_int();
    }

    // フォールバック: HIR型情報がnull、またはelement_typeがジェネリック型の場合、MIRローカル変数の型から判定
    // ジェネリック関数内での ptr[i] アクセスでは、HIRの型がまだ T* のままなので
    // モノモーフ化後のMIRローカル変数の型を使用する必要がある
    bool needs_fallback =
        !is_slice &&
        (!elem_type || elem_type->kind == hir::TypeKind::Generic ||
         elem_type->name == "T" ||  // 一般的なジェネリック型パラメータ
         (elem_type->name.length() == 1 && std::isupper(elem_type->name[0]))  // 単一大文字
        );

    if (needs_fallback && array_is_set && array < ctx.func->locals.size()) {
        hir::TypePtr array_type = ctx.func->locals[array].type;
        if (array_type && (array_type->kind == hir::TypeKind::Array ||
                           array_type->kind == hir::TypeKind::Pointer)) {
            current_type = array_type;
            for (size_t i = 0; i < index_locals.size() && current_type; ++i) {
                if (current_type->kind == hir::TypeKind::Array) {
                    is_slice = !current_type->array_size.has_value();
                    if (current_type->element_type) {
                        current_type = current_type->element_type;
                    } else {
                        break;
                    }
                } else if (current_type->kind == hir::TypeKind::Pointer) {
                    // ポインタ型の場合も要素型を取得
                    if (current_type->element_type) {
                        current_type = current_type->element_type;
                    } else {
                        break;
                    }
                } else {
                    break;
                }
            }
            // MIRローカル変数から具象型が取得できた場合のみ更新
            if (current_type && current_type->kind != hir::TypeKind::Generic) {
                elem_type = current_type;
            }
        }
    }

    // メンバ場所ベースの最適化は固定長配列のみで行う。スライス（fat pointer）はランタイム表現が
    // 異なり要素アドレス計算がデリファレンスを要するため、既存の値materialize経路へ戻す。
    if (have_base_place && is_slice) {
        array = lower_expression(*index_expr.object, ctx);
        array_is_set = true;
        have_base_place = false;
    }

    // スライスの場合、HIR型はtypedefエイリアス未解決のことがあるため、解決済みのMIRローカル型がユニオンならそちらを優先する
    if (is_slice && array_is_set && array < ctx.func->locals.size()) {
        hir::TypePtr array_type = ctx.func->locals[array].type;
        if (array_type && array_type->kind == hir::TypeKind::Array && array_type->element_type &&
            array_type->element_type->kind == hir::TypeKind::Union) {
            elem_type = array_type->element_type;
        }
    }

    // 多次元スライスの多重添字読み（rows[0][1]等）: 中間レベルをcm_slice_get_subsliceで辿り、
    // 単一添字の読みへ還元する（従来は固定長配列のprojection経路へ落ちて壊れた値を読んでいた。H10）
    if (is_slice && index_locals.size() > 1 && array_is_set) {
        hir::TypePtr walk = nullptr;
        if (array < ctx.func->locals.size() && ctx.func->locals[array].type) {
            walk = ctx.resolve_typedef(ctx.func->locals[array].type);
        }
        if (!walk && index_expr.object && index_expr.object->type) {
            walk = ctx.resolve_typedef(index_expr.object->type);
        }
        std::vector<hir::TypePtr> level_types;
        bool all_slice = true;
        for (size_t i = 0; i < index_locals.size(); ++i) {
            if (!walk || walk->kind != hir::TypeKind::Array || walk->array_size.has_value()) {
                all_slice = false;
                break;
            }
            walk = walk->element_type ? ctx.resolve_typedef(walk->element_type) : nullptr;
            level_types.push_back(walk);
        }
        if (all_slice) {
            LocalId cur = array;
            for (size_t i = 0; i + 1 < index_locals.size(); ++i) {
                LocalId nxt = ctx.new_temp(level_types[i] ? level_types[i] : hir::make_int());
                BlockId sb = ctx.new_block();
                std::vector<MirOperandPtr> sargs;
                sargs.push_back(MirOperand::copy(MirPlace{cur}));
                sargs.push_back(MirOperand::copy(MirPlace{index_locals[i]}));
                auto ct = std::make_unique<MirTerminator>();
                ct->kind = MirTerminator::Call;
                ct->data =
                    MirTerminator::CallData{MirOperand::function_ref("cm_slice_get_subslice"),
                                            std::move(sargs),
                                            MirPlace{nxt},
                                            sb,
                                            std::nullopt,
                                            "",
                                            "",
                                            false};
                ctx.set_terminator(std::move(ct));
                ctx.switch_to_block(sb);
                cur = nxt;
            }
            array = cur;
            const LocalId last_index = index_locals.back();
            index_locals.clear();
            index_locals.push_back(last_index);
            if (!level_types.empty() && level_types.back()) {
                elem_type = level_types.back();
            }
        }
    }

    LocalId result = ctx.new_temp(elem_type);

    // スライスの場合は関数呼び出しを生成（多次元は非対応）
    if (is_slice && index_locals.size() == 1) {
        // 要素型が配列の場合（多次元スライス）はサブスライスを取得
        bool is_multidim = elem_type && elem_type->kind == hir::TypeKind::Array;

        std::string get_func = "cm_slice_get_i32";
        if (is_multidim) {
            get_func = "cm_slice_get_subslice";
        } else if (elem_type) {
            auto elem_kind = elem_type->kind;
            if (auto info = slice_scalar_info(elem_kind)) {
                // スカラ型: 幅サフィックスをslice_dispatchから取得（elem_sizeと整合。C4）
                get_func = std::string("cm_slice_get_") + info->width;
            } else if (elem_kind == hir::TypeKind::Pointer || elem_kind == hir::TypeKind::String) {
                get_func = "cm_slice_get_ptr";
            } else if (elem_kind == hir::TypeKind::Union || elem_kind == hir::TypeKind::Struct) {
                // ユニオン・構造体要素: blob格納のため要素先頭へのポインタを取得する
                get_func = "cm_slice_get_element_ptr";
            }
        }

        BlockId success_block = ctx.new_block();
        std::vector<MirOperandPtr> args;
        args.push_back(MirOperand::copy(MirPlace{array}));
        args.push_back(MirOperand::copy(MirPlace{index_locals[0]}));

        // ユニオン型要素は要素ポインタを受けてからデリファレンスでロードする
        bool deref_result = (get_func == "cm_slice_get_element_ptr");
        LocalId call_dest = result;
        if (deref_result) {
            call_dest = ctx.new_temp(hir::make_pointer(elem_type));
        }

        auto call_term = std::make_unique<MirTerminator>();
        call_term->kind = MirTerminator::Call;
        call_term->data = MirTerminator::CallData{MirOperand::function_ref(get_func),
                                                  std::move(args),
                                                  MirPlace{call_dest},
                                                  success_block,
                                                  std::nullopt,
                                                  "",
                                                  "",
                                                  false};
        ctx.set_terminator(std::move(call_term));
        ctx.switch_to_block(success_block);

        if (deref_result) {
            ctx.push_statement(MirStatement::assign(
                MirPlace{result},
                MirRvalue::use(MirOperand::copy(MirPlace{call_dest, {PlaceProjection::deref()}}))));
        }

        return result;
    }

    // 通常の配列インデックス（単一または多次元）
    // 多次元配列最適化: 連続するIndex projectionを生成
    // a[i][j][k] → place.projections = [Index(i), Index(j), Index(k)]
    // メンバアクセス経由の配列（arena.nodes[i] 等）は取得済みの場所を土台にして、
    // 配列全体のコピーを挟まず要素だけをコピーする
    MirPlace place = have_base_place ? base_place : MirPlace{array};

    // ポインタ型の「変数」に対するインデックスアクセスの場合、Index前にDerefが必要
    // p[0] → place.projections = [Deref, Index(0)]
    // ただし self.data[idx] のようなメンバーアクセス経由は、lower_expressionで既にポインタ値がtemp変数にロードされているため、Derefは不要
    bool is_var_ref = index_expr.object && std::holds_alternative<std::unique_ptr<hir::HirVarRef>>(
                                               index_expr.object->kind);
    if (is_var_ref && index_expr.object && index_expr.object->type &&
        index_expr.object->type->kind == hir::TypeKind::Pointer) {
        place.projections.push_back(PlaceProjection::deref());
    } else if (is_var_ref && !is_slice && array < ctx.func->locals.size()) {
        // MIRローカル変数の型からもポインタ型を検出（VarRefの場合のみ）
        auto& array_local = ctx.func->locals[array];
        if (array_local.type && array_local.type->kind == hir::TypeKind::Pointer) {
            place.projections.push_back(PlaceProjection::deref());
        }
    }

    for (LocalId idx_local : index_locals) {
        // ポインタ経由のインデックスアクセス時にresult_type（elem_type）を設定
        // これによりモノモーフ化でsubstitute_place_typesがジェネリック型を具象型に置換可能
        place.projections.push_back(PlaceProjection::index(idx_local, elem_type));
    }

    ctx.push_statement(
        MirStatement::assign(MirPlace{result}, MirRvalue::use(MirOperand::copy(place))));

    return result;
}

}  // namespace cm::mir
