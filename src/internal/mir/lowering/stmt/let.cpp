// MIR lowering - let文（定数畳み込みヘルパーと配列/スライス初期化を含む変数宣言の展開）

#include "internal/base/debug.hpp"
#include "internal/base/target.hpp"
#include "internal/mir/lowering/slice_dispatch.hpp"
#include "internal/mir/lowering/stmt.hpp"
#include "internal/mir/passes/scalar/const_eval.hpp"

#include <cinttypes>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cm::mir {

// 型の幅を数値化（二項演算の結果型決定用）
static int type_width(const hir::TypePtr& type) {
    if (!type)
        return 32;
    switch (type->kind) {
        case hir::TypeKind::ULong:
            return 65;  // ulongは最も広い
        case hir::TypeKind::Long:
            return 64;
        case hir::TypeKind::UInt:
            return 33;
        default:
            return 32;  // int
    }
}

// LHS/RHSの型のうち広い方を返す
static hir::TypePtr wider_type(const hir::TypePtr& lhs, const hir::TypePtr& rhs) {
    return type_width(lhs) >= type_width(rhs) ? lhs : rhs;
}

// コンパイル時定数評価（const folding）
// HIR式がコンパイル時に評価可能な場合、MirConstantを返す
static std::optional<MirConstant> try_const_eval(const hir::HirExpr& expr, LoweringContext& ctx) {
    // リテラルの場合
    if (auto* lit = std::get_if<std::unique_ptr<hir::HirLiteral>>(&expr.kind)) {
        if (*lit) {
            MirConstant c;
            c.type = expr.type ? expr.type : hir::make_int();
            c.value = (*lit)->value;
            return c;
        }
    }

    // 変数参照の場合（既登録のconst変数を伝搬）
    if (auto* var = std::get_if<std::unique_ptr<hir::HirVarRef>>(&expr.kind)) {
        if (*var) {
            auto cv = ctx.get_const_value((*var)->name);
            if (cv)
                return *cv;
        }
    }

    // 単項マイナスの場合
    if (auto* unary = std::get_if<std::unique_ptr<hir::HirUnary>>(&expr.kind)) {
        if (*unary && (*unary)->op == hir::HirUnaryOp::Neg && (*unary)->operand) {
            auto inner = try_const_eval(*(*unary)->operand, ctx);
            if (inner && std::holds_alternative<int64_t>(inner->value)) {
                MirConstant c;
                c.type = inner->type;
                c.value = -std::get<int64_t>(inner->value);
                return c;
            }
        }
    }

    // 二項演算の場合（ビット演算、算術演算）
    if (auto* bin = std::get_if<std::unique_ptr<hir::HirBinary>>(&expr.kind)) {
        if (*bin && (*bin)->lhs && (*bin)->rhs) {
            auto lhs = try_const_eval(*(*bin)->lhs, ctx);
            auto rhs = try_const_eval(*(*bin)->rhs, ctx);
            if (lhs && rhs && std::holds_alternative<int64_t>(lhs->value) &&
                std::holds_alternative<int64_t>(rhs->value)) {
                int64_t l = std::get<int64_t>(lhs->value);
                int64_t r = std::get<int64_t>(rhs->value);
                int64_t result = 0;
                bool ok = true;
                // 符号なし型は論理シフト・符号なし除算で畳み込む（folding.cppと同じ規則）
                const bool uns = const_eval::use_unsigned_op(lhs->type, rhs->type);
                switch ((*bin)->op) {
                    case hir::HirBinaryOp::Add:
                        result = l + r;
                        break;
                    case hir::HirBinaryOp::Sub:
                        result = l - r;
                        break;
                    case hir::HirBinaryOp::Mul:
                        result = l * r;
                        break;
                    case hir::HirBinaryOp::Div:
                        if (r != 0)
                            result = uns ? static_cast<int64_t>(static_cast<uint64_t>(l) /
                                                                static_cast<uint64_t>(r))
                                         : l / r;
                        else
                            ok = false;
                        break;
                    case hir::HirBinaryOp::Mod:
                        if (r != 0)
                            result = uns ? static_cast<int64_t>(static_cast<uint64_t>(l) %
                                                                static_cast<uint64_t>(r))
                                         : l % r;
                        else
                            ok = false;
                        break;
                    case hir::HirBinaryOp::BitAnd:
                        result = l & r;
                        break;
                    case hir::HirBinaryOp::BitOr:
                        result = l | r;
                        break;
                    case hir::HirBinaryOp::BitXor:
                        result = l ^ r;
                        break;
                    case hir::HirBinaryOp::Shl:
                        result = l << r;
                        break;
                    case hir::HirBinaryOp::Shr:
                        result = uns ? static_cast<int64_t>(static_cast<uint64_t>(l) >>
                                                            (static_cast<uint64_t>(r) & 63))
                                     : l >> (r & 63);
                        break;
                    default:
                        ok = false;
                        break;
                }
                if (ok) {
                    MirConstant c;
                    // LHS/RHSの型のうち広い方を結果型とする
                    c.type = wider_type(lhs->type, rhs->type);
                    // 型幅へ正規化する（int32のオーバーフローをラップ等。
                    // folding.cppと同じ規則。生のint64値を伝播すると
                    // 表示値と比較結果が実行時セマンティクスと食い違う）
                    c.value = const_eval::normalize_int(result, c.type);
                    return c;
                }
            }
        }
    }

    return std::nullopt;
}

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
        // 元の型引数を保持（補間ミニパイプラインでのペイロード型復元に使用）
        tagged_union_type->type_args = let.type->type_args;
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

    // const変数の場合、初期値をコンパイル時評価して保存
    // リテラル、const参照、二項演算（ビット演算含む）に対応
    if (let.is_const && let.init) {
        auto const_val = try_const_eval(*let.init, ctx);
        if (const_val) {
            const_val->type = let.type ? let.type : const_val->type;
            ctx.register_const_value(let.name, *const_val);
        }
    }

    // static変数: 格納はゼロ初期化のグローバル（バックエンド側でfunc名_変数名の永続領域）とし、
    // 初期化子は初回到達時に1回だけ実行するガード付き代入として発行する（X1。
    // 従来は初期化コード自体を生成せず、非ゼロ初期値が全スコープで無視されていた）
    if (let.is_static) {
        if (let.init) {
            // ガード用のstatic bool（ゼロ初期化=未初期化）
            LocalId guard =
                ctx.new_local(let.name + "__static_guard", hir::make_bool(), true, false, true);
            LocalId guard_val = ctx.new_temp(hir::make_bool());
            ctx.push_statement(MirStatement::assign(
                MirPlace{guard_val}, MirRvalue::use(MirOperand::copy(MirPlace{guard}))));

            BlockId init_block = ctx.new_block();
            BlockId after_block = ctx.new_block();
            ctx.set_terminator(MirTerminator::switch_int(MirOperand::copy(MirPlace{guard_val}),
                                                         {{0, init_block}}, after_block));

            ctx.switch_to_block(init_block);
            MirConstant true_const;
            true_const.value = true;
            true_const.type = hir::make_bool();
            ctx.push_statement(MirStatement::assign(
                MirPlace{guard}, MirRvalue::use(MirOperand::constant(true_const))));
            LocalId init_value = expr_lowering->lower_expression(*let.init, ctx);
            ctx.push_statement(MirStatement::assign(
                MirPlace{local}, MirRvalue::use(MirOperand::copy(MirPlace{init_value}))));
            ctx.set_terminator(MirTerminator::goto_block(after_block));

            ctx.switch_to_block(after_block);
        }
        return;
    }

    // スライス型の変数で初期値がない場合、空のスライスを作成
    if (!let.init && let.type && let.type->kind == hir::TypeKind::Array &&
        !let.type->array_size.has_value()) {
        // 動的配列（スライス）の初期化
        // typedefエイリアス（例: Value = int | string）を解決してから要素サイズを決める
        hir::TypePtr elem_type =
            ctx.resolve_typedef(let.type->element_type ? let.type->element_type : hir::make_int());

        // 要素サイズを取得
        int64_t elem_size = 4;  // デフォルトはint
        auto elem_kind = elem_type->kind;
        if (auto info = slice_scalar_info(elem_kind)) {
            // スカラ型: elem_sizeをslice_dispatchから取得（アクセス幅と整合。C4）
            elem_size = info->elem_size;
        } else if (elem_kind == hir::TypeKind::Pointer || elem_kind == hir::TypeKind::String) {
            elem_size = 8;
        } else if (elem_kind == hir::TypeKind::Struct || elem_kind == hir::TypeKind::Union) {
            // 構造体・ユニオンはblob（値のインラインコピー）として格納する
            elem_size = ctx.layout_size(elem_type);
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
                // 配列リテラルからスライスへの初期化まず空のスライスを作成してから、各要素をpushで追加
                if (auto* arr_lit =
                        std::get_if<std::unique_ptr<hir::HirArrayLiteral>>(&let.init->kind)) {
                    const auto& elements = (*arr_lit)->elements;
                    // typedefエイリアスを解決してから要素サイズ・push関数を決める
                    hir::TypePtr elem_type = ctx.resolve_typedef(
                        let.type->element_type ? let.type->element_type : hir::make_int());

                    // 要素サイズを取得
                    int64_t elem_size = 4;  // デフォルトはint
                    auto elem_kind = elem_type->kind;
                    if (auto info = slice_scalar_info(elem_kind)) {
                        // スカラ型: elem_sizeをslice_dispatchから取得（アクセス幅と整合。C4）
                        elem_size = info->elem_size;
                    } else if (elem_kind == hir::TypeKind::Pointer ||
                               elem_kind == hir::TypeKind::String) {
                        elem_size = 8;
                    } else if (elem_kind == hir::TypeKind::Struct ||
                               elem_kind == hir::TypeKind::Union) {
                        // 構造体・ユニオンはblob（値のインラインコピー）として格納する
                        elem_size = ctx.layout_size(elem_type);
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
                    if (auto info = slice_scalar_info(elem_kind)) {
                        // スカラ型: 幅サフィックスをslice_dispatchから取得（elem_sizeと整合。C4）
                        push_func = std::string("cm_slice_push_") + info->width;
                    } else if (elem_kind == hir::TypeKind::Pointer ||
                               elem_kind == hir::TypeKind::String) {
                        push_func = "cm_slice_push_ptr";
                    } else if (elem_kind == hir::TypeKind::Union ||
                               elem_kind == hir::TypeKind::Struct) {
                        // ユニオン・構造体要素はblobとしてメモリコピー（push側と統一）
                        push_func = "cm_slice_push_blob";
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
                                if (auto info = slice_scalar_info(inner_elem_kind)) {
                                    // スカラ型: elem_sizeをslice_dispatchから取得（C4）
                                    inner_elem_size = info->elem_size;
                                } else if (inner_elem_kind == hir::TypeKind::Pointer ||
                                           inner_elem_kind == hir::TypeKind::String) {
                                    // cm_array_to_sliceのmemcpyは配列実ストライド（ポインタサイズ）基準
                                    inner_elem_size = cm::target_pointer_size();
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

                            // インターフェイス要素スライスへの具象構造体要素:
                            // インターフェイス型の一時へ代入してfat pointerを構築してからblob格納する（H1）
                            if (elem_kind == hir::TypeKind::Struct && ctx.interface_names &&
                                ctx.interface_names->count(elem_type->name) > 0) {
                                hir::TypePtr actual_type = nullptr;
                                if (elem_value < ctx.func->locals.size()) {
                                    actual_type = ctx.func->locals[elem_value].type;
                                }
                                if (actual_type && actual_type->kind == hir::TypeKind::Struct &&
                                    actual_type->name != elem_type->name) {
                                    LocalId iface_tmp = ctx.new_temp(elem_type);
                                    ctx.push_statement(MirStatement::assign(
                                        MirPlace{iface_tmp},
                                        MirRvalue::use(MirOperand::copy(MirPlace{elem_value}))));
                                    elem_value = iface_tmp;
                                }
                            }

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
                        if (push_func == "cm_slice_push_blob") {
                            // blob pushはデータ先頭へのポインタを受け取る
                            hir::TypePtr value_type = nullptr;
                            if (elem_value < ctx.func->locals.size()) {
                                value_type = ctx.func->locals[elem_value].type;
                            }
                            LocalId addr_local = ctx.new_temp(
                                hir::make_pointer(value_type ? value_type : hir::make_int()));
                            ctx.push_statement(MirStatement::assign(
                                MirPlace{addr_local}, MirRvalue::ref(MirPlace{elem_value}, false)));
                            args.push_back(MirOperand::copy(MirPlace{addr_local}));
                        } else {
                            args.push_back(MirOperand::copy(MirPlace{elem_value}));
                        }

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
                        auto resolved_ek = ctx.resolve_typedef(let.init->type->element_type);
                        auto ek =
                            resolved_ek ? resolved_ek->kind : let.init->type->element_type->kind;
                        if (auto info = slice_scalar_info(ek)) {
                            // スカラ型: elem_sizeをslice_dispatchから取得（short/ushort欠落を解消。C4）
                            elem_size = info->elem_size;
                        } else if (ek == hir::TypeKind::Pointer || ek == hir::TypeKind::String) {
                            // cm_array_to_sliceは配列をmemcpyするため、スロット幅は配列の実ストライド
                            // （ターゲットのポインタサイズ）に合わせる。8固定だとwasm32(4バイト)で
                            // 2要素目以降が範囲外読みになり文字列要素が壊れる
                            elem_size = cm::target_pointer_size();
                        } else if (ek == hir::TypeKind::Struct || ek == hir::TypeKind::Union) {
                            // 構造体・ユニオンはblob要素としてインラインコピーされる
                            elem_size = ctx.layout_size(let.init->type->element_type);
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
                    // ユニオン型変数を変種の値で初期化する場合はCast（ユニオン構築）を経由してタグ+ペイロードを書き込む（直接storeするとタグ未設定になり、O0での `as` タグ検査パニックや `is` の誤判定になる）
                    hir::TypePtr resolved_let_type = ctx.resolve_typedef(let.type);
                    hir::TypePtr init_type =
                        (init_value < ctx.func->locals.size())
                            ? ctx.resolve_typedef(ctx.func->locals[init_value].type)
                            : nullptr;
                    if (resolved_let_type && resolved_let_type->kind == hir::TypeKind::Union &&
                        (!init_type || init_type->kind != hir::TypeKind::Union)) {
                        ctx.push_statement(MirStatement::assign(
                            MirPlace{local}, MirRvalue::cast(MirOperand::copy(MirPlace{init_value}),
                                                             resolved_let_type)));
                    } else {
                        // 整数初期値を浮動小数変数へ入れる場合はsitofp/uitofp相当のCastを挿入する（B2）
                        init_value = ctx.coerce_to_float_context(init_value, resolved_let_type);
                        ctx.push_statement(MirStatement::assign(
                            MirPlace{local},
                            MirRvalue::use(MirOperand::copy(MirPlace{init_value}))));
                    }
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

}  // namespace cm::mir
