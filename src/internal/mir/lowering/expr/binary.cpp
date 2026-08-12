// MIR lowering - 二項演算式

#include "internal/base/debug.hpp"
#include "internal/mir/lowering/expr.hpp"
#include "internal/syntax/ast/typekey.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cm::mir {

namespace {

// 文字列連結（どちらかのオペランドがstring型のAdd）かを判定する（H9第5段）
bool cm_is_string_add(const hir::HirExpr& e) {
    auto* b = std::get_if<std::unique_ptr<hir::HirBinary>>(&e.kind);
    if (!b || !*b || (*b)->op != hir::HirBinaryOp::Add) {
        return false;
    }
    const auto& lhs = (*b)->lhs;
    const auto& rhs = (*b)->rhs;
    const bool l = lhs && lhs->type && lhs->type->kind == hir::TypeKind::String;
    const bool r = rhs && rhs->type && rhs->type->kind == hir::TypeKind::String;
    return l || r;
}

// 連結チェーンを左から右の平坦列へ展開する
void cm_flatten_string_concat(const hir::HirExpr& e, std::vector<const hir::HirExpr*>& out) {
    if (cm_is_string_add(e)) {
        auto* b = std::get_if<std::unique_ptr<hir::HirBinary>>(&e.kind);
        cm_flatten_string_concat(*(*b)->lhs, out);
        cm_flatten_string_concat(*(*b)->rhs, out);
        return;
    }
    out.push_back(&e);
}

}  // namespace

// ユニオンの等値比較を「タグ一致＋アクティブ変種のペイロード比較」のCFGへ脱糖する。
// 従来は生表現のMIR Eqへ落ち、native/jitはタグのみ比較（1==2がtrue）、jsはオブジェクト参照比較（1==1がfalse）と全バックエンドで誤値だった。
// 既存のis（Cast check_only=タグ比較）・as（Cast=ペイロード抽出）・型付きEq（string=内容比較等はバックエンドがオペランド型で解決）のみで構築するため、バックエンド個別対応は不要。
// 対象: union==union（同一union型）と union==変種値（片側が変種型に一致）。Ne は結果の反転。
// null変種は is null 同士のタグ一致のみで等値とする
LocalId cm_lower_union_equality(bool is_ne, LocalId lhs, LocalId rhs, const hir::TypePtr& lt,
                                const hir::TypePtr& rt, bool l_union, bool r_union,
                                LoweringContext& ctx) {
    LocalId result = ctx.new_temp(hir::make_bool());
    MirConstant false_const;
    false_const.value = false;
    false_const.type = hir::make_bool();
    ctx.push_statement(
        MirStatement::assign(MirPlace{result}, MirRvalue::use(MirOperand::constant(false_const))));
    BlockId end_block = ctx.new_block();

    auto emit_is = [&](LocalId v, const hir::TypePtr& vt) -> LocalId {
        LocalId t = ctx.new_temp(hir::make_bool());
        ctx.push_statement(MirStatement::assign(
            MirPlace{t}, MirRvalue::cast(MirOperand::copy(MirPlace{v}), vt, /*check_only=*/true)));
        return t;
    };
    auto emit_as = [&](LocalId v, const hir::TypePtr& vt) -> LocalId {
        LocalId t = ctx.new_temp(vt);
        ctx.push_statement(
            MirStatement::assign(MirPlace{t}, MirRvalue::cast(MirOperand::copy(MirPlace{v}), vt)));
        return t;
    };
    // p1 == p2 を result へ格納して end へ合流する（比較の型はvt。string等の内容比較はバックエンドがオペランド型で解決する）。
    // 動的スライス変種はHIRの配列比較と同じ正準ランタイム cm_slice_equal で内容比較する（生のMIR Eqはnativeでクラッシュする）
    auto finish_with_eq = [&](LocalId p1, const hir::TypePtr& t1, LocalId p2,
                              const hir::TypePtr& t2) {
        LocalId eq = ctx.new_temp(hir::make_bool());
        const bool is_dyn_slice =
            t1 && t1->kind == hir::TypeKind::Array && !t1->array_size.has_value();
        if (is_dyn_slice) {
            BlockId success = ctx.new_block();
            std::vector<MirOperandPtr> args;
            args.push_back(MirOperand::copy(MirPlace{p1}, t1));
            args.push_back(MirOperand::copy(MirPlace{p2}, t2));
            auto call_term = std::make_unique<MirTerminator>();
            call_term->kind = MirTerminator::Call;
            call_term->data = MirTerminator::CallData{MirOperand::function_ref("cm_slice_equal"),
                                                      std::move(args),
                                                      MirPlace{eq},
                                                      success,
                                                      std::nullopt,
                                                      "",
                                                      "",
                                                      false};
            ctx.set_terminator(std::move(call_term));
            ctx.switch_to_block(success);
        } else {
            ctx.push_statement(MirStatement::assign(
                MirPlace{eq},
                MirRvalue::binary(MirBinaryOp::Eq, MirOperand::copy(MirPlace{p1}, t1),
                                  MirOperand::copy(MirPlace{p2}, t2), hir::make_bool())));
        }
        ctx.push_statement(
            MirStatement::assign(MirPlace{result}, MirRvalue::use(MirOperand::copy(MirPlace{eq}))));
        ctx.set_terminator(MirTerminator::goto_block(end_block));
    };

    if (l_union && r_union) {
        // 変種はtype_args形式（$Uデコード産）とUnionType::variants形式の両対応（union_variant_types）。
        // UnionTypeへのstatic_castはtype_args形式の素のast::Typeでvariants読みが不正アクセスになる
        for (const auto& vt : ast::union_variant_types(lt)) {
            if (!vt) {
                continue;
            }
            BlockId chk_rhs = ctx.new_block();
            BlockId next_variant = ctx.new_block();
            LocalId t1 = emit_is(lhs, vt);
            ctx.set_terminator(MirTerminator::switch_int(MirOperand::copy(MirPlace{t1}),
                                                         {{1, chk_rhs}}, next_variant));
            ctx.switch_to_block(chk_rhs);
            LocalId t2 = emit_is(rhs, vt);
            if (vt->kind == hir::TypeKind::Null) {
                // null変種: 両側がnull変種ならペイロードは無く等値
                BlockId set_true = ctx.new_block();
                ctx.set_terminator(MirTerminator::switch_int(MirOperand::copy(MirPlace{t2}),
                                                             {{1, set_true}}, end_block));
                ctx.switch_to_block(set_true);
                MirConstant true_const;
                true_const.value = true;
                true_const.type = hir::make_bool();
                ctx.push_statement(MirStatement::assign(
                    MirPlace{result}, MirRvalue::use(MirOperand::constant(true_const))));
                ctx.set_terminator(MirTerminator::goto_block(end_block));
            } else {
                BlockId payload = ctx.new_block();
                ctx.set_terminator(MirTerminator::switch_int(MirOperand::copy(MirPlace{t2}),
                                                             {{1, payload}}, end_block));
                ctx.switch_to_block(payload);
                LocalId p1 = emit_as(lhs, vt);
                LocalId p2 = emit_as(rhs, vt);
                finish_with_eq(p1, vt, p2, vt);
            }
            ctx.switch_to_block(next_variant);
        }
        // どの変種タグでもない（不正状態）→ false のまま end へ
        ctx.set_terminator(MirTerminator::goto_block(end_block));
    } else {
        // 片側union: unionでない側の型に一致する変種のタグ検査＋ペイロード比較
        LocalId uv = l_union ? lhs : rhs;
        LocalId sv = l_union ? rhs : lhs;
        const hir::TypePtr& union_t = l_union ? lt : rt;
        const hir::TypePtr& scalar_t = l_union ? rt : lt;
        hir::TypePtr vt = nullptr;
        const std::string scalar_key = ast::type_to_string(*scalar_t);
        for (const auto& variant : ast::union_variant_types(union_t)) {
            if (variant && ast::type_to_string(*variant) == scalar_key) {
                vt = variant;
                break;
            }
        }
        if (vt) {
            BlockId payload = ctx.new_block();
            LocalId t1 = emit_is(uv, vt);
            ctx.set_terminator(MirTerminator::switch_int(MirOperand::copy(MirPlace{t1}),
                                                         {{1, payload}}, end_block));
            ctx.switch_to_block(payload);
            LocalId p = emit_as(uv, vt);
            finish_with_eq(p, vt, sv, scalar_t);
        } else {
            // 変種に無い型との比較は常にfalse
            ctx.set_terminator(MirTerminator::goto_block(end_block));
        }
    }

    ctx.switch_to_block(end_block);
    if (is_ne) {
        LocalId neg = ctx.new_temp(hir::make_bool());
        auto unary_rvalue = std::make_unique<MirRvalue>();
        unary_rvalue->kind = MirRvalue::UnaryOp;
        unary_rvalue->data =
            MirRvalue::UnaryOpData{MirUnaryOp::Not, MirOperand::copy(MirPlace{result})};
        ctx.push_statement(MirStatement::assign(MirPlace{neg}, std::move(unary_rvalue)));
        return neg;
    }
    return result;
}

namespace {

// lower_binaryの腕: 代入先の期待型を求める
// 配列リテラルRHSは代入先の型を期待型として渡す（`h.vs = []` のような空リテラルが要素型int既定に落ちるのを防ぐ）
hir::TypePtr cm_assign_target_type(const hir::HirBinary& bin, LoweringContext& ctx) {
    hir::TypePtr assign_target_type = bin.lhs ? bin.lhs->type : nullptr;
    if ((!assign_target_type || assign_target_type->kind != hir::TypeKind::Array) && bin.lhs) {
        if (auto* mem = std::get_if<std::unique_ptr<hir::HirMember>>(&bin.lhs->kind)) {
            const auto& obj = (*mem)->object;
            if (obj && obj->type && obj->type->kind == hir::TypeKind::Struct && ctx.struct_defs &&
                ctx.struct_defs->count(obj->type->name)) {
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
    return assign_target_type;
}

// lower_binaryの腕: スライスへのインデックス書き込みの正規化
// スライスへのインデックス書き込みはCmSlice*への直接GEPになり不正（SIGBUS）なため、要素ポインタ経由のデリファレンス格納へ正規化する
void cm_normalize_slice_index_store(MirPlace& place, LoweringContext& ctx) {
    hir::TypePtr walk_type = nullptr;
    if (place.local < ctx.func->locals.size()) {
        walk_type = ctx.func->locals[place.local].type;
    }
    for (size_t pi = 0; pi < place.projections.size(); ++pi) {
        const auto& proj = place.projections[pi];
        bool is_slice_base = walk_type && walk_type->kind == hir::TypeKind::Array &&
                             !walk_type->array_size.has_value() &&
                             walk_type->size_param_name.empty();
        if (proj.kind == ProjectionKind::Index && is_slice_base) {
            hir::TypePtr elem_type =
                walk_type->element_type ? walk_type->element_type : hir::make_int();
            const bool elem_is_slice =
                elem_type->kind == hir::TypeKind::Array && !elem_type->array_size.has_value();

            // スライス値（CmSlice*）を場所化する。基点が{ptr, deref}（インライン格納の
            // 内側ヘッダ）の場合、derefコピーはヘッダ先頭のdataポインタを値として読む
            // 誤り（W2のSIGSEGV源）のため、ポインタ自体をCmSlice*として使う
            LocalId slice_local;
            MirPlace slice_place = place;
            slice_place.projections.resize(pi);
            if (slice_place.projections.size() == 1 &&
                slice_place.projections[0].kind == ProjectionKind::Deref) {
                slice_local = slice_place.local;
            } else if (slice_place.projections.empty()) {
                slice_local = slice_place.local;
            } else {
                slice_local = ctx.new_temp(walk_type);
                ctx.push_statement(MirStatement::assign(
                    MirPlace{slice_local}, MirRvalue::use(MirOperand::copy(slice_place))));
            }

            // 中間スライス段（要素がスライス）は参照版subsliceで内側ヘッダへ降下する（W2）。
            // 最終要素段は要素ポインタ経由のデリファレンス格納にする
            const std::string get_func =
                elem_is_slice ? "cm_slice_get_subslice_ref" : "cm_slice_get_element_ptr";
            hir::TypePtr dest_type = elem_is_slice ? elem_type : hir::make_pointer(elem_type);
            LocalId elem_ptr = ctx.new_temp(dest_type);
            BlockId next_block = ctx.new_block();
            std::vector<MirOperandPtr> ep_args;
            ep_args.push_back(MirOperand::copy(MirPlace{slice_local}));
            ep_args.push_back(MirOperand::copy(MirPlace{proj.index_local}));
            auto ep_term = std::make_unique<MirTerminator>();
            ep_term->kind = MirTerminator::Call;
            ep_term->data = MirTerminator::CallData{MirOperand::function_ref(get_func),
                                                    std::move(ep_args),
                                                    MirPlace{elem_ptr},
                                                    next_block,
                                                    std::nullopt,
                                                    "",
                                                    "",
                                                    false};
            ctx.set_terminator(std::move(ep_term));
            ctx.switch_to_block(next_block);

            // 残りのプロジェクションを新基点へ付け替える
            MirPlace new_place{elem_ptr};
            if (!elem_is_slice) {
                new_place.projections.push_back(PlaceProjection::deref());
            }
            for (size_t rest = pi + 1; rest < place.projections.size(); ++rest) {
                new_place.projections.push_back(place.projections[rest]);
            }
            place = new_place;
            walk_type = elem_type;
            if (elem_is_slice) {
                // subslice降下は新placeの先頭が次のindex投影のため、0から再走査する
                // （continueでpi++されるためSIZE_MAXを経由して0に戻す）
                pi = static_cast<size_t>(-1);
            } else {
                pi = 0;  // deref基点はderefを飛ばして次から再走査
            }
            continue;
        }
        // 型を追跡
        if (proj.kind == ProjectionKind::Index || proj.kind == ProjectionKind::Deref) {
            walk_type = walk_type ? walk_type->element_type : nullptr;
        } else if (proj.kind == ProjectionKind::Field) {
            if (walk_type && walk_type->kind == hir::TypeKind::Struct && ctx.struct_defs &&
                ctx.struct_defs->count(walk_type->name)) {
                const auto* sd = ctx.struct_defs->at(walk_type->name);
                walk_type =
                    proj.field_id < sd->fields.size() ? sd->fields[proj.field_id].type : nullptr;
            } else {
                walk_type = nullptr;
            }
        }
    }
}

// lower_binaryの腕: HIRの二項演算子をMIRの二項演算子へ変換する
MirBinaryOp cm_to_mir_binary_op(hir::HirBinaryOp op) {
    // MIRの二項演算子に変換
    MirBinaryOp mir_op;
    switch (op) {
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
    return mir_op;
}

// lower_binaryの腕: 浮動小数×整数の混合オペランド防衛層（発火時はエラー一時を返す）
std::optional<LocalId> cm_check_float_int_mix(const hir::HirBinary& bin, LocalId lhs, LocalId rhs,
                                              MirBinaryOp mir_op, bool is_comparison,
                                              LoweringContext& ctx) {
    // 防衛層（Y4）: 浮動小数×整数の混合オペランドは型検査の昇格Cast挿入で解消されている前提。
    // ここへ混合が到達した場合は上流の欠陥であり、fadd i32, double等の不正IRを黙って発行せず診断で停止する
    {
        auto floatness = [&](const hir::TypePtr& t0, LocalId local) -> int {
            hir::TypePtr t = t0;
            if ((!t || t->is_error()) && local < ctx.func->locals.size()) {
                t = ctx.func->locals[local].type;
            }
            t = t ? ctx.resolve_typedef(t) : nullptr;
            if (!t) {
                return -1;  // 不明（判定不能なら防衛層は発火させない）
            }
            switch (t->kind) {
                case hir::TypeKind::Float:
                case hir::TypeKind::UFloat:
                case hir::TypeKind::Double:
                case hir::TypeKind::UDouble:
                    return 1;
                case hir::TypeKind::Tiny:
                case hir::TypeKind::UTiny:
                case hir::TypeKind::Short:
                case hir::TypeKind::UShort:
                case hir::TypeKind::Int:
                case hir::TypeKind::UInt:
                case hir::TypeKind::Long:
                case hir::TypeKind::ULong:
                case hir::TypeKind::ISize:
                case hir::TypeKind::USize:
                case hir::TypeKind::Char:
                case hir::TypeKind::Bool:
                    return 0;
                default:
                    return -1;
            }
        };
        const bool op_is_numeric = is_comparison || mir_op == MirBinaryOp::Add ||
                                   mir_op == MirBinaryOp::Sub || mir_op == MirBinaryOp::Mul ||
                                   mir_op == MirBinaryOp::Div || mir_op == MirBinaryOp::Mod;
        if (op_is_numeric) {
            int lf = floatness(bin.lhs->type, lhs);
            int rf = floatness(bin.rhs->type, rhs);
            if (lf >= 0 && rf >= 0 && lf != rf) {
                debug::log(debug::Stage::Mir, debug::Level::Error,
                           "二項演算の浮動小数×整数混合オペランドがMIRへ到達しました（型検査の昇格"
                           "挿入漏れ）");
                return ctx.new_temp(hir::make_error());
            }
        }
    }
    return std::nullopt;
}

// lower_binaryの腕: 二項演算の結果型を決定する
// 結果型を決定
// 比較演算子 -> bool
// 算術演算子 -> 左辺の型（または型昇格）
hir::TypePtr cm_binary_result_type(const hir::HirBinary& bin, LocalId lhs, LocalId rhs,
                                   bool is_comparison, LoweringContext& ctx) {
    hir::TypePtr result_type;
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
    return result_type;
}

}  // namespace

// lower_binaryの腕: 文字列連結チェーンの平坦化（3要素以上のみ該当。非該当ならnullopt）
// 文字列連結チェーンの平坦化（H9第5段）: a + b + c (+ d ...) をcm_string_concat3/4へ
// まとめ、中間バッファの確保回数を減らす（3要素: 2回確保→1回、4要素: 3回→1回）
std::optional<LocalId> ExprLowering::try_lower_string_concat_chain(const hir::HirBinary& bin,
                                                                   LoweringContext& ctx) {
    if (bin.op == hir::HirBinaryOp::Add) {
        // lhs/rhsの型から文字列連結かを判定する
        const bool lhs_str =
            bin.lhs && bin.lhs->type && bin.lhs->type->kind == hir::TypeKind::String;
        const bool rhs_str =
            bin.rhs && bin.rhs->type && bin.rhs->type->kind == hir::TypeKind::String;
        if (lhs_str || rhs_str) {
            std::vector<const hir::HirExpr*> parts;
            cm_flatten_string_concat(*bin.lhs, parts);
            cm_flatten_string_concat(*bin.rhs, parts);
            if (parts.size() >= 3) {
                // 各要素をlowerし、非文字列は文字列へ変換する
                std::vector<LocalId> str_parts;
                str_parts.reserve(parts.size());
                for (const auto* pe : parts) {
                    LocalId v = lower_expression(*pe, ctx);
                    if (pe->type && pe->type->kind == hir::TypeKind::String) {
                        str_parts.push_back(v);
                    } else {
                        str_parts.push_back(convert_to_string(v, pe->type, ctx));
                    }
                }
                // 4要素ずつ（2回目以降は前結果+3要素）でconcat2/3/4に畳む
                size_t i = 0;
                LocalId cur = 0;
                bool has_cur = false;
                const size_t n = str_parts.size();
                while (i < n) {
                    const size_t take = std::min<size_t>(has_cur ? 3 : 4, n - i);
                    std::vector<MirOperandPtr> args;
                    if (has_cur) {
                        args.push_back(MirOperand::copy(MirPlace{cur}));
                    }
                    for (size_t k = 0; k < take; ++k) {
                        args.push_back(MirOperand::copy(MirPlace{str_parts[i + k]}));
                    }
                    i += take;
                    const size_t argc = args.size();
                    const char* fn = argc == 2   ? "cm_string_concat"
                                     : argc == 3 ? "cm_string_concat3"
                                                 : "cm_string_concat4";
                    LocalId result = ctx.new_temp(hir::make_string());
                    BlockId ok = ctx.new_block();
                    auto term = std::make_unique<MirTerminator>();
                    term->kind = MirTerminator::Call;
                    term->data = MirTerminator::CallData{MirOperand::function_ref(fn),
                                                         std::move(args),
                                                         MirPlace{result},
                                                         ok,
                                                         std::nullopt,
                                                         "",
                                                         "",
                                                         false};
                    ctx.set_terminator(std::move(term));
                    ctx.switch_to_block(ok);
                    // 中間・最終結果とも無名一時としてdropパスへ登録する（中間は次のconcatに
                    // 消費されるだけなので文末に解放され、最終結果は既存のエスケープ規則に乗る）
                    ctx.note_string_temp(result);
                    cur = result;
                    has_cur = true;
                }
                return cur;
            }
        }
    }
    return std::nullopt;
}

// lower_binaryの腕: 代入RHSが配列リテラルの場合の直接要素書き込み（非該当ならnullopt）
// Bug#14修正: 右辺が配列リテラルの場合、temp経由のcopyを避けて
// 直接ターゲット変数の各インデックスに要素を書き込む。
// temp経由copyでは構造体要素の配列で正しくコピーされない問題がある。
std::optional<LocalId> ExprLowering::try_lower_assign_array_literal_unroll(
    const hir::HirBinary& bin, LoweringContext& ctx) {
    if (auto* arr_lit_ptr = std::get_if<std::unique_ptr<hir::HirArrayLiteral>>(&bin.rhs->kind)) {
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
    return std::nullopt;
}

// lower_binaryの腕: 代入左辺値のMirPlace構築（再帰）
// 左辺値のMirPlaceを構築するヘルパー関数
// 複雑な左辺値（c.values[0], points[0].x など）を再帰的に処理
bool ExprLowering::build_assign_lvalue_place(const hir::HirExpr* expr, MirPlace& place,
                                             hir::TypePtr& current_type, LoweringContext& ctx) {
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
        if (!build_assign_lvalue_place((*member)->object.get(), place, inner_type, ctx)) {
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
                        // 注: HIRでTがStruct扱いになる場合があるため、kindではなくgeneric_params名で照合する
                        if (field_type && !inner_type->type_args.empty()) {
                            // ジェネリックパラメータ名を型引数にマッピング
                            for (size_t i = 0; i < struct_def->generic_params.size() &&
                                               i < inner_type->type_args.size();
                                 ++i) {
                                if (struct_def->generic_params[i].name == field_type->name) {
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
        if (!build_assign_lvalue_place((*index)->object.get(), place, inner_type, ctx)) {
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
            if (!build_assign_lvalue_place((*unary)->operand.get(), place, inner_type, ctx)) {
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
}

// lower_binaryの腕: 代入演算の処理
LocalId ExprLowering::lower_assign(const hir::HirBinary& bin, LoweringContext& ctx) {
    // 代入RHSが配列リテラルの腕（該当時は直接要素書き込みで完結する）
    if (auto unrolled = try_lower_assign_array_literal_unroll(bin, ctx)) {
        return *unrolled;
    }

    // 右辺を先に評価
    hir::TypePtr assign_target_type = cm_assign_target_type(bin, ctx);

    LocalId rhs_value;
    if (auto* rhs_arr_lit = std::get_if<std::unique_ptr<hir::HirArrayLiteral>>(&bin.rhs->kind);
        rhs_arr_lit && assign_target_type && assign_target_type->kind == hir::TypeKind::Array) {
        rhs_value = lower_array_literal(**rhs_arr_lit, assign_target_type, ctx);
    } else {
        rhs_value = lower_expression(*bin.rhs, ctx);
    }

    // 左辺値のMirPlaceを構築
    MirPlace place{0};
    hir::TypePtr current_type;

    if (build_assign_lvalue_place(bin.lhs.get(), place, current_type, ctx)) {
        // スライスへのインデックス書き込みは要素ポインタ経由のデリファレンス格納へ正規化する
        cm_normalize_slice_index_store(place, ctx);

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
        // 変換統一ドライバ第1段: numeric/ユニオン構築/固定長配列→スライスをcoerce_to_expected 1系統で挿入する（B2）
        (void)rhs_resolved;
        rhs_value = ctx.coerce_to_expected(rhs_value, lhs_resolved);
        ctx.push_statement(
            MirStatement::assign(place, MirRvalue::use(MirOperand::copy(MirPlace{rhs_value}))));
        return rhs_value;
    }

    // その他の左辺（エラー）は評価済みの右辺値を返す
    return rhs_value;
}

// lower_binaryの腕: AND演算の短絡評価
LocalId ExprLowering::lower_logical_and(const hir::HirBinary& bin, LoweringContext& ctx) {
    // AND演算の短絡評価
    // 左辺を評価
    LocalId lhs = lower_expression(*bin.lhs, ctx);

    // 結果を格納する変数
    LocalId result = ctx.new_temp(hir::make_bool());

    // ブロックを作成
    BlockId eval_rhs = ctx.new_block();  // 右辺を評価するブロック
    BlockId skip_rhs = ctx.new_block();  // 右辺をスキップするブロック（結果はfalse）
    BlockId merge = ctx.new_block();     // 結果を統合するブロック

    // 左辺がtrueなら右辺を評価、falseならスキップ
    ctx.set_terminator(
        MirTerminator::switch_int(MirOperand::copy(MirPlace{lhs}), {{1, eval_rhs}}, skip_rhs));

    // 右辺を評価するブロック（条件付き実行のため内側の文字列一時は文末drop対象外・C12。
    // 腕スコープでトラッキングし、腕内で完結した一時は腕ブロック内で解放する）
    ctx.switch_to_block(eval_rhs);
    ctx.conditional_expr_depth++;
    bool and_rhs_arm = begin_arm_temp_scope(ctx);
    LocalId rhs = lower_expression(*bin.rhs, ctx);
    ctx.conditional_expr_depth--;
    // 結果は右辺の値（左辺は既にtrue）
    ctx.push_statement(
        MirStatement::assign(MirPlace{result}, MirRvalue::use(MirOperand::copy(MirPlace{rhs}))));
    end_arm_temp_scope(ctx, and_rhs_arm);
    ctx.set_terminator(MirTerminator::goto_block(merge));

    // 右辺をスキップするブロック（左辺がfalse）
    ctx.switch_to_block(skip_rhs);
    // 結果はfalse
    MirConstant false_const;
    false_const.type = hir::make_bool();
    false_const.value = false;
    ctx.push_statement(
        MirStatement::assign(MirPlace{result}, MirRvalue::use(MirOperand::constant(false_const))));
    ctx.set_terminator(MirTerminator::goto_block(merge));

    // マージブロック
    ctx.switch_to_block(merge);

    return result;
}

// lower_binaryの腕: OR演算の短絡評価
LocalId ExprLowering::lower_logical_or(const hir::HirBinary& bin, LoweringContext& ctx) {
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
    ctx.push_statement(
        MirStatement::assign(MirPlace{result}, MirRvalue::use(MirOperand::constant(true_const))));
    ctx.set_terminator(MirTerminator::goto_block(merge));

    // 右辺を評価するブロック（条件付き実行のため内側の文字列一時は文末drop対象外・C12。
    // 腕スコープでトラッキングし、腕内で完結した一時は腕ブロック内で解放する）
    ctx.switch_to_block(eval_rhs);
    ctx.conditional_expr_depth++;
    bool or_rhs_arm = begin_arm_temp_scope(ctx);
    LocalId rhs = lower_expression(*bin.rhs, ctx);
    ctx.conditional_expr_depth--;
    // 結果は右辺の値（左辺は既にfalse）
    ctx.push_statement(
        MirStatement::assign(MirPlace{result}, MirRvalue::use(MirOperand::copy(MirPlace{rhs}))));
    end_arm_temp_scope(ctx, or_rhs_arm);
    ctx.set_terminator(MirTerminator::goto_block(merge));

    // マージブロック
    ctx.switch_to_block(merge);

    return result;
}

// lower_binaryの腕: 構造体の比較演算子（with による自動実装。非該当ならnullopt）
std::optional<LocalId> ExprLowering::try_lower_struct_eq_op(const hir::HirBinary& bin, LocalId lhs,
                                                            LocalId rhs, LoweringContext& ctx) {
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
                        op_func_name = ast::typekey::spec_fn_prefix(type_name) + "__op_eq";
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
    return std::nullopt;
}

// lower_binaryの腕: 構造体の順序演算子（with Ord による自動実装。非該当ならnullopt）
std::optional<LocalId> ExprLowering::try_lower_struct_ord_op(const hir::HirBinary& bin, LocalId lhs,
                                                             LocalId rhs, LoweringContext& ctx) {
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
                        op_func_name = ast::typekey::spec_fn_prefix(type_name) + "__op_lt";
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
    return std::nullopt;
}

// lower_binaryの腕: 構造体の算術演算子（impl for Add/Sub/Mul/Div/Mod。非該当ならnullopt）
std::optional<LocalId> ExprLowering::try_lower_struct_arith_op(const hir::HirBinary& bin,
                                                               LocalId lhs, LocalId rhs,
                                                               LoweringContext& ctx) {
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
    return std::nullopt;
}

// lower_binaryの腕: 2オペランドの文字列連結（非該当ならnullopt）
std::optional<LocalId> ExprLowering::try_lower_string_concat_pair(const hir::HirBinary& bin,
                                                                  LocalId lhs, LocalId rhs,
                                                                  LoweringContext& ctx) {
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

            // concat結果は新規確保された無名一時。文末のdropパス対象として登録する（C12）
            ctx.note_string_temp(result);

            return result;
        }
    }
    return std::nullopt;
}

LocalId ExprLowering::lower_binary(const hir::HirBinary& bin, LoweringContext& ctx) {
    // 文字列連結チェーンの平坦化の腕（3要素以上のみ該当。非該当なら以降の腕へ流す）
    if (auto chain_result = try_lower_string_concat_chain(bin, ctx)) {
        return *chain_result;
    }

    // 代入演算の腕
    if (bin.op == hir::HirBinaryOp::Assign) {
        return lower_assign(bin, ctx);
    }

    // 論理演算 (AND/OR) - 短絡評価を実装
    if (bin.op == hir::HirBinaryOp::And) {
        return lower_logical_and(bin, ctx);
    }
    if (bin.op == hir::HirBinaryOp::Or) {
        return lower_logical_or(bin, ctx);
    }

    // 通常の二項演算
    // 左辺と右辺をlowering
    LocalId lhs = lower_expression(*bin.lhs, ctx);
    LocalId rhs = lower_expression(*bin.rhs, ctx);

    // ユニオンの等値比較（Eq/Ne）: 生表現比較は誤値になるため、タグ一致＋アクティブ変種のペイロード比較へ脱糖する
    if (bin.op == hir::HirBinaryOp::Eq || bin.op == hir::HirBinaryOp::Ne) {
        hir::TypePtr lt = ctx.resolve_typedef(bin.lhs ? bin.lhs->type : nullptr);
        hir::TypePtr rt = ctx.resolve_typedef(bin.rhs ? bin.rhs->type : nullptr);
        const bool l_union = lt && lt->kind == hir::TypeKind::Union;
        const bool r_union = rt && rt->kind == hir::TypeKind::Union;
        if (l_union || r_union) {
            return cm_lower_union_equality(bin.op == hir::HirBinaryOp::Ne, lhs, rhs, lt, rt,
                                           l_union, r_union, ctx);
        }
    }

    // 構造体の比較演算子の腕（with による自動実装）
    if (auto eq_result = try_lower_struct_eq_op(bin, lhs, rhs, ctx)) {
        return *eq_result;
    }

    // 構造体の順序演算子の腕（with Ord による自動実装）
    if (auto ord_result = try_lower_struct_ord_op(bin, lhs, rhs, ctx)) {
        return *ord_result;
    }

    // 構造体の算術演算子の腕（impl for Add/Sub/Mul/Div/Mod）
    if (auto arith_result = try_lower_struct_arith_op(bin, lhs, rhs, ctx)) {
        return *arith_result;
    }

    // 文字列連結の腕（2オペランド）
    if (auto concat_result = try_lower_string_concat_pair(bin, lhs, rhs, ctx)) {
        return *concat_result;
    }

    // MIRの二項演算子に変換
    MirBinaryOp mir_op = cm_to_mir_binary_op(bin.op);

    bool is_comparison =
        (mir_op == MirBinaryOp::Eq || mir_op == MirBinaryOp::Ne || mir_op == MirBinaryOp::Lt ||
         mir_op == MirBinaryOp::Le || mir_op == MirBinaryOp::Gt || mir_op == MirBinaryOp::Ge);

    // 防衛層（Y4）: 浮動小数×整数の混合オペランド検査（発火時はエラー一時を返して停止する）
    if (auto mixed_error = cm_check_float_int_mix(bin, lhs, rhs, mir_op, is_comparison, ctx)) {
        return *mixed_error;
    }

    // 結果型を決定（比較はbool・算術は型昇格）
    hir::TypePtr result_type = cm_binary_result_type(bin, lhs, rhs, is_comparison, ctx);

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