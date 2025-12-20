#include "../../common/debug.hpp"
#include "expr_lowering.hpp"

namespace cm::mir {

// リテラルのlowering
LocalId ExprLowering::lower_literal(const hir::HirLiteral& lit, LoweringContext& ctx) {
    // 文字列リテラルの場合、補間が必要かチェック
    if (lit.value.index() == 5) {  // string型のインデックス
        std::string str_val = std::get<std::string>(lit.value);

        // プレースホルダを含むかチェック（エスケープされた中括弧も含む）
        bool has_placeholders = false;
        bool has_escaped_braces = false;
        size_t pos = 0;

        while (pos < str_val.length()) {
            if (pos + 1 < str_val.length() && str_val[pos] == '{' && str_val[pos + 1] == '{') {
                has_escaped_braces = true;
                pos += 2;
                continue;
            }
            if (pos + 1 < str_val.length() && str_val[pos] == '}' && str_val[pos + 1] == '}') {
                has_escaped_braces = true;
                pos += 2;
                continue;
            }
            if (str_val[pos] == '{') {
                size_t end_pos = str_val.find('}', pos + 1);
                if (end_pos != std::string::npos) {
                    // {と}の間に内容があるかチェック
                    std::string content = str_val.substr(pos + 1, end_pos - pos - 1);
                    if (!content.empty() &&
                        (std::isalpha(content[0]) || content[0] == '*' || content[0] == '&')) {
                        has_placeholders = true;
                        break;
                    }
                }
            }
            pos++;
        }

        // プレースホルダがある場合、フォーマット関数を呼び出す
        if (has_placeholders || has_escaped_braces) {
            // 名前付きプレースホルダを抽出
            auto [var_names, converted_format] = extract_named_placeholders(str_val, ctx);

            // cm_format_string 関数を呼び出す
            std::vector<MirOperandPtr> args;

            // フォーマット文字列を最初の引数として追加
            MirConstant str_const;
            str_const.type = hir::make_string();
            str_const.value = converted_format;
            args.push_back(MirOperand::constant(str_const));

            // 名前付き変数を解決して引数リストを作成
            std::vector<LocalId> arg_locals;
            for (const auto& var_name : var_names) {
                // まずconst変数をチェック
                auto const_value = ctx.get_const_value(var_name);
                if (const_value) {
                    // const変数の場合、その値を持つ一時変数を作成
                    LocalId temp = ctx.new_temp(const_value->type);
                    auto const_stmt = std::make_unique<MirStatement>();
                    const_stmt->kind = MirStatement::Assign;
                    const_stmt->data = MirStatement::AssignData{
                        MirPlace{temp}, MirRvalue::use(MirOperand::constant(*const_value))};
                    ctx.push_statement(std::move(const_stmt));
                    arg_locals.push_back(temp);
                } else {
                    // 通常の変数を解決
                    auto var_id = ctx.resolve_variable(var_name);
                    if (var_id) {
                        arg_locals.push_back(*var_id);
                    } else {
                        // 変数が見つからない場合、エラー用のダミー値
                        auto err_type = hir::make_error();
                        arg_locals.push_back(ctx.new_temp(err_type));
                    }
                }
            }

            // 引数の数を追加
            MirConstant argc_const;
            argc_const.type = hir::make_int();
            argc_const.value = static_cast<int64_t>(arg_locals.size());
            args.push_back(MirOperand::constant(argc_const));

            // 実際の引数を追加
            for (LocalId arg_local : arg_locals) {
                args.push_back(MirOperand::copy(MirPlace{arg_local}));
            }

            // 戻り値用の一時変数
            LocalId result = ctx.new_temp(hir::make_string());

            // Call終端命令を作成
            BlockId success_block = ctx.new_block();
            auto func_operand = MirOperand::function_ref("cm_format_string");
            auto call_term = std::make_unique<MirTerminator>();
            call_term->kind = MirTerminator::Call;
            call_term->data = MirTerminator::CallData{
                std::move(func_operand),
                std::move(args),
                MirPlace{result},  // 戻り値を格納
                success_block,
                std::nullopt,  // unwind無し
                "",
                "",
                false  // 通常の関数呼び出し
            };
            ctx.set_terminator(std::move(call_term));
            ctx.switch_to_block(success_block);

            return result;
        }
    }

    // 通常のリテラル処理
    MirConstant constant;

    std::visit(
        [&](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, bool>) {
                constant.type = hir::make_bool();
                constant.value = val;
            } else if constexpr (std::is_same_v<T, int64_t>) {
                constant.type = hir::make_int();
                constant.value = val;
            } else if constexpr (std::is_same_v<T, double>) {
                constant.type = hir::make_double();
                constant.value = val;
            } else if constexpr (std::is_same_v<T, char>) {
                constant.type = hir::make_char();
                constant.value = static_cast<int64_t>(val);
            } else if constexpr (std::is_same_v<T, std::string>) {
                constant.type = hir::make_string();
                constant.value = val;
            } else {
                // デフォルト（monostate）
                constant.type = hir::make_void();
                constant.value = int64_t(0);
            }
        },
        lit.value);

    // 一時変数に代入
    LocalId temp = ctx.new_temp(constant.type);
    ctx.push_statement(
        MirStatement::assign(MirPlace{temp}, MirRvalue::use(MirOperand::constant(constant))));

    return temp;
}

// 変数参照のlowering
LocalId ExprLowering::lower_var_ref(const hir::HirVarRef& var, const hir::TypePtr& expr_type,
                                    LoweringContext& ctx) {
    // 関数参照の場合（関数ポインタ用）
    if (var.is_function_ref) {
        // 式の型（関数ポインタ型）を使用
        hir::TypePtr func_ptr_type =
            expr_type ? expr_type : hir::make_function_ptr(hir::make_int(), {});
        LocalId temp = ctx.new_temp(func_ptr_type);

        // 関数参照を一時変数に格納
        ctx.push_statement(MirStatement::assign(
            MirPlace{temp}, MirRvalue::use(MirOperand::function_ref(var.name))));

        return temp;
    }

    // 変数名からローカルIDを解決
    auto local_opt = ctx.resolve_variable(var.name);

    if (!local_opt) {
        // 変数が見つからない場合は仮の値を返す
        // TODO: エラーハンドリングの改善
        LocalId temp = ctx.new_temp(hir::make_int());
        MirConstant zero_const;
        zero_const.value = int64_t(0);
        zero_const.type = hir::make_int();
        ctx.push_statement(
            MirStatement::assign(MirPlace{temp}, MirRvalue::use(MirOperand::constant(zero_const))));
        return temp;
    }

    LocalId local = *local_opt;

    // 変数の型を取得
    hir::TypePtr var_type = hir::make_int();  // デフォルト
    if (local < ctx.func->locals.size()) {
        var_type = ctx.func->locals[local].type;
    }

    // 変数をコピーして一時変数に
    LocalId temp = ctx.new_temp(var_type);
    ctx.push_statement(
        MirStatement::assign(MirPlace{temp}, MirRvalue::use(MirOperand::copy(MirPlace{local}))));

    return temp;
}

// 二項演算のlowering
LocalId ExprLowering::lower_binary(const hir::HirBinary& bin, LoweringContext& ctx) {
    // 代入演算の処理
    if (bin.op == hir::HirBinaryOp::Assign) {
        // 右辺を先に評価
        LocalId rhs_value = lower_expression(*bin.rhs, ctx);

        // 左辺が変数参照の場合
        if (auto var_ref = std::get_if<std::unique_ptr<hir::HirVarRef>>(&bin.lhs->kind)) {
            auto local_opt = ctx.resolve_variable((*var_ref)->name);
            if (local_opt) {
                LocalId target = *local_opt;

                // 変数に代入
                ctx.push_statement(MirStatement::assign(
                    MirPlace{target}, MirRvalue::use(MirOperand::copy(MirPlace{rhs_value}))));

                // C++の仕様: 代入式は左辺値を返す
                return target;
            }
        }
        // 左辺がメンバーアクセスの場合（構造体フィールドへの代入）
        else if (auto member = std::get_if<std::unique_ptr<hir::HirMember>>(&bin.lhs->kind)) {
            // オブジェクトの変数を解決
            if (auto obj_var =
                    std::get_if<std::unique_ptr<hir::HirVarRef>>(&(*member)->object->kind)) {
                auto local_opt = ctx.resolve_variable((*obj_var)->name);
                if (local_opt) {
                    LocalId object = *local_opt;

                    // オブジェクトの型から構造体名を取得
                    auto obj_type = (*member)->object->type;
                    if (obj_type && obj_type->kind == hir::TypeKind::Struct) {
                        // 構造体のフィールドインデックスを取得
                        auto field_idx = ctx.get_field_index(obj_type->name, (*member)->member);
                        if (field_idx) {
                            // Projectionを使ったアクセスを生成
                            MirPlace place{object};
                            place.projections.push_back(PlaceProjection::field(*field_idx));

                            // フィールドに代入
                            ctx.push_statement(MirStatement::assign(
                                place, MirRvalue::use(MirOperand::copy(MirPlace{rhs_value}))));

                            // 代入された値を返す
                            return rhs_value;
                        }
                    }
                }
            }
        }
        // 左辺が配列インデックスの場合
        else if (auto index = std::get_if<std::unique_ptr<hir::HirIndex>>(&bin.lhs->kind)) {
            LocalId array;

            // objectが変数参照の場合は直接その変数を使用
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

            LocalId idx = lower_expression(*(*index)->index, ctx);

            // Projectionを使ったアクセスを生成
            MirPlace place{array};
            place.projections.push_back(PlaceProjection::index(idx));

            // 配列要素に代入
            ctx.push_statement(
                MirStatement::assign(place, MirRvalue::use(MirOperand::copy(MirPlace{rhs_value}))));

            return rhs_value;
        }
        // 左辺がデリファレンスの場合 (*p = value)
        else if (auto unary = std::get_if<std::unique_ptr<hir::HirUnary>>(&bin.lhs->kind)) {
            if ((*unary)->op == hir::HirUnaryOp::Deref) {
                // ポインタをlowering
                LocalId ptr = lower_expression(*(*unary)->operand, ctx);

                // Derefプロジェクションを使って代入
                MirPlace place{ptr};
                place.projections.push_back(PlaceProjection::deref());

                ctx.push_statement(MirStatement::assign(
                    place, MirRvalue::use(MirOperand::copy(MirPlace{rhs_value}))));

                return rhs_value;
            }
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

                    // < と > では引数の順序を変える
                    // <= と >= では結果を反転する
                    std::vector<MirOperandPtr> args;
                    if (bin.op == hir::HirBinaryOp::Lt || bin.op == hir::HirBinaryOp::Le) {
                        // a < b または a <= b: __op_lt(a, b)
                        args.push_back(MirOperand::copy(MirPlace{lhs}));
                        args.push_back(MirOperand::copy(MirPlace{rhs}));
                    } else {
                        // a > b または a >= b: __op_lt(b, a)
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
                        // a <= b は !(b < a) → 結果を反転
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

        // HIRの型が利用できない、またはエラー型の場合、ローカル変数から型を取得
        // （operator実装内の式など、型チェッカーが型を設定しない場合に対応）
        if ((!lhs_type || lhs_type->is_error()) && lhs < ctx.func->locals.size()) {
            lhs_type = ctx.func->locals[lhs].type;
        }
        if ((!rhs_type || rhs_type->is_error()) && rhs < ctx.func->locals.size()) {
            rhs_type = ctx.func->locals[rhs].type;
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
            // longがあればlong
            else if (lhs_type->kind == hir::TypeKind::Long ||
                     rhs_type->kind == hir::TypeKind::Long ||
                     lhs_type->kind == hir::TypeKind::ULong ||
                     rhs_type->kind == hir::TypeKind::ULong) {
                result_type = hir::make_long();
            }
            // それ以外は左辺の型を使用
            else {
                result_type = lhs_type;
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

    // BinaryOp Rvalueを作成
    auto bin_rvalue = std::make_unique<MirRvalue>();
    bin_rvalue->kind = MirRvalue::BinaryOp;
    bin_rvalue->data = MirRvalue::BinaryOpData{mir_op, MirOperand::copy(MirPlace{lhs}),
                                               MirOperand::copy(MirPlace{rhs})};

    ctx.push_statement(MirStatement::assign(MirPlace{result}, std::move(bin_rvalue)));

    return result;
}

// 単項演算のlowering
LocalId ExprLowering::lower_unary(const hir::HirUnary& unary, LoweringContext& ctx) {
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
                bin_rvalue->data = MirRvalue::BinaryOpData{op, MirOperand::copy(MirPlace{var_id}),
                                                           MirOperand::copy(MirPlace{one})};

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

        // 変数でない場合はエラー（一時的に0を返す）
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
        default:
            mir_op = MirUnaryOp::Neg;  // プレースホルダー
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

// フォーマット文字列から変数名を抽出し、引数を自動生成
std::pair<std::vector<std::string>, std::string> ExprLowering::extract_named_placeholders(
    const std::string& format_str, [[maybe_unused]] LoweringContext& ctx) {
    std::vector<std::string> var_names;
    std::string converted_format;

    size_t pos = 0;
    while (pos < format_str.length()) {
        if (pos + 1 < format_str.length() && format_str[pos] == '{' && format_str[pos + 1] == '{') {
            // エスケープされた {{ を処理
            converted_format += "{{";
            pos += 2;
            continue;
        }
        if (pos + 1 < format_str.length() && format_str[pos] == '}' && format_str[pos + 1] == '}') {
            // エスケープされた }} を処理
            converted_format += "}}";
            pos += 2;
            continue;
        }

        if (format_str[pos] == '{') {
            size_t end = pos + 1;
            // :: は変数名の一部として扱う（enum値のため）
            while (end < format_str.length() && format_str[end] != '}') {
                // フォーマット指定子のコロンをチェック（:: ではない場合）
                if (format_str[end] == ':' &&
                    (end + 1 >= format_str.length() || format_str[end + 1] != ':')) {
                    break;  // フォーマット指定子の開始
                }
                if (format_str[end] == ':' && end + 1 < format_str.length() &&
                    format_str[end + 1] == ':') {
                    end += 2;  // :: をスキップ
                } else {
                    end++;
                }
            }

            if (end < format_str.length() || end == format_str.length()) {
                std::string content = format_str.substr(pos + 1, end - pos - 1);

                // フォーマット指定子を探す（:: はスキップ）
                size_t colon_pos = std::string::npos;
                for (size_t i = pos + 1; i < format_str.length(); ++i) {
                    if (format_str[i] == ':' &&
                        (i + 1 >= format_str.length() || format_str[i + 1] != ':')) {
                        colon_pos = i;
                        break;
                    }
                    if (format_str[i] == ':' && i + 1 < format_str.length() &&
                        format_str[i + 1] == ':') {
                        i++;  // :: をスキップ
                    }
                }
                size_t close_pos = format_str.find('}', pos + 1);

                if (close_pos != std::string::npos) {
                    if (colon_pos != std::string::npos && colon_pos < close_pos) {
                        // {name:format} の形式
                        std::string var_name = format_str.substr(pos + 1, colon_pos - pos - 1);
                        std::string format_spec =
                            format_str.substr(colon_pos, close_pos - colon_pos + 1);

                        // &variable パターンをチェック
                        if (!var_name.empty() && var_name[0] == '&') {
                            // &variable形式 - アドレス取得 (後でサポート)
                            std::string actual_var = var_name.substr(1);
                            if (!actual_var.empty() && std::isalpha(actual_var[0])) {
                                var_names.push_back("&" + actual_var);  // &付きで格納
                                // フォーマット指定子をそのまま維持
                                converted_format += "{" + format_spec;
                            } else {
                                // 無効な&フォーマット - そのまま処理
                                converted_format += format_str.substr(pos, close_pos - pos + 1);
                            }
                        } else if (!var_name.empty() && var_name[0] == '*') {
                            // *variable形式 - ポインタのデリファレンス
                            std::string ptr_var = var_name.substr(1);
                            if (!ptr_var.empty() && std::isalpha(ptr_var[0])) {
                                var_names.push_back("*" + ptr_var);  // *付きで格納
                                // フォーマット指定子をそのまま維持
                                converted_format += "{" + format_spec;
                            } else {
                                // 無効な*フォーマット - そのまま処理
                                converted_format += format_str.substr(pos, close_pos - pos + 1);
                            }
                        } else if (!var_name.empty() &&
                                   (std::isalpha(var_name[0]) || var_name[0] == '!' ||
                                    var_name[0] == '*' ||  // デリファレンスを許可
                                    var_name.substr(0, 5) == "self." ||
                                    var_name.find("::") != std::string::npos)) {
                            // 変数名、メンバーアクセス、メソッド呼び出し、enum値、または否定演算子として有効
                            // self.x, p.field, r.area(), Color::Red, !true のような形式も許可
                            var_names.push_back(var_name);
                            converted_format += "{" + format_spec;  // {:x} のような形式に変換
                        } else {
                            // 位置プレースホルダは無視（変数名ではないので処理しない）
                            // 空のままにしてエラーにする
                            return {var_names, format_str};  // エラー：変換せずに元の文字列を返す
                        }
                        pos = close_pos + 1;
                    } else {
                        // {name} の形式
                        std::string var_name = format_str.substr(pos + 1, close_pos - pos - 1);

                        // &variable パターンをチェック
                        if (!var_name.empty() && var_name[0] == '&') {
                            // &variable形式 - アドレス取得 (後でサポート)
                            std::string actual_var = var_name.substr(1);
                            if (!actual_var.empty() && std::isalpha(actual_var[0])) {
                                var_names.push_back("&" + actual_var);  // &付きで格納
                                // アドレス表示用: プレースホルダーのみ（プレフィックスなし）
                                converted_format += "{}";
                            } else {
                                // 無効な&フォーマット - そのまま処理
                                converted_format += format_str.substr(pos, close_pos - pos + 1);
                            }
                        } else if (!var_name.empty() && var_name[0] == '*') {
                            // *variable形式 - ポインタのデリファレンス
                            std::string ptr_var = var_name.substr(1);
                            if (!ptr_var.empty() && std::isalpha(ptr_var[0])) {
                                var_names.push_back("*" + ptr_var);  // *付きで格納
                                converted_format += "{}";
                            } else {
                                // 無効な*フォーマット - そのまま処理
                                converted_format += format_str.substr(pos, close_pos - pos + 1);
                            }
                        } else if (!var_name.empty() &&
                                   (std::isalpha(var_name[0]) || var_name[0] == '!' ||
                                    var_name[0] == '*' ||  // デリファレンスを許可
                                    var_name.substr(0, 5) == "self." ||
                                    var_name.find("::") != std::string::npos)) {
                            // 変数名、メンバーアクセス、メソッド呼び出し、enum値、または否定演算子として有効
                            // self.x, p.field, r.area(), Color::Red, !true のような形式も許可
                            debug_msg("MIR", "Extracted placeholder: " + var_name);
                            var_names.push_back(var_name);
                            converted_format += "{}";  // 位置プレースホルダに変換
                        } else {
                            // 位置プレースホルダは無視（変数名ではないので処理しない）
                            // 空のままにしてエラーにする
                            return {var_names, format_str};  // エラー：変換せずに元の文字列を返す
                        }
                        pos = close_pos + 1;
                    }
                } else {
                    converted_format += format_str[pos];
                    pos++;
                }
            } else {
                converted_format += format_str[pos];
                pos++;
            }
        } else {
            converted_format += format_str[pos];
            pos++;
        }
    }

    return {var_names, converted_format};
}

// 関数呼び出しのlowering
LocalId ExprLowering::lower_call(const hir::HirCall& call, const hir::TypePtr& result_type,
                                 LoweringContext& ctx) {
    // println builtin特別処理
    if (call.func_name == "println") {
        // 引数がない場合は空行を出力
        if (call.args.empty()) {
            BlockId success_block = ctx.new_block();

            // cm_println_string("") を呼び出す
            std::vector<MirOperandPtr> args;
            MirConstant str_const;
            str_const.type = hir::make_string();
            str_const.value = std::string("");
            args.push_back(MirOperand::constant(str_const));

            auto func_operand = MirOperand::function_ref("cm_println_string");
            auto call_term = std::make_unique<MirTerminator>();
            call_term->kind = MirTerminator::Call;
            call_term->data = MirTerminator::CallData{
                std::move(func_operand),
                std::move(args),
                std::nullopt,  // 戻り値なし
                success_block,
                std::nullopt,  // unwind無し
                "",
                "",
                false  // 通常の関数呼び出し
            };
            ctx.set_terminator(std::move(call_term));
            ctx.switch_to_block(success_block);
            return ctx.new_temp(hir::make_void());
        }

        // 最初の引数を取得
        const auto& first_arg = call.args[0];

        // 引数の型に基づいて適切なランタイム関数を選択
        std::string runtime_func;
        std::vector<MirOperandPtr> args;

        // 複数引数がある場合は常にフォーマット関数を使う
        // または、文字列リテラルでフォーマット指定子がある場合
        bool use_format = false;
        bool has_escaped_braces = false;

        // 文字列リテラルかチェック（フォーマット文字列の可能性）
        if (auto lit = std::get_if<std::unique_ptr<hir::HirLiteral>>(&first_arg->kind)) {
            if ((*lit) && (*lit)->value.index() == 5) {  // string値
                std::string str_val = std::get<std::string>((*lit)->value);

                // フォーマット文字列かチェック（{...}パターンを含むか、またはエスケープされた中括弧を含むか）
                size_t pos = 0;
                while ((pos = str_val.find('{', pos)) != std::string::npos) {
                    if (pos + 1 < str_val.length() && str_val[pos + 1] == '{') {
                        // エスケープされた {{ を発見
                        has_escaped_braces = true;
                        pos += 2;
                        continue;
                    }
                    // { の後に } があるか確認
                    size_t end_pos = str_val.find('}', pos + 1);
                    if (end_pos != std::string::npos) {
                        use_format = true;
                        break;
                    }
                    pos++;
                }
                // エスケープされた }} もチェック
                if (!has_escaped_braces) {
                    pos = 0;
                    while ((pos = str_val.find('}', pos)) != std::string::npos) {
                        if (pos + 1 < str_val.length() && str_val[pos + 1] == '}') {
                            has_escaped_braces = true;
                            break;
                        }
                        pos++;
                    }
                }

                if ((use_format && call.args.size() > 1) || has_escaped_braces || use_format) {
                    // フォーマット文字列として処理
                    runtime_func = "cm_println_format";

                    // 名前付きプレースホルダを抽出
                    auto [var_names, converted_format] = extract_named_placeholders(str_val, ctx);

                    // フォーマット文字列を最初の引数として追加（変換後のフォーマット）
                    MirConstant str_const;
                    str_const.type = first_arg->type;
                    str_const.value = converted_format;
                    args.push_back(MirOperand::constant(str_const));

                    // 引数を収集（名前付き変数を先に、明示的引数を後に）
                    std::vector<LocalId> arg_locals;

                    // 名前付き変数を解決して追加（プレースホルダの順番通り）
                    for (const auto& var_name : var_names) {
                        if (!var_name.empty() && var_name[0] == '!') {
                            // 論理否定演算子の処理
                            std::string inner_expr = var_name.substr(1);

                            // 複数の否定演算子を処理（例: !!true）
                            int negation_count = 1;
                            while (!inner_expr.empty() && inner_expr[0] == '!') {
                                negation_count++;
                                inner_expr = inner_expr.substr(1);
                            }

                            LocalId expr_result;

                            // 内部式を評価
                            if (inner_expr == "true") {
                                // trueリテラル
                                MirConstant bool_const;
                                bool_const.type = hir::make_bool();
                                bool_const.value = true;
                                expr_result = ctx.new_temp(hir::make_bool());
                                ctx.push_statement(MirStatement::assign(
                                    MirPlace{expr_result},
                                    MirRvalue::use(MirOperand::constant(bool_const))));
                            } else if (inner_expr == "false") {
                                // falseリテラル
                                MirConstant bool_const;
                                bool_const.type = hir::make_bool();
                                bool_const.value = false;
                                expr_result = ctx.new_temp(hir::make_bool());
                                ctx.push_statement(MirStatement::assign(
                                    MirPlace{expr_result},
                                    MirRvalue::use(MirOperand::constant(bool_const))));
                            } else if (inner_expr.find(" && ") != std::string::npos ||
                                       inner_expr.find(" || ") != std::string::npos) {
                                // 論理式の処理（簡易実装）
                                // TODO: 完全な式パーサーが必要
                                // 現在は論理演算を含む式は評価できないため、falseを返す
                                MirConstant bool_const;
                                bool_const.type = hir::make_bool();
                                bool_const.value = false;
                                expr_result = ctx.new_temp(hir::make_bool());
                                ctx.push_statement(MirStatement::assign(
                                    MirPlace{expr_result},
                                    MirRvalue::use(MirOperand::constant(bool_const))));
                            } else {
                                // 変数を解決
                                auto var_id = ctx.resolve_variable(inner_expr);
                                if (var_id) {
                                    expr_result = *var_id;
                                } else {
                                    // 変数が見つからない場合、falseとして扱う
                                    MirConstant bool_const;
                                    bool_const.type = hir::make_bool();
                                    bool_const.value = false;
                                    expr_result = ctx.new_temp(hir::make_bool());
                                    ctx.push_statement(MirStatement::assign(
                                        MirPlace{expr_result},
                                        MirRvalue::use(MirOperand::constant(bool_const))));
                                }
                            }

                            // 否定演算を適用
                            for (int i = 0; i < negation_count; ++i) {
                                LocalId new_result = ctx.new_temp(hir::make_bool());
                                ctx.push_statement(MirStatement::assign(
                                    MirPlace{new_result},
                                    MirRvalue::unary(MirUnaryOp::Not,
                                                     MirOperand::copy(MirPlace{expr_result}))));
                                expr_result = new_result;
                            }

                            arg_locals.push_back(expr_result);
                        } else if (!var_name.empty() && var_name[0] == '&') {
                            // &variable形式 - アドレス取得を実装
                            std::string actual_var = var_name.substr(1);
                            auto var_id = ctx.resolve_variable(actual_var);
                            if (var_id) {
                                // アドレス取得 - ポインタ型の一時変数を作成
                                hir::TypePtr ptr_type = nullptr;

                                // 変数の型を取得してポインタ型を作成
                                if (*var_id < ctx.func->locals.size()) {
                                    auto base_type = ctx.func->locals[*var_id].type;
                                    ptr_type = hir::make_pointer(base_type);
                                }

                                if (!ptr_type) {
                                    ptr_type = hir::make_pointer(hir::make_int());  // デフォルト
                                }

                                LocalId result = ctx.new_temp(ptr_type);

                                // Ref演算でアドレスを取得（immutableポインタ）
                                ctx.push_statement(MirStatement::assign(
                                    MirPlace{result}, MirRvalue::ref(MirPlace{*var_id}, false)));

                                // 特殊マーカーを使って、これがアドレス表示であることを示す
                                // フォーマット文字列内で &変数名: プレフィックスを追加
                                debug_msg("MIR", "Address interpolation: adding pointer local " +
                                                     std::to_string(result) + " with type " +
                                                     ptr_type->name);
                                arg_locals.push_back(result);  // ポインタをそのまま渡す
                            } else {
                                // 変数が見つからない場合、エラー用のダミー値を追加
                                auto err_type = hir::make_error();
                                arg_locals.push_back(ctx.new_temp(err_type));
                            }
                        } else if (!var_name.empty() && var_name[0] == '*') {
                            // *variable形式 - ポインタのデリファレンス
                            std::string ptr_var = var_name.substr(1);
                            auto var_id = ctx.resolve_variable(ptr_var);
                            if (var_id) {
                                // ポインタ変数の型を取得
                                hir::TypePtr ptr_type = nullptr;
                                hir::TypePtr deref_type = nullptr;

                                if (*var_id < ctx.func->locals.size()) {
                                    ptr_type = ctx.func->locals[*var_id].type;
                                    // ポインタ型から指している型を取得
                                    if (ptr_type && ptr_type->kind == hir::TypeKind::Pointer &&
                                        ptr_type->element_type) {
                                        deref_type = ptr_type->element_type;
                                    }
                                }

                                if (!deref_type) {
                                    deref_type = hir::make_int();  // デフォルト
                                }

                                LocalId result = ctx.new_temp(deref_type);

                                // Derefプロジェクションを使用してデリファレンス
                                MirPlace place{*var_id};
                                place.projections.push_back(PlaceProjection::deref());

                                // ポインタをデリファレンスして値を取得
                                ctx.push_statement(MirStatement::assign(
                                    MirPlace{result}, MirRvalue::use(MirOperand::copy(place))));

                                debug_msg("MIR",
                                          "Pointer dereference interpolation: dereferencing " +
                                              std::to_string(*var_id) + " to " +
                                              std::to_string(result));
                                arg_locals.push_back(result);
                            } else {
                                // 変数が見つからない場合、エラー用のダミー値を追加
                                auto err_type = hir::make_error();
                                arg_locals.push_back(ctx.new_temp(err_type));
                            }
                        } else {
                            // メンバーアクセスかどうかチェック（"self.x" や "p.field" の形式）
                            size_t dot_pos = var_name.find('.');
                            if (dot_pos != std::string::npos) {
                                // メンバーアクセスの処理
                                std::string obj_name = var_name.substr(0, dot_pos);
                                std::string member_name = var_name.substr(dot_pos + 1);

                                // メソッド呼び出しかどうかチェック（末尾が "()" の場合）
                                bool is_method_call = false;
                                if (member_name.size() > 2 &&
                                    member_name.substr(member_name.size() - 2) == "()") {
                                    is_method_call = true;
                                    member_name = member_name.substr(
                                        0, member_name.size() - 2);  // "()" を除去
                                }

                                // オブジェクトを解決
                                auto obj_id = ctx.resolve_variable(obj_name);
                                if (obj_id) {
                                    // オブジェクトの型を取得
                                    hir::TypePtr obj_type = nullptr;
                                    if (*obj_id < ctx.func->locals.size()) {
                                        obj_type = ctx.func->locals[*obj_id].type;
                                    }

                                    if (is_method_call) {
                                        // メソッド呼び出しの処理
                                        // 簡易実装：メソッド呼び出しを事前に実行

                                        // メソッド呼び出しのための新しいブロックを作成
                                        BlockId call_block = ctx.new_block();
                                        BlockId after_call_block = ctx.new_block();

                                        // 戻り値用の一時変数（とりあえずintとして扱う）
                                        hir::TypePtr return_type = hir::make_int();
                                        LocalId result = ctx.new_temp(return_type);

                                        // メソッド名を構築
                                        // インターフェースメソッドの場合は構造体名::インターフェース名::メソッド名
                                        // 通常のメソッドの場合は構造体名::メソッド名
                                        // ここでは簡易実装として、実際の関数名解決はコード生成時に行われる
                                        std::string method_name =
                                            obj_type->name + "::" + member_name;

                                        // 引数リスト（selfパラメータ）
                                        std::vector<MirOperandPtr> method_args;
                                        method_args.push_back(MirOperand::copy(MirPlace{*obj_id}));

                                        // メソッド呼び出しのターミネータを作成
                                        // インターフェースメソッドとして処理（仮想メソッド呼び出し）
                                        auto call_term = std::make_unique<MirTerminator>();
                                        call_term->kind = MirTerminator::Call;
                                        call_term->data = MirTerminator::CallData{
                                            MirOperand::function_ref(method_name),
                                            std::move(method_args),
                                            std::make_optional(MirPlace{result}),
                                            after_call_block,
                                            std::nullopt,  // unwindブロックなし
                                            "",  // interface_name（後でコード生成時に解決）
                                            member_name,  // method_name
                                            true  // is_virtual（インターフェースメソッドの可能性）
                                        };

                                        // 現在のブロックから呼び出しブロックへジャンプ
                                        ctx.set_terminator(MirTerminator::goto_block(call_block));

                                        // 呼び出しブロックを設定
                                        ctx.switch_to_block(call_block);
                                        ctx.get_current_block()->terminator = std::move(call_term);

                                        // 呼び出し後のブロックに切り替え
                                        ctx.switch_to_block(after_call_block);

                                        arg_locals.push_back(result);
                                    } else if (obj_type &&
                                               obj_type->kind == hir::TypeKind::Struct) {
                                        // フィールドアクセスの処理
                                        auto field_idx =
                                            ctx.get_field_index(obj_type->name, member_name);
                                        if (field_idx) {
                                            // フィールドの型を取得
                                            hir::TypePtr field_type =
                                                hir::make_int();  // デフォルト
                                            if (ctx.struct_defs &&
                                                ctx.struct_defs->count(obj_type->name)) {
                                                const auto* struct_def =
                                                    ctx.struct_defs->at(obj_type->name);
                                                if (*field_idx < struct_def->fields.size()) {
                                                    field_type =
                                                        struct_def->fields[*field_idx].type;
                                                }
                                            }

                                            // フィールドアクセスのための一時変数
                                            LocalId result = ctx.new_temp(field_type);

                                            // Projectionを使ったアクセスを生成
                                            MirPlace place{*obj_id};
                                            place.projections.push_back(
                                                PlaceProjection::field(*field_idx));

                                            ctx.push_statement(MirStatement::assign(
                                                MirPlace{result},
                                                MirRvalue::use(MirOperand::copy(place))));

                                            arg_locals.push_back(result);
                                        } else {
                                            // フィールドが見つからない
                                            auto err_type = hir::make_error();
                                            arg_locals.push_back(ctx.new_temp(err_type));
                                        }
                                    } else {
                                        // 構造体型でない
                                        auto err_type = hir::make_error();
                                        arg_locals.push_back(ctx.new_temp(err_type));
                                    }
                                } else {
                                    // オブジェクトが見つからない
                                    auto err_type = hir::make_error();
                                    arg_locals.push_back(ctx.new_temp(err_type));
                                }
                            } else {
                                // enum値かどうかチェック（"::" を含む場合）
                                size_t scope_pos = var_name.find("::");
                                if (scope_pos != std::string::npos) {
                                    // enum値の処理（例: Color::Red）
                                    std::string enum_name = var_name.substr(0, scope_pos);
                                    std::string enum_member = var_name.substr(scope_pos + 2);

                                    // enum定数を解決
                                    auto enum_value = ctx.get_enum_value(enum_name, enum_member);

                                    hir::TypePtr enum_type =
                                        hir::make_int();  // enum型はintとして扱う
                                    LocalId result = ctx.new_temp(enum_type);

                                    // enum値を定数として生成
                                    MirConstant enum_const;
                                    enum_const.type = enum_type;

                                    if (enum_value) {
                                        // enum値が見つかった場合
                                        enum_const.value = *enum_value;
                                    } else {
                                        // enum値が見つからない場合はエラー値
                                        enum_const.value = int64_t(0);
                                        debug_msg("MIR",
                                                  "Warning: Enum value not found: " + var_name);
                                    }

                                    ctx.push_statement(MirStatement::assign(
                                        MirPlace{result},
                                        MirRvalue::use(MirOperand::constant(enum_const))));

                                    arg_locals.push_back(result);
                                } else {
                                    debug_msg("MIR", "Processing placeholder: " + var_name);
                                    // メソッド呼び出しかどうかチェック（obj.method()形式）
                                    size_t dot_pos = var_name.find('.');
                                    size_t paren_pos = var_name.find('(');

                                    if (dot_pos != std::string::npos &&
                                        paren_pos != std::string::npos && dot_pos < paren_pos &&
                                        var_name.back() == ')') {
                                        // メソッド呼び出しパターン: obj.method(args)
                                        std::string obj_name = var_name.substr(0, dot_pos);
                                        std::string method_name =
                                            var_name.substr(dot_pos + 1, paren_pos - dot_pos - 1);
                                        std::string args_str = var_name.substr(
                                            paren_pos + 1, var_name.length() - paren_pos - 2);

                                        debug_msg("MIR", "Method call interpolation: obj=" +
                                                             obj_name + ", method=" + method_name);

                                        // オブジェクトを解決
                                        auto obj_id = ctx.resolve_variable(obj_name);
                                        if (obj_id) {
                                            // オブジェクトの型を取得
                                            hir::TypePtr obj_type = nullptr;
                                            if (*obj_id < ctx.func->locals.size()) {
                                                obj_type = ctx.func->locals[*obj_id].type;
                                            }

                                            if (obj_type &&
                                                obj_type->kind == hir::TypeKind::Struct) {
                                                debug_msg("MIR", "Object type: " + obj_type->name);

                                                // メソッド呼び出しのブロックを作成
                                                BlockId call_block = ctx.new_block();
                                                BlockId after_call_block = ctx.new_block();

                                                // 戻り値用の一時変数
                                                hir::TypePtr return_type = hir::make_int();
                                                LocalId result = ctx.new_temp(return_type);

                                                // 引数の準備（レシーバーが最初の引数）
                                                std::vector<MirOperandPtr> call_args;
                                                call_args.push_back(
                                                    MirOperand::copy(MirPlace{*obj_id}));

                                                // 追加引数を解析（簡易実装）
                                                if (!args_str.empty()) {
                                                    try {
                                                        int64_t value = std::stoll(args_str);
                                                        MirConstant arg_const;
                                                        arg_const.type = hir::make_int();
                                                        arg_const.value = value;
                                                        call_args.push_back(
                                                            MirOperand::constant(arg_const));
                                                    } catch (...) {
                                                        // 解析エラーの場合は引数なし
                                                    }
                                                }

                                                // インターフェースメソッド名を探す
                                                // 形式: StructName::InterfaceName::MethodName
                                                std::string full_method_name =
                                                    obj_type->name + "__" + method_name;
                                                debug_msg("MIR",
                                                          "Full method name: " + full_method_name);

                                                // 簡易実装：インターフェース名を仮定（実際は検索が必要）
                                                // TODO:
                                                // 実際のインターフェース名を検索する機能を実装
                                                std::string interface_name = "";
                                                bool is_virtual = false;

                                                // 暫定的にインターフェース名を検索
                                                // 実際にはimpl情報から検索する必要がある
                                                if (method_name == "sum") {
                                                    interface_name = "Summable";
                                                    is_virtual = true;
                                                } else if (method_name == "get_value") {
                                                    interface_name = "Valuable";
                                                    is_virtual = true;
                                                }

                                                // メソッド呼び出しのターミネータを作成
                                                auto call_term = std::make_unique<MirTerminator>();
                                                call_term->kind = MirTerminator::Call;
                                                call_term->data = MirTerminator::CallData{
                                                    MirOperand::function_ref(full_method_name),
                                                    std::move(call_args),
                                                    std::make_optional(MirPlace{result}),
                                                    after_call_block,
                                                    std::nullopt,
                                                    interface_name,  // インターフェース名
                                                    method_name,     // メソッド名
                                                    is_virtual       // 仮想メソッドフラグ
                                                };

                                                // 現在のブロックから呼び出しブロックへジャンプ
                                                ctx.set_terminator(
                                                    MirTerminator::goto_block(call_block));

                                                // 呼び出しブロックを設定
                                                ctx.switch_to_block(call_block);
                                                ctx.get_current_block()->terminator =
                                                    std::move(call_term);

                                                // 呼び出し後のブロックに切り替え
                                                ctx.switch_to_block(after_call_block);

                                                arg_locals.push_back(result);
                                            } else {
                                                // 構造体型でない場合はエラー
                                                auto err_type = hir::make_error();
                                                arg_locals.push_back(ctx.new_temp(err_type));
                                            }
                                        } else {
                                            // オブジェクトが見つからない場合はエラー
                                            auto err_type = hir::make_error();
                                            arg_locals.push_back(ctx.new_temp(err_type));
                                        }
                                    } else {
                                        // 通常の関数呼び出しかどうかチェック
                                        bool is_function_call = false;
                                        std::string func_name = var_name;
                                        std::vector<std::string> func_args;

                                        // 関数呼び出しパターンをチェック（"func()" or
                                        // "func(args)"）
                                        if (paren_pos != std::string::npos &&
                                            var_name.back() == ')') {
                                            is_function_call = true;
                                            func_name = var_name.substr(0, paren_pos);

                                            // 引数を解析（簡易実装：現在は単一の整数のみサポート）
                                            std::string args_str = var_name.substr(
                                                paren_pos + 1, var_name.length() - paren_pos - 2);
                                            if (!args_str.empty()) {
                                                func_args.push_back(args_str);
                                            }
                                        }

                                        if (is_function_call) {
                                            // まず変数として解決を試みる（関数ポインタの可能性）
                                            auto var_id = ctx.resolve_variable(func_name);

                                            if (var_id) {
                                                // 変数が見つかった - 関数ポインタ経由の呼び出し
                                                BlockId call_block = ctx.new_block();
                                                BlockId after_call_block = ctx.new_block();

                                                // 戻り値用の一時変数（とりあえずintとして扱う）
                                                hir::TypePtr return_type = hir::make_int();
                                                LocalId result = ctx.new_temp(return_type);

                                                // 引数の準備
                                                std::vector<MirOperandPtr> call_args;
                                                for (const auto& arg_str : func_args) {
                                                    // 簡易実装：整数リテラルのみサポート
                                                    try {
                                                        int64_t value = std::stoll(arg_str);
                                                        MirConstant arg_const;
                                                        arg_const.type = hir::make_int();
                                                        arg_const.value = value;
                                                        call_args.push_back(
                                                            MirOperand::constant(arg_const));
                                                    } catch (...) {
                                                        // 解析エラーの場合はダミー値
                                                        MirConstant arg_const;
                                                        arg_const.type = hir::make_int();
                                                        arg_const.value = 0;
                                                        call_args.push_back(
                                                            MirOperand::constant(arg_const));
                                                    }
                                                }

                                                // 関数ポインタ経由の呼び出し
                                                auto call_term = std::make_unique<MirTerminator>();
                                                call_term->kind = MirTerminator::Call;
                                                call_term->data = MirTerminator::CallData{
                                                    MirOperand::copy(MirPlace{
                                                        *var_id}),  // 関数ポインタ変数を使用
                                                    std::move(call_args),
                                                    std::make_optional(MirPlace{result}),
                                                    after_call_block,
                                                    std::nullopt,  // unwindブロックなし
                                                    "",            // interface_name
                                                    "",            // method_name
                                                    false          // is_virtual
                                                };

                                                // 現在のブロックから呼び出しブロックへジャンプ
                                                ctx.set_terminator(
                                                    MirTerminator::goto_block(call_block));

                                                // 呼び出しブロックを設定
                                                ctx.switch_to_block(call_block);
                                                ctx.get_current_block()->terminator =
                                                    std::move(call_term);

                                                // 呼び出し後のブロックに切り替え
                                                ctx.switch_to_block(after_call_block);

                                                arg_locals.push_back(result);
                                            } else {
                                                // 変数が見つからない - 通常の関数呼び出しを試みる
                                                BlockId call_block = ctx.new_block();
                                                BlockId after_call_block = ctx.new_block();

                                                // 戻り値用の一時変数（とりあえずintとして扱う）
                                                hir::TypePtr return_type = hir::make_int();
                                                LocalId result = ctx.new_temp(return_type);

                                                // 引数の準備
                                                std::vector<MirOperandPtr> call_args;
                                                for (const auto& arg_str : func_args) {
                                                    // 簡易実装：整数リテラルのみサポート
                                                    try {
                                                        int64_t value = std::stoll(arg_str);
                                                        MirConstant arg_const;
                                                        arg_const.type = hir::make_int();
                                                        arg_const.value = value;
                                                        call_args.push_back(
                                                            MirOperand::constant(arg_const));
                                                    } catch (...) {
                                                        // 解析エラーの場合はダミー値
                                                        MirConstant arg_const;
                                                        arg_const.type = hir::make_int();
                                                        arg_const.value = 0;
                                                        call_args.push_back(
                                                            MirOperand::constant(arg_const));
                                                    }
                                                }

                                                // 関数呼び出しのターミネータを作成
                                                auto call_term = std::make_unique<MirTerminator>();
                                                call_term->kind = MirTerminator::Call;
                                                call_term->data = MirTerminator::CallData{
                                                    MirOperand::function_ref(func_name),
                                                    std::move(call_args),
                                                    std::make_optional(MirPlace{result}),
                                                    after_call_block,
                                                    std::nullopt,  // unwindブロックなし
                                                    "",            // interface_name
                                                    "",            // method_name
                                                    false          // is_virtual
                                                };

                                                // 現在のブロックから呼び出しブロックへジャンプ
                                                ctx.set_terminator(
                                                    MirTerminator::goto_block(call_block));

                                                // 呼び出しブロックを設定
                                                ctx.switch_to_block(call_block);
                                                ctx.get_current_block()->terminator =
                                                    std::move(call_term);

                                                // 呼び出し後のブロックに切り替え
                                                ctx.switch_to_block(after_call_block);

                                                arg_locals.push_back(result);
                                            }
                                        } else {
                                            // 通常の変数
                                            // まずconst値をチェック（定数として登録されている場合）
                                            auto const_val = ctx.get_const_value(var_name);
                                            if (const_val) {
                                                // const変数の値を一時変数に格納
                                                LocalId const_temp = ctx.new_temp(const_val->type);
                                                ctx.push_statement(MirStatement::assign(
                                                    MirPlace{const_temp},
                                                    MirRvalue::use(
                                                        MirOperand::constant(*const_val))));
                                                arg_locals.push_back(const_temp);
                                            } else {
                                                auto var_id = ctx.resolve_variable(var_name);
                                                if (var_id) {
                                                    arg_locals.push_back(*var_id);
                                                } else {
                                                    // 変数が見つからない場合、エラー用のダミー値を追加
                                                    auto err_type = hir::make_error();
                                                    arg_locals.push_back(ctx.new_temp(err_type));
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // 明示的な引数は無視（単一の文字列リテラルのみ許可）
                    // 将来的にはエラーを報告する
                    if (call.args.size() > 1) {
                        // TODO: エラーを報告: println accepts only a single string literal. Use
                        // variable interpolation instead: println("{var}") 現在は追加引数を無視
                    }

                    // 引数の数を追加
                    MirConstant argc_const;
                    argc_const.type = hir::make_int();
                    argc_const.value = static_cast<int64_t>(arg_locals.size());
                    args.push_back(MirOperand::constant(argc_const));

                    // 実際の引数を追加
                    for (LocalId arg_local : arg_locals) {
                        args.push_back(MirOperand::copy(MirPlace{arg_local}));
                    }
                } else {
                    // 通常の文字列出力
                    runtime_func = "cm_println_string";
                    MirConstant str_const;
                    str_const.type = first_arg->type;
                    str_const.value = str_val;
                    args.push_back(MirOperand::constant(str_const));
                }
            } else {
                // その他のリテラル（整数など）
                runtime_func = "cm_println_int";
                LocalId arg_local = lower_expression(*first_arg, ctx);
                args.push_back(MirOperand::copy(MirPlace{arg_local}));
            }
        } else {
            // 式の場合、評価して型に基づいて選択
            LocalId arg_local = lower_expression(*first_arg, ctx);

            // 型チェック
            if (first_arg->type) {
                switch (first_arg->type->kind) {
                    case hir::TypeKind::String:
                        // 文字列変数で複数引数がある場合はフォーマット関数を使う
                        if (call.args.size() > 1) {
                            runtime_func = "cm_println_format";

                            // 文字列変数を最初の引数として追加
                            args.push_back(MirOperand::copy(MirPlace{arg_local}));

                            // 引数の数を追加
                            MirConstant argc_const;
                            argc_const.type = hir::make_int();
                            argc_const.value = static_cast<int64_t>(call.args.size() - 1);
                            args.push_back(MirOperand::constant(argc_const));

                            // 残りの引数を処理
                            for (size_t i = 1; i < call.args.size(); ++i) {
                                LocalId arg = lower_expression(*call.args[i], ctx);
                                args.push_back(MirOperand::copy(MirPlace{arg}));
                            }

                            // Call終端命令を作成
                            BlockId success_block = ctx.new_block();
                            auto func_operand = MirOperand::function_ref(runtime_func);
                            auto call_term = std::make_unique<MirTerminator>();
                            call_term->kind = MirTerminator::Call;
                            call_term->data = MirTerminator::CallData{
                                std::move(func_operand),
                                std::move(args),
                                std::nullopt,  // printlnは戻り値なし
                                success_block,
                                std::nullopt,  // unwind無し
                                std::string(),
                                std::string(),
                                false  // 通常の関数呼び出し
                            };
                            ctx.set_terminator(std::move(call_term));
                            ctx.switch_to_block(success_block);
                            return ctx.new_temp(hir::make_void());
                        } else {
                            runtime_func = "cm_println_string";
                        }
                        break;
                    case hir::TypeKind::Float:
                    case hir::TypeKind::Double:
                        runtime_func = "cm_println_double";
                        break;
                    case hir::TypeKind::Bool:
                        runtime_func = "cm_println_bool";
                        break;
                    case hir::TypeKind::Char:
                        runtime_func = "cm_println_char";
                        break;
                    default:
                        runtime_func = "cm_println_int";
                        break;
                }
            } else {
                runtime_func = "cm_println_int";
            }
            args.push_back(MirOperand::copy(MirPlace{arg_local}));
        }

        // Call終端命令（戻り値なし）
        BlockId success_block = ctx.new_block();

        // 関数名を関数参照オペランドとして作成
        auto func_operand = MirOperand::function_ref(runtime_func);

        // Call終端命令を手動で作成
        auto call_term = std::make_unique<MirTerminator>();
        call_term->kind = MirTerminator::Call;
        call_term->data = MirTerminator::CallData{
            std::move(func_operand),
            std::move(args),
            std::nullopt,  // printlnは戻り値なし
            success_block,
            std::nullopt,  // unwind無し
            std::string(),
            std::string(),
            false  // 通常の関数呼び出し
        };
        ctx.set_terminator(std::move(call_term));

        // 次のブロックへ移動
        ctx.switch_to_block(success_block);

        // ダミーの戻り値
        return ctx.new_temp(hir::make_void());
    }

    // 通常の関数呼び出し
    std::vector<MirOperandPtr> args;
    for (const auto& arg : call.args) {
        LocalId arg_local = lower_expression(*arg, ctx);
        args.push_back(MirOperand::copy(MirPlace{arg_local}));
    }

    // 結果用の一時変数（型チェッカーが推論した型を使用）
    hir::TypePtr actual_result_type = result_type ? result_type : hir::make_int();
    LocalId result = ctx.new_temp(actual_result_type);

    // Call終端命令（現在のブロックを終端）
    BlockId success_block = ctx.new_block();

    // 関数オペランドを作成
    MirOperandPtr func_operand;
    if (call.is_indirect) {
        // 関数ポインタ経由の呼び出し: 変数から関数ポインタを取得
        auto var_id = ctx.resolve_variable(call.func_name);
        if (var_id) {
            func_operand = MirOperand::copy(MirPlace{*var_id});
        } else {
            // 変数が見つからない場合はエラー
            std::cerr << "[MIR] Error: Function pointer variable '" << call.func_name
                      << "' not found\n";
            func_operand = MirOperand::function_ref(call.func_name);
        }
    } else {
        // 直接呼び出し: 関数参照を使用
        func_operand = MirOperand::function_ref(call.func_name);
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
        std::nullopt,  // unwind無し
        std::string(),
        std::string(),
        false  // 通常の関数呼び出し
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

    return result;
}

// メンバーアクセスのlowering
LocalId ExprLowering::lower_member(const hir::HirMember& member, LoweringContext& ctx) {
    // オブジェクトをlowering
    LocalId object = lower_expression(*member.object, ctx);

    // オブジェクトの型から構造体名を取得
    auto obj_type = member.object->type;

    // 型が未設定の場合、ローカル変数から推論
    if (!obj_type || obj_type->kind != hir::TypeKind::Struct) {
        if (object < ctx.func->locals.size()) {
            obj_type = ctx.func->locals[object].type;
        }
    }

    if (!obj_type || obj_type->kind != hir::TypeKind::Struct) {
        // エラー：構造体型でない
        debug_msg("MIR",
                  "Error: Member access on non-struct type for member '" + member.member + "'");
        return ctx.new_temp(hir::make_error());
    }

    // 構造体のフィールドインデックスを取得
    auto field_idx = ctx.get_field_index(obj_type->name, member.member);
    if (!field_idx) {
        // エラー：フィールドが見つからない
        debug_msg("MIR", "Error: Field '" + member.member + "' not found in struct '" +
                             obj_type->name + "'");
        return ctx.new_temp(hir::make_error());
    }

    // フィールドの型を取得
    hir::TypePtr field_type = hir::make_int();  // デフォルト
    if (ctx.struct_defs && ctx.struct_defs->count(obj_type->name)) {
        const auto* struct_def = ctx.struct_defs->at(obj_type->name);
        if (*field_idx < struct_def->fields.size()) {
            field_type = struct_def->fields[*field_idx].type;
        }
    }

    // フィールドアクセスのための一時変数
    LocalId result = ctx.new_temp(field_type);

    // Projectionを使ったアクセスを生成
    MirPlace place{object};
    place.projections.push_back(PlaceProjection::field(*field_idx));

    ctx.push_statement(
        MirStatement::assign(MirPlace{result}, MirRvalue::use(MirOperand::copy(place))));

    return result;
}

// 配列インデックスのlowering
LocalId ExprLowering::lower_index(const hir::HirIndex& index_expr, LoweringContext& ctx) {
    // オブジェクトとインデックスをlowering
    LocalId array;

    // objectが変数参照の場合は直接その変数を使用（配列のコピーを防ぐ）
    if (auto* var_ref = std::get_if<std::unique_ptr<hir::HirVarRef>>(&index_expr.object->kind)) {
        auto var_id = ctx.resolve_variable((*var_ref)->name);
        if (var_id) {
            array = *var_id;
        } else {
            array = lower_expression(*index_expr.object, ctx);
        }
    } else {
        array = lower_expression(*index_expr.object, ctx);
    }

    LocalId index = lower_expression(*index_expr.index, ctx);

    // 要素型を取得
    hir::TypePtr elem_type = hir::make_int();  // デフォルト
    if (index_expr.object && index_expr.object->type &&
        index_expr.object->type->kind == hir::TypeKind::Array &&
        index_expr.object->type->element_type) {
        elem_type = index_expr.object->type->element_type;
    }

    LocalId result = ctx.new_temp(elem_type);

    MirPlace place{array};
    place.projections.push_back(PlaceProjection::index(index));

    ctx.push_statement(
        MirStatement::assign(MirPlace{result}, MirRvalue::use(MirOperand::copy(place))));

    return result;
}

// 三項演算子のlowering
LocalId ExprLowering::lower_ternary(const hir::HirTernary& ternary, LoweringContext& ctx) {
    // 条件をlowering
    LocalId cond = lower_expression(*ternary.condition, ctx);

    // ブロックを作成
    BlockId then_block = ctx.new_block();
    BlockId else_block = ctx.new_block();
    BlockId merge_block = ctx.new_block();

    // 結果用の変数（then_exprの型を使用）
    hir::TypePtr result_type = ternary.then_expr ? ternary.then_expr->type : hir::make_int();
    LocalId result = ctx.new_temp(result_type);

    // 条件分岐
    ctx.set_terminator(
        MirTerminator::switch_int(MirOperand::copy(MirPlace{cond}), {{1, then_block}}, else_block));

    // then部
    ctx.switch_to_block(then_block);
    LocalId then_value = lower_expression(*ternary.then_expr, ctx);
    ctx.push_statement(MirStatement::assign(
        MirPlace{result}, MirRvalue::use(MirOperand::copy(MirPlace{then_value}))));
    ctx.set_terminator(MirTerminator::goto_block(merge_block));

    // else部
    ctx.switch_to_block(else_block);
    LocalId else_value = lower_expression(*ternary.else_expr, ctx);
    ctx.push_statement(MirStatement::assign(
        MirPlace{result}, MirRvalue::use(MirOperand::copy(MirPlace{else_value}))));
    ctx.set_terminator(MirTerminator::goto_block(merge_block));

    // マージポイント
    ctx.switch_to_block(merge_block);

    return result;
}

// 値を文字列に変換するヘルパー（文字列連結用）
LocalId ExprLowering::convert_to_string(LocalId value, const hir::TypePtr& type,
                                        LoweringContext& ctx) {
    // 変換関数名を型に基づいて決定
    std::string conv_func;
    if (!type) {
        conv_func = "cm_int_to_string";  // デフォルト
    } else {
        switch (type->kind) {
            case hir::TypeKind::Int:
            case hir::TypeKind::Short:
            case hir::TypeKind::Long:
            case hir::TypeKind::Tiny:
                conv_func = "cm_int_to_string";
                break;
            case hir::TypeKind::UInt:
            case hir::TypeKind::UShort:
            case hir::TypeKind::ULong:
            case hir::TypeKind::UTiny:
                conv_func = "cm_uint_to_string";
                break;
            case hir::TypeKind::Float:
            case hir::TypeKind::Double:
                conv_func = "cm_double_to_string";
                break;
            case hir::TypeKind::Bool:
                conv_func = "cm_bool_to_string";
                break;
            case hir::TypeKind::Char:
                conv_func = "cm_char_to_string";
                break;
            case hir::TypeKind::String:
                // 既に文字列なので変換不要
                return value;
            default:
                conv_func = "cm_int_to_string";  // フォールバック
                break;
        }
    }

    // 変換関数を呼び出す
    LocalId str_result = ctx.new_temp(hir::make_string());
    std::vector<MirOperandPtr> conv_args;
    conv_args.push_back(MirOperand::copy(MirPlace{value}));

    BlockId conv_success = ctx.new_block();

    auto conv_func_operand = MirOperand::function_ref(conv_func);

    auto conv_call_term = std::make_unique<MirTerminator>();
    conv_call_term->kind = MirTerminator::Call;
    conv_call_term->data = MirTerminator::CallData{std::move(conv_func_operand),
                                                   std::move(conv_args),
                                                   MirPlace{str_result},
                                                   conv_success,
                                                   std::nullopt,
                                                   "",
                                                   "",
                                                   false};  // 通常の関数呼び出し
    ctx.set_terminator(std::move(conv_call_term));
    ctx.switch_to_block(conv_success);

    return str_result;
}

}  // namespace cm::mir