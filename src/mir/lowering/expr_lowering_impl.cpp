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
                    if (!content.empty() && std::isalpha(content[0])) {
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
                auto var_id = ctx.resolve_variable(var_name);
                if (var_id) {
                    arg_locals.push_back(*var_id);
                } else {
                    // 変数が見つからない場合、エラー用のダミー値
                    auto err_type = hir::make_error();
                    arg_locals.push_back(ctx.new_temp(err_type));
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
                std::move(func_operand), std::move(args), MirPlace{result},  // 戻り値を格納
                success_block,
                std::nullopt  // unwind無し
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
LocalId ExprLowering::lower_var_ref(const hir::HirVarRef& var, LoweringContext& ctx) {
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
            // 配列オブジェクトと添え字を解決
            LocalId array = lower_expression(*(*index)->object, ctx);
            LocalId idx = lower_expression(*(*index)->index, ctx);

            // Projectionを使ったアクセスを生成
            MirPlace place{array};
            place.projections.push_back(PlaceProjection::index(idx));

            // 配列要素に代入
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
            concat_call_term->data =
                MirTerminator::CallData{std::move(concat_func_operand), std::move(args),
                                        MirPlace{result}, concat_success, std::nullopt};
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

    // 結果用の一時変数
    LocalId result = ctx.new_temp(hir::make_int());  // TODO: 実際の結果型を推論

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
    hir::TypePtr result_type =
        (unary.op == hir::HirUnaryOp::Not) ? hir::make_bool() : unary.operand->type;
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
    const std::string& format_str, LoweringContext& ctx) {
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
            while (end < format_str.length() && format_str[end] != '}' && format_str[end] != ':') {
                end++;
            }

            if (end < format_str.length()) {
                std::string content = format_str.substr(pos + 1, end - pos - 1);

                // フォーマット指定子を探す
                size_t colon_pos = format_str.find(':', pos + 1);
                size_t close_pos = format_str.find('}', pos + 1);

                if (close_pos != std::string::npos) {
                    if (colon_pos != std::string::npos && colon_pos < close_pos) {
                        // {name:format} の形式
                        std::string var_name = format_str.substr(pos + 1, colon_pos - pos - 1);
                        std::string format_spec =
                            format_str.substr(colon_pos, close_pos - colon_pos + 1);

                        if (!var_name.empty() && std::isalpha(var_name[0])) {
                            // 変数名として有効
                            var_names.push_back(var_name);
                            converted_format += "{" + format_spec;  // {:x} のような形式に変換
                        } else {
                            // 通常の位置プレースホルダ
                            converted_format += format_str.substr(pos, close_pos - pos + 1);
                        }
                        pos = close_pos + 1;
                    } else {
                        // {name} の形式
                        std::string var_name = format_str.substr(pos + 1, close_pos - pos - 1);

                        if (!var_name.empty() && std::isalpha(var_name[0])) {
                            // 変数名として有効
                            var_names.push_back(var_name);
                            converted_format += "{}";  // 位置プレースホルダに変換
                        } else {
                            // 通常の位置プレースホルダまたは空
                            converted_format += format_str.substr(pos, close_pos - pos + 1);
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
LocalId ExprLowering::lower_call(const hir::HirCall& call, LoweringContext& ctx) {
    // println builtin特別処理
    if (call.func_name == "println" && !call.args.empty()) {
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
                        auto var_id = ctx.resolve_variable(var_name);
                        if (var_id) {
                            arg_locals.push_back(*var_id);
                        } else {
                            // 変数が見つからない場合、エラー用のダミー値を追加
                            // TODO: エラー報告
                            auto err_type = hir::make_error();
                            arg_locals.push_back(ctx.new_temp(err_type));
                        }
                    }

                    // 明示的な引数を処理（名前付き変数の後に追加）
                    for (size_t i = 1; i < call.args.size(); ++i) {
                        LocalId arg_local = lower_expression(*call.args[i], ctx);
                        arg_locals.push_back(arg_local);
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
                                std::move(func_operand), std::move(args),
                                std::nullopt,  // printlnは戻り値なし
                                success_block,
                                std::nullopt  // unwind無し
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
            std::move(func_operand), std::move(args),
            std::nullopt,  // printlnは戻り値なし
            success_block,
            std::nullopt  // unwind無し
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

    // 結果用の一時変数
    LocalId result = ctx.new_temp(hir::make_int());  // TODO: 実際の戻り値型

    // Call終端命令（現在のブロックを終端）
    BlockId success_block = ctx.new_block();

    // 関数名を関数参照オペランドとして作成
    auto func_operand = MirOperand::function_ref(call.func_name);

    // Call終端命令を手動で作成
    auto call_term = std::make_unique<MirTerminator>();
    call_term->kind = MirTerminator::Call;
    call_term->data = MirTerminator::CallData{
        std::move(func_operand), std::move(args), MirPlace{result},  // 戻り値の格納先
        success_block,
        std::nullopt  // unwind無し
    };
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
    if (!obj_type || obj_type->kind != hir::TypeKind::Struct) {
        // エラー：構造体型でない
        return ctx.new_temp(hir::make_error());
    }

    // 構造体のフィールドインデックスを取得
    auto field_idx = ctx.get_field_index(obj_type->name, member.member);
    if (!field_idx) {
        // エラー：フィールドが見つからない
        std::cerr << "[MIR] Error: Field '" << member.member << "' not found in struct '"
                  << obj_type->name << "'\n";
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
    // 配列とインデックスをlowering
    LocalId array = lower_expression(*index_expr.object, ctx);
    LocalId index = lower_expression(*index_expr.index, ctx);

    // インデックスアクセス（簡易実装）
    LocalId result = ctx.new_temp(hir::make_int());  // TODO: 要素型

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

    // 結果用の変数
    LocalId result = ctx.new_temp(hir::make_int());  // TODO: 実際の型

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
    conv_call_term->data =
        MirTerminator::CallData{std::move(conv_func_operand), std::move(conv_args),
                                MirPlace{str_result}, conv_success, std::nullopt};
    ctx.set_terminator(std::move(conv_call_term));
    ctx.switch_to_block(conv_success);

    return str_result;
}

}  // namespace cm::mir