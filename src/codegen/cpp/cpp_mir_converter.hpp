#pragma once

#include "../../hir/hir_nodes.hpp"
#include "../../hir/hir_types.hpp"
#include "cpp_mir.hpp"

#include <regex>
#include <sstream>
#include <unordered_map>

namespace cm {
namespace cpp_mir {

class HirToCppMirConverter {
   public:
    Program convert(const hir::HirProgram& hir_program) {
        Program program;

        // 標準ヘッダーの追加
        program.includes.push_back("cstdio");
        program.includes.push_back("cstdint");  // int8_t, uint32_t等のため

        // 各関数を変換
        for (const auto& decl : hir_program.declarations) {
            if (auto* func = std::get_if<std::unique_ptr<hir::HirFunction>>(&decl->kind)) {
                auto cpp_func = convertFunction(**func);

                // メタデータ更新
                if (cpp_func.uses_printf) {
                    // cstdioは既に追加済み
                }
                if (cpp_func.uses_string) {
                    addIncludeOnce(program.includes, "string");
                }
                if (cpp_func.uses_format) {
                    program.needs_format_helpers = true;
                }

                program.functions.push_back(std::move(cpp_func));
            }
            // 他の宣言タイプ（struct, interface等）は現時点では無視
        }

        return program;
    }

   private:
    // 現在の関数コンテキスト
    Function* current_function = nullptr;

    // 変数の型情報を保持
    std::unordered_map<std::string, Type> variable_types;

    Function convertFunction(const hir::HirFunction& hir_func) {
        Function func;
        func.name = (hir_func.name == "main") ? "main" : hir_func.name;

        // C++ではmainはint型を返す必要がある
        if (hir_func.name == "main") {
            func.return_type = Type::INT32;
        } else {
            func.return_type = convertType(hir_func.return_type);
        }

        // パラメータ変換
        for (const auto& param : hir_func.params) {
            func.parameters.emplace_back(convertType(param.type), param.name);
            variable_types[param.name] = convertType(param.type);
        }

        // 関数コンテキストを設定
        current_function = &func;
        variable_types.clear();

        // 本体を変換
        for (const auto& stmt : hir_func.body) {
            convertStatement(*stmt, func.body);
        }

        // 線形フロー検出
        func.is_linear = detectLinearFlow(func.body);

        current_function = nullptr;
        return func;
    }

    void convertStatement(const hir::HirStmt& stmt, std::vector<Statement>& body) {
        std::visit(
            [this, &body](const auto& s) {
                using T = std::decay_t<decltype(s)>;

                if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirLet>>) {
                    // 変数宣言
                    auto& let = *s;
                    Type type = convertType(let.type);
                    variable_types[let.name] = type;

                    // std::stringを使う場合はフラグを設定
                    if (type == Type::STRING && current_function) {
                        current_function->uses_string = true;
                    }

                    if (let.init) {
                        // 文字列リテラルの補間処理
                        if (type == Type::STRING) {
                            if (auto* lit = std::get_if<std::unique_ptr<hir::HirLiteral>>(
                                    &let.init->kind)) {
                                if (auto* str_val = std::get_if<std::string>(&(*lit)->value)) {
                                    // 補間が必要かチェック
                                    auto interpolated_expr =
                                        processStringLiteralInterpolation(*str_val);
                                    if (interpolated_expr) {
                                        body.push_back(
                                            Statement::Declare(type, let.name, *interpolated_expr));
                                        return;
                                    }
                                }
                            }
                        }
                        auto init_expr = convertExpression(*let.init);
                        body.push_back(Statement::Declare(type, let.name, init_expr));
                    } else {
                        body.push_back(Statement::Declare(type, let.name));
                    }

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirAssign>>) {
                    // 代入
                    auto& assign = *s;
                    auto value = convertExpression(*assign.value);
                    body.push_back(Statement::Assign(assign.target, value));

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirReturn>>) {
                    // return文
                    auto& ret = *s;
                    if (ret.value) {
                        body.push_back(Statement::ReturnValue(convertExpression(*ret.value)));
                    } else {
                        body.push_back(Statement::ReturnVoid());
                    }

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirExprStmt>>) {
                    // 式文（代入または関数呼び出し）
                    auto& expr_stmt = *s;

                    // 代入式かどうかをチェック
                    if (auto* bin =
                            std::get_if<std::unique_ptr<hir::HirBinary>>(&expr_stmt.expr->kind)) {
                        if ((*bin)->op == hir::HirBinaryOp::Assign) {
                            // 代入式の場合はAssignment文として処理
                            std::string target = extractTargetName(*(*bin)->lhs);
                            auto value = convertExpression(*(*bin)->rhs);
                            body.push_back(Statement::Assign(target, value));
                            return;
                        }
                    }

                    auto expr = convertExpression(*expr_stmt.expr);

                    // println/print の最適化
                    if (expr.kind == Expression::CALL) {
                        if (expr.func_name == "println" || expr.func_name == "print") {
                            auto printf_stmt = optimizePrintCall(expr.func_name, expr.args);
                            body.push_back(printf_stmt);
                            return;
                        }
                    }

                    body.push_back(Statement::Expr(expr));

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirIf>>) {
                    // if文
                    auto& if_stmt = *s;
                    std::vector<StatementPtr> then_body, else_body;

                    for (const auto& inner_stmt : if_stmt.then_block) {
                        std::vector<Statement> temp_body;
                        convertStatement(*inner_stmt, temp_body);
                        for (auto& st : temp_body) {
                            then_body.push_back(std::make_shared<Statement>(std::move(st)));
                        }
                    }
                    for (const auto& inner_stmt : if_stmt.else_block) {
                        std::vector<Statement> temp_body;
                        convertStatement(*inner_stmt, temp_body);
                        for (auto& st : temp_body) {
                            else_body.push_back(std::make_shared<Statement>(std::move(st)));
                        }
                    }

                    body.push_back(Statement::If(convertExpression(*if_stmt.cond),
                                                 std::move(then_body), std::move(else_body)));

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirLoop>>) {
                    // 無限ループ
                    auto& loop = *s;
                    std::vector<StatementPtr> loop_body;

                    for (const auto& inner_stmt : loop.body) {
                        std::vector<Statement> temp_body;
                        convertStatement(*inner_stmt, temp_body);
                        for (auto& st : temp_body) {
                            loop_body.push_back(std::make_shared<Statement>(std::move(st)));
                        }
                    }

                    // 無限ループの場合
                    body.push_back(Statement::WhileLoop(Expression::Literal("true", Type::BOOL),
                                                        std::move(loop_body)));

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirWhile>>) {
                    // while文
                    auto& while_stmt = *s;
                    std::vector<StatementPtr> while_body;

                    for (const auto& inner_stmt : while_stmt.body) {
                        std::vector<Statement> temp_body;
                        convertStatement(*inner_stmt, temp_body);
                        for (auto& st : temp_body) {
                            while_body.push_back(std::make_shared<Statement>(std::move(st)));
                        }
                    }

                    body.push_back(Statement::WhileLoop(convertExpression(*while_stmt.cond),
                                                        std::move(while_body)));

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirFor>>) {
                    // for文
                    auto& for_stmt = *s;

                    // init
                    StatementPtr init_ptr = nullptr;
                    if (for_stmt.init) {
                        std::vector<Statement> init_stmts;
                        convertStatement(*for_stmt.init, init_stmts);
                        if (!init_stmts.empty()) {
                            init_ptr = std::make_shared<Statement>(std::move(init_stmts[0]));
                        }
                    }

                    // cond
                    std::optional<Expression> cond_expr;
                    if (for_stmt.cond) {
                        cond_expr = convertExpression(*for_stmt.cond);
                    }

                    // update - 式として処理
                    StatementPtr update_ptr = nullptr;
                    if (for_stmt.update) {
                        auto update_expr = convertExpression(*for_stmt.update);
                        auto update_stmt = Statement::Expr(update_expr);
                        update_ptr = std::make_shared<Statement>(std::move(update_stmt));
                    }

                    // body
                    std::vector<StatementPtr> for_body;
                    for (const auto& inner_stmt : for_stmt.body) {
                        std::vector<Statement> temp_body;
                        convertStatement(*inner_stmt, temp_body);
                        for (auto& st : temp_body) {
                            for_body.push_back(std::make_shared<Statement>(std::move(st)));
                        }
                    }

                    body.push_back(
                        Statement::ForLoop(init_ptr, cond_expr, update_ptr, std::move(for_body)));

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirBreak>>) {
                    body.push_back(Statement::Break());

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirContinue>>) {
                    body.push_back(Statement::Continue());

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirBlock>>) {
                    // ブロック文 - 内部の文を展開
                    auto& block = *s;
                    for (const auto& inner_stmt : block.stmts) {
                        convertStatement(*inner_stmt, body);
                    }

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirSwitch>>) {
                    // switch文 - if-else if-else連鎖に変換（最適化版）
                    auto& sw = *s;
                    auto switch_expr = convertExpression(*sw.expr);

                    // else if チェーン用の構造を構築
                    std::vector<std::pair<Expression, std::vector<StatementPtr>>> if_chain;
                    std::vector<StatementPtr> else_body;

                    for (size_t i = 0; i < sw.cases.size(); ++i) {
                        const auto& case_stmt = sw.cases[i];

                        std::vector<StatementPtr> case_body;
                        for (const auto& inner_stmt : case_stmt.stmts) {
                            std::vector<Statement> temp_body;
                            convertStatement(*inner_stmt, temp_body);
                            for (auto& st : temp_body) {
                                case_body.push_back(std::make_shared<Statement>(std::move(st)));
                            }
                        }

                        // パターンから条件式を生成
                        std::string cond_str;
                        bool has_condition = false;

                        if (case_stmt.pattern) {
                            cond_str = generatePatternCondition(switch_expr, *case_stmt.pattern);
                            has_condition = true;
                        } else if (case_stmt.value) {
                            // 後方互換性: valueが設定されている場合
                            auto case_val = convertExpression(*case_stmt.value);
                            cond_str = "(" + exprToString(switch_expr) +
                                       " == " + exprToString(case_val) + ")";
                            has_condition = true;
                        }

                        if (has_condition) {
                            // 条件付きケース
                            auto cond = Expression::BinaryOp(cond_str, Type::BOOL);
                            if_chain.push_back({cond, std::move(case_body)});
                        } else {
                            // else/defaultケース
                            else_body = std::move(case_body);
                        }
                    }

                    // if-else if-else チェーンを生成
                    if (!if_chain.empty()) {
                        // 最初のif
                        auto& first = if_chain[0];
                        if (if_chain.size() == 1 && else_body.empty()) {
                            // 単純なif
                            body.push_back(Statement::If(first.first, std::move(first.second)));
                        } else {
                            // if-else if-else構造を構築
                            // 最後のelse部分から逆順に構築
                            std::vector<StatementPtr> current_else = std::move(else_body);

                            // 後ろから処理（else if チェーンを構築）
                            for (int j = if_chain.size() - 1; j >= 1; --j) {
                                auto& chain_item = if_chain[j];
                                std::vector<StatementPtr> if_else_body;
                                if_else_body.push_back(std::make_shared<Statement>(
                                    Statement::If(chain_item.first, std::move(chain_item.second),
                                                  std::move(current_else))));
                                current_else = std::move(if_else_body);
                            }

                            // 最初のifを追加
                            body.push_back(Statement::If(first.first, std::move(first.second),
                                                         std::move(current_else)));
                        }
                    } else if (!else_body.empty()) {
                        // elseのみ（全てdefault）
                        for (auto& stmt : else_body) {
                            body.push_back(std::move(*stmt));
                        }
                    }
                }
            },
            stmt.kind);
    }

    Expression convertExpression(const hir::HirExpr& expr) {
        return std::visit(
            [this](const auto& e) -> Expression {
                using T = std::decay_t<decltype(e)>;

                if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirLiteral>>) {
                    auto& lit = *e;
                    if (auto* str_val = std::get_if<std::string>(&lit.value)) {
                        // エスケープされたブレースを変換: {{ → {, }} → }
                        std::string processed = *str_val;
                        size_t pos = 0;
                        while ((pos = processed.find("{{", pos)) != std::string::npos) {
                            processed.replace(pos, 2, "{");
                            pos += 1;
                        }
                        pos = 0;
                        while ((pos = processed.find("}}", pos)) != std::string::npos) {
                            processed.replace(pos, 2, "}");
                            pos += 1;
                        }
                        return Expression::Literal("\"" + processed + "\"", Type::STRING);
                    } else if (auto* int_val = std::get_if<int64_t>(&lit.value)) {
                        return Expression::Literal(std::to_string(*int_val), Type::INT32);
                    } else if (auto* bool_val = std::get_if<bool>(&lit.value)) {
                        return Expression::Literal(*bool_val ? "true" : "false", Type::BOOL);
                    } else if (auto* double_val = std::get_if<double>(&lit.value)) {
                        return Expression::Literal(std::to_string(*double_val), Type::DOUBLE);
                    } else if (auto* char_val = std::get_if<char>(&lit.value)) {
                        // C++のchar型はシングルクォートで表現
                        std::string char_str = "'";
                        char_str += *char_val;
                        char_str += "'";
                        return Expression::Literal(char_str, Type::CHAR);
                    }
                    return Expression::Literal("0", Type::INT32);

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirVarRef>>) {
                    auto& var = *e;
                    auto it = variable_types.find(var.name);
                    Type type = (it != variable_types.end()) ? it->second : Type::INT32;
                    return Expression::Variable(var.name, type);

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirCall>>) {
                    auto& call = *e;
                    std::vector<Expression> args;
                    for (const auto& arg : call.args) {
                        args.push_back(convertExpression(*arg));
                    }
                    // 完全修飾名から関数名を抽出（例: "std::io::println" → "println"）
                    auto func_name = extractFunctionName(call.func_name);
                    return Expression::Call(func_name, args);

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirBinary>>) {
                    auto& bin = *e;
                    auto lhs = convertExpression(*bin.lhs);
                    auto rhs = convertExpression(*bin.rhs);

                    // 文字列連結の特殊処理
                    if (bin.op == hir::HirBinaryOp::Add &&
                        (lhs.type == Type::STRING || rhs.type == Type::STRING)) {
                        // 文字列連結: std::string() + std::to_string() 形式に変換
                        std::string lhs_str = convertToStringExpr(lhs);
                        std::string rhs_str = convertToStringExpr(rhs);
                        std::string result_str = "(" + lhs_str + " + " + rhs_str + ")";

                        // std::stringを使うことをマーク
                        if (current_function) {
                            current_function->uses_string = true;
                        }

                        return Expression::BinaryOp(result_str, Type::STRING);
                    }

                    // 二項演算子の種類に応じて処理
                    std::string op_str;
                    switch (bin.op) {
                        case hir::HirBinaryOp::Add:
                            op_str = "+";
                            break;
                        case hir::HirBinaryOp::Sub:
                            op_str = "-";
                            break;
                        case hir::HirBinaryOp::Mul:
                            op_str = "*";
                            break;
                        case hir::HirBinaryOp::Div:
                            op_str = "/";
                            break;
                        case hir::HirBinaryOp::Mod:
                            op_str = "%";
                            break;
                        case hir::HirBinaryOp::Eq:
                            op_str = "==";
                            break;
                        case hir::HirBinaryOp::Ne:
                            op_str = "!=";
                            break;
                        case hir::HirBinaryOp::Lt:
                            op_str = "<";
                            break;
                        case hir::HirBinaryOp::Gt:
                            op_str = ">";
                            break;
                        case hir::HirBinaryOp::Le:
                            op_str = "<=";
                            break;
                        case hir::HirBinaryOp::Ge:
                            op_str = ">=";
                            break;
                        case hir::HirBinaryOp::And:
                            op_str = "&&";
                            break;
                        case hir::HirBinaryOp::Or:
                            op_str = "||";
                            break;
                        case hir::HirBinaryOp::Assign:
                            op_str = "=";
                            break;
                        default:
                            op_str = "+";
                            break;
                    }

                    // 文字列として結合
                    std::string result_str =
                        "(" + exprToString(lhs) + " " + op_str + " " + exprToString(rhs) + ")";
                    return Expression::BinaryOp(result_str, lhs.type);

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirUnary>>) {
                    auto& unary = *e;
                    auto operand = convertExpression(*unary.operand);

                    std::string result_str;
                    switch (unary.op) {
                        case hir::HirUnaryOp::Neg:
                            result_str = "(-" + exprToString(operand) + ")";
                            break;
                        case hir::HirUnaryOp::Not:
                            result_str = "(!" + exprToString(operand) + ")";
                            break;
                        case hir::HirUnaryOp::BitNot:
                            result_str = "(~" + exprToString(operand) + ")";
                            break;
                        case hir::HirUnaryOp::PreInc:
                            result_str = "(++" + exprToString(operand) + ")";
                            break;
                        case hir::HirUnaryOp::PreDec:
                            result_str = "(--" + exprToString(operand) + ")";
                            break;
                        case hir::HirUnaryOp::PostInc:
                            result_str = "(" + exprToString(operand) + "++)";
                            break;
                        case hir::HirUnaryOp::PostDec:
                            result_str = "(" + exprToString(operand) + "--)";
                            break;
                        case hir::HirUnaryOp::Deref:
                            result_str = "(*" + exprToString(operand) + ")";
                            break;
                        case hir::HirUnaryOp::AddrOf:
                            result_str = "(&" + exprToString(operand) + ")";
                            break;
                        default:
                            result_str = "(-" + exprToString(operand) + ")";
                            break;
                    }

                    Expression result;
                    result.kind = Expression::UNARY_OP;
                    result.type = operand.type;
                    result.value = result_str;
                    return result;

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirIndex>>) {
                    auto& idx = *e;
                    auto obj = convertExpression(*idx.object);
                    auto index = convertExpression(*idx.index);

                    std::string result_str = exprToString(obj) + "[" + exprToString(index) + "]";
                    Expression result;
                    result.kind = Expression::VARIABLE;
                    result.type = Type::INT32;  // TODO: 正確な型を推論
                    result.value = result_str;
                    return result;

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirMember>>) {
                    auto& mem = *e;
                    auto obj = convertExpression(*mem.object);

                    std::string result_str = exprToString(obj) + "." + mem.member;
                    Expression result;
                    result.kind = Expression::VARIABLE;
                    result.type = Type::INT32;  // TODO: 正確な型を推論
                    result.value = result_str;
                    return result;

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirTernary>>) {
                    auto& tern = *e;
                    auto cond = convertExpression(*tern.condition);
                    auto then_expr = convertExpression(*tern.then_expr);
                    auto else_expr = convertExpression(*tern.else_expr);

                    std::string result_str = "(" + exprToString(cond) + " ? " +
                                             exprToString(then_expr) + " : " +
                                             exprToString(else_expr) + ")";
                    return Expression::BinaryOp(result_str, then_expr.type);
                }

                return Expression::Literal("0", Type::INT32);
            },
            expr.kind);
    }

    Type convertType(const hir::TypePtr& hir_type) {
        if (!hir_type)
            return Type::VOID;

        // TypeKindを直接チェック (ast::TypeKind経由)
        switch (hir_type->kind) {
            case ast::TypeKind::Void:
                return Type::VOID;
            case ast::TypeKind::Bool:
                return Type::BOOL;
            case ast::TypeKind::Char:
                return Type::CHAR;
            case ast::TypeKind::Tiny:
                return Type::INT8;
            case ast::TypeKind::Short:
                return Type::INT16;
            case ast::TypeKind::Int:
                return Type::INT32;
            case ast::TypeKind::Long:
                return Type::INT64;
            case ast::TypeKind::UTiny:
                return Type::UINT8;
            case ast::TypeKind::UShort:
                return Type::UINT16;
            case ast::TypeKind::UInt:
                return Type::UINT32;
            case ast::TypeKind::ULong:
                return Type::UINT64;
            case ast::TypeKind::Float:
                return Type::FLOAT;
            case ast::TypeKind::Double:
                return Type::DOUBLE;
            case ast::TypeKind::String:
                return Type::STRING;
            default:
                return Type::INT32;
        }
    }

    // 式を文字列連結用に変換する
    // 文字列リテラルの場合はstd::string()でラップ、数値の場合はstd::to_string()でラップ
    std::string convertToStringExpr(const Expression& expr) {
        std::string base = exprToString(expr);

        switch (expr.type) {
            case Type::STRING:
                // 文字列リテラルの場合、std::string()でラップ
                if (expr.kind == Expression::LITERAL) {
                    return "std::string(" + base + ")";
                }
                // 既にstd::stringの変数や式の場合はそのまま
                return base;
            case Type::INT8:
            case Type::INT16:
            case Type::INT32:
            case Type::INT64:
            case Type::UINT8:
            case Type::UINT16:
            case Type::UINT32:
            case Type::UINT64:
                return "std::to_string(" + base + ")";
            case Type::FLOAT:
                return "std::to_string(" + base + ")";
            case Type::DOUBLE:
                return "std::to_string(" + base + ")";
            case Type::BOOL:
                return "std::string(" + base + " ? \"true\" : \"false\")";
            case Type::CHAR_PTR:
                return "std::string(" + base + ")";
            default:
                return base;
        }
    }

    Statement optimizePrintCall(const std::string& func_name, const std::vector<Expression>& args) {
        bool add_newline = (func_name == "println");

        if (args.empty()) {
            // 引数なしの場合
            if (add_newline) {
                return Statement::PrintF("\\n", {});
            } else {
                return Statement::PrintF("", {});
            }
        }

        // 文字列補間の解析
        std::string format_string;
        std::vector<Expression> printf_args;

        // 最初の引数が文字列リテラルかチェック
        if (args[0].kind == Expression::LITERAL && args[0].type == Type::STRING) {
            auto str_value = args[0].value;
            // 引用符を除去
            if (str_value.length() >= 2 && str_value[0] == '"' && str_value.back() == '"') {
                str_value = str_value.substr(1, str_value.length() - 2);
            }

            // エスケープ処理（生の改行をエスケープシーケンスに変換）
            std::string escaped_str;
            for (size_t i = 0; i < str_value.length(); ++i) {
                char c = str_value[i];
                if (c == '\n') {
                    escaped_str += "\\n";
                } else if (c == '\r') {
                    escaped_str += "\\r";
                } else if (c == '\t') {
                    escaped_str += "\\t";
                } else {
                    escaped_str += c;
                }
            }

            // 文字列補間の処理
            auto interpolated = processStringInterpolation(escaped_str, args);
            format_string = interpolated.first;
            printf_args = interpolated.second;
        } else if (args[0].kind == Expression::BINARY_OP) {
            // 文字列連結の場合 - フラット化してprintf形式に変換
            flattenStringConcat(args[0], format_string, printf_args);
        } else {
            // 文字列でない場合は全引数を連結して出力
            for (size_t i = 0; i < args.size(); ++i) {
                format_string += getFormatSpecifier(args[i].type);
                printf_args.push_back(args[i]);
            }
        }

        // 改行を追加
        if (add_newline) {
            format_string += "\\n";
        }

        current_function->uses_printf = true;
        return Statement::PrintF(format_string, printf_args);
    }

    // 文字列連結式をフラット化してprintf形式に変換
    void flattenStringConcat(const Expression& expr, std::string& format_str,
                             std::vector<Expression>& args) {
        // 式がリテラルの場合
        if (expr.kind == Expression::LITERAL) {
            if (expr.type == Type::STRING) {
                std::string val = expr.value;
                // 引用符を除去
                if (val.length() >= 2 && val[0] == '"' && val.back() == '"') {
                    val = val.substr(1, val.length() - 2);
                }
                // エスケープ処理
                for (char c : val) {
                    if (c == '\n')
                        format_str += "\\n";
                    else if (c == '\r')
                        format_str += "\\r";
                    else if (c == '\t')
                        format_str += "\\t";
                    else if (c == '%')
                        format_str += "%%";  // %をエスケープ
                    else
                        format_str += c;
                }
            } else {
                format_str += getFormatSpecifier(expr.type);
                args.push_back(expr);
            }
        } else if (expr.kind == Expression::VARIABLE) {
            format_str += getFormatSpecifier(expr.type);
            args.push_back(expr);
        } else if (expr.kind == Expression::BINARY_OP) {
            // 二項演算子の場合、文字列連結かどうかを確認
            // valueの形式: "(lhs + rhs)"
            // 再帰的に処理するのは難しいので、シンプルに式として扱う
            format_str += getFormatSpecifier(expr.type);
            args.push_back(expr);
        } else {
            // その他の式
            format_str += getFormatSpecifier(expr.type);
            args.push_back(expr);
        }
    }

    std::pair<std::string, std::vector<Expression>> processStringInterpolation(
        const std::string& str, const std::vector<Expression>& original_args) {
        std::string format_str;
        std::vector<Expression> args;

        // 位置引数のインデックス（original_args[1]から始まる）
        size_t arg_index = 1;

        size_t i = 0;
        while (i < str.length()) {
            // エスケープされたブレースの処理
            if (str[i] == '{' && i + 1 < str.length() && str[i + 1] == '{') {
                format_str += '{';
                i += 2;
                continue;
            }
            if (str[i] == '}' && i + 1 < str.length() && str[i + 1] == '}') {
                format_str += '}';
                i += 2;
                continue;
            }

            if (str[i] == '{') {
                // {}プレースホルダーまたは{変数名}を検出
                size_t end = str.find('}', i);
                if (end != std::string::npos) {
                    std::string placeholder = str.substr(i + 1, end - i - 1);

                    if (placeholder.empty()) {
                        // {}の場合 - 位置引数を使用
                        if (arg_index < original_args.size()) {
                            const auto& arg = original_args[arg_index++];
                            format_str += getFormatSpecifier(arg.type);
                            args.push_back(arg);
                        } else {
                            // 引数が足りない場合はそのまま出力
                            format_str += "{}";
                        }
                    } else if (placeholder[0] == ':') {
                        // {:x}形式 - 位置引数にフォーマット指定子を適用
                        std::string format_spec = placeholder.substr(1);
                        if (arg_index < original_args.size()) {
                            const auto& arg = original_args[arg_index++];

                            // バイナリ形式の特別処理
                            if (format_spec == "b") {
                                format_str += "%s";
                                // バイナリ変換のためのラムダ式を使用（プレフィックスなし）
                                Expression binary_expr;
                                binary_expr.kind = Expression::BINARY_OP;
                                binary_expr.type = Type::STRING;
                                binary_expr.value =
                                    "[&]{ std::string r; int _v = " + exprToString(arg) +
                                    "; if(_v==0)return std::string(\"0\"); "
                                    "while(_v>0){r=(char)('0'+(_v&1))+r;_v>>=1;} return r; "
                                    "}().c_str()";
                                args.push_back(binary_expr);
                                if (current_function)
                                    current_function->uses_string = true;
                            } else if (format_spec.length() > 2 && format_spec[0] == '0' &&
                                       format_spec[1] == '>') {
                                // ゼロパディング {:0>5} 形式
                                std::string width = format_spec.substr(2);
                                format_str += "%0" + width + "d";
                                args.push_back(arg);
                            } else if (format_spec.length() > 1 && format_spec[0] == '^') {
                                // 中央揃え {:^10} 形式
                                std::string width = format_spec.substr(1);
                                // widthの妥当性を確認（例外は上位でキャッチされる）
                                std::stoi(width);
                                // 中央揃えをラムダで実装
                                format_str += "%s";
                                Expression center_expr;
                                center_expr.kind = Expression::BINARY_OP;
                                center_expr.type = Type::STRING;
                                center_expr.value = "[&]{ std::string s = " + exprToString(arg) +
                                                    "; int w = " + width +
                                                    "; int pad = (w > (int)s.length()) ? w - "
                                                    "s.length() : 0; int left = pad / 2; int right "
                                                    "= pad - left; return std::string(left, ' ') + "
                                                    "s + std::string(right, ' '); }().c_str()";
                                args.push_back(center_expr);
                                if (current_function)
                                    current_function->uses_string = true;
                            } else {
                                format_str += convertFormatSpec(format_spec);
                                args.push_back(arg);
                            }
                        } else {
                            format_str += "{" + placeholder + "}";
                        }
                    } else if (placeholder.find(':') != std::string::npos) {
                        // {x:02d}形式 - 変数名付きフォーマット指定子
                        size_t colon_pos = placeholder.find(':');
                        std::string actual_var = placeholder.substr(0, colon_pos);
                        std::string format_spec = placeholder.substr(colon_pos + 1);

                        format_str += convertFormatSpec(format_spec);

                        auto it = variable_types.find(actual_var);
                        Type type = (it != variable_types.end()) ? it->second : Type::INT32;
                        args.push_back(Expression::Variable(actual_var, type));
                    } else {
                        // {変数名}形式 - 変数参照
                        auto it = variable_types.find(placeholder);
                        if (it != variable_types.end()) {
                            Type type = it->second;
                            format_str += getFormatSpecifier(type);
                            args.push_back(Expression::Variable(placeholder, type));
                        } else {
                            // 変数が見つからない場合はリテラルとして出力
                            format_str += "{" + placeholder + "}";
                        }
                    }

                    i = end + 1;
                    continue;
                }
            }

            // 通常の文字
            if (str[i] == '%') {
                format_str += "%%";  // %をエスケープ
            } else {
                format_str += str[i];
            }
            i++;
        }

        return {format_str, args};
    }

    std::string convertFormatSpec(const std::string& spec) {
        // Cm形式からprintf形式への変換
        // 例: "02d" → "%02d", ".2" → "%.2f", "x" → "%x", "<10" → "%-10s"
        if (spec.empty())
            return "%d";

        std::string result = "%";

        size_t i = 0;

        // アライメント指定子
        if (i < spec.length() && (spec[i] == '<' || spec[i] == '>' || spec[i] == '^')) {
            char align = spec[i++];
            // 幅を取得
            std::string width;
            while (i < spec.length() && std::isdigit(spec[i])) {
                width += spec[i++];
            }
            if (!width.empty()) {
                if (align == '<') {
                    result += "-" + width;  // 左寄せ
                } else if (align == '>' || align == '^') {
                    result += width;  // 右寄せ（中央は右寄せで近似）
                }
            }
            result += "s";  // アライメント指定は文字列用
            return result;
        }

        // ゼロパディング
        if (i < spec.length() && spec[i] == '0') {
            result += spec[i++];
        }

        // 精度指定（.N）
        if (i < spec.length() && spec[i] == '.') {
            result += spec[i++];
            while (i < spec.length() && std::isdigit(spec[i])) {
                result += spec[i++];
            }
            // 精度指定があれば浮動小数点を仮定
            if (i >= spec.length()) {
                result += "f";
                return result;
            }
        }

        // 数字（幅）
        while (i < spec.length() && std::isdigit(spec[i])) {
            result += spec[i++];
        }

        // 型指定子
        if (i < spec.length()) {
            char type_char = spec[i];
            switch (type_char) {
                case 'd':
                case 'i':
                    result += "d";
                    break;
                case 'x':
                    result += "x";
                    break;
                case 'X':
                    result += "X";
                    break;
                case 'o':
                    result += "o";
                    break;
                case 'b':
                    // バイナリはprintfではサポートされていないので%dで代用
                    result += "d";
                    break;
                case 'f':
                    result += "f";
                    break;
                case 'e':
                    result += "e";
                    break;
                case 'E':
                    result += "E";
                    break;
                case 's':
                    result += "s";
                    break;
                default:
                    result += "d";
                    break;
            }
        } else {
            result += "d";  // デフォルト
        }

        return result;
    }

    std::string getFormatSpecifier(Type type) {
        switch (type) {
            case Type::INT8:
            case Type::INT16:
            case Type::INT32:
                return "%d";
            case Type::INT64:
                return "%lld";
            case Type::UINT8:
            case Type::UINT16:
            case Type::UINT32:
                return "%u";
            case Type::UINT64:
                return "%llu";
            case Type::FLOAT:
                return "%g";  // floatも%gで十分な精度
            case Type::DOUBLE:
                return "%g";  // %gは不要な末尾ゼロを省略
            case Type::STRING:
                return "%s";
            case Type::BOOL:
                return "%s";  // true/false として出力
            case Type::CHAR:
                return "%c";
            case Type::CHAR_PTR:
                return "%s";
            default:
                return "%d";
        }
    }

    // switchのパターンから条件式を生成
    std::string generatePatternCondition(const Expression& switch_expr,
                                         const hir::HirSwitchPattern& pattern) {
        std::string expr_str = exprToString(switch_expr);

        switch (pattern.kind) {
            case hir::HirSwitchPattern::SingleValue: {
                if (pattern.value) {
                    auto val = convertExpression(*pattern.value);
                    return "(" + expr_str + " == " + exprToString(val) + ")";
                }
                return "true";
            }
            case hir::HirSwitchPattern::Range: {
                if (pattern.range_start && pattern.range_end) {
                    auto start = convertExpression(*pattern.range_start);
                    auto end = convertExpression(*pattern.range_end);
                    return "((" + expr_str + " >= " + exprToString(start) + ") && (" + expr_str +
                           " <= " + exprToString(end) + "))";
                }
                return "true";
            }
            case hir::HirSwitchPattern::Or: {
                std::string result = "(";
                bool first = true;
                for (const auto& sub_pattern : pattern.or_patterns) {
                    if (!first)
                        result += " || ";
                    first = false;
                    result += generatePatternCondition(switch_expr, *sub_pattern);
                }
                result += ")";
                return result;
            }
            default:
                return "true";
        }
    }

    bool detectLinearFlow(const std::vector<Statement>& statements) {
        for (const auto& stmt : statements) {
            // 分岐やループがある場合は非線形
            if (stmt.kind == StatementKind::IF_ELSE || stmt.kind == StatementKind::WHILE ||
                stmt.kind == StatementKind::FOR || stmt.kind == StatementKind::BREAK ||
                stmt.kind == StatementKind::CONTINUE) {
                return false;
            }
        }
        return true;
    }

    std::string extractFunctionName(const std::string& qualified_name) {
        // 完全修飾名から関数名を抽出（例: "std::io::println" → "println"）
        size_t last_colon = qualified_name.rfind("::");
        if (last_colon != std::string::npos) {
            return qualified_name.substr(last_colon + 2);
        }
        return qualified_name;
    }

    // 代入のターゲット名を抽出
    std::string extractTargetName(const hir::HirExpr& expr) {
        if (auto* var = std::get_if<std::unique_ptr<hir::HirVarRef>>(&expr.kind)) {
            return (*var)->name;
        } else if (auto* idx = std::get_if<std::unique_ptr<hir::HirIndex>>(&expr.kind)) {
            // 配列アクセスの場合は式として返す
            return exprToString(convertExpression(*(*idx)->object)) + "[" +
                   exprToString(convertExpression(*(*idx)->index)) + "]";
        } else if (auto* mem = std::get_if<std::unique_ptr<hir::HirMember>>(&expr.kind)) {
            // メンバアクセスの場合
            return exprToString(convertExpression(*(*mem)->object)) + "." + (*mem)->member;
        }
        return "unknown";
    }

    void addIncludeOnce(std::vector<std::string>& includes, const std::string& header) {
        if (std::find(includes.begin(), includes.end(), header) == includes.end()) {
            includes.push_back(header);
        }
    }

    std::string exprToString(const Expression& expr) {
        if (expr.kind == Expression::LITERAL || expr.kind == Expression::VARIABLE ||
            expr.kind == Expression::BINARY_OP || expr.kind == Expression::UNARY_OP) {
            return expr.value;
        } else if (expr.kind == Expression::CALL) {
            std::string result = expr.func_name + "(";
            for (size_t i = 0; i < expr.args.size(); ++i) {
                if (i > 0)
                    result += ", ";
                result += exprToString(expr.args[i]);
            }
            result += ")";
            return result;
        }
        return "";
    }

    // 文字列リテラル内の変数補間を処理
    std::optional<Expression> processStringLiteralInterpolation(const std::string& str) {
        // 補間が必要かチェック
        bool needs_interpolation = false;
        size_t pos = 0;
        while ((pos = str.find('{', pos)) != std::string::npos) {
            // {{ はエスケープなのでスキップ
            if (pos + 1 < str.size() && str[pos + 1] == '{') {
                pos += 2;
                continue;
            }
            size_t end = str.find('}', pos);
            if (end != std::string::npos) {
                std::string var_content = str.substr(pos + 1, end - pos - 1);
                // 変数名を取得（フォーマット指定子を除く）
                size_t colon = var_content.find(':');
                std::string var_name =
                    (colon != std::string::npos) ? var_content.substr(0, colon) : var_content;
                // 変数が存在するかチェック
                if (!var_name.empty() && variable_types.find(var_name) != variable_types.end()) {
                    needs_interpolation = true;
                    break;
                }
            }
            pos++;
        }

        if (!needs_interpolation) {
            return std::nullopt;  // 補間不要
        }

        // 文字列連結式を構築
        std::string result_expr;
        pos = 0;
        bool first = true;

        while (pos < str.length()) {
            // エスケープされたブレースの処理
            if (str[pos] == '{' && pos + 1 < str.length() && str[pos + 1] == '{') {
                if (!first)
                    result_expr += " + ";
                first = false;
                result_expr += "std::string(\"{\")";
                pos += 2;
                continue;
            }
            if (str[pos] == '}' && pos + 1 < str.length() && str[pos + 1] == '}') {
                if (!first)
                    result_expr += " + ";
                first = false;
                result_expr += "std::string(\"}\")";
                pos += 2;
                continue;
            }

            if (str[pos] == '{') {
                size_t end = str.find('}', pos);
                if (end != std::string::npos) {
                    std::string var_content = str.substr(pos + 1, end - pos - 1);
                    size_t colon = var_content.find(':');
                    std::string var_name =
                        (colon != std::string::npos) ? var_content.substr(0, colon) : var_content;
                    std::string format_spec =
                        (colon != std::string::npos) ? var_content.substr(colon + 1) : "";

                    auto it = variable_types.find(var_name);
                    if (it != variable_types.end()) {
                        if (!first)
                            result_expr += " + ";
                        first = false;

                        // フォーマット指定子がある場合はカスタム処理
                        if (!format_spec.empty()) {
                            result_expr +=
                                formatVariableForConcat(var_name, it->second, format_spec);
                        } else {
                            // 通常の変数連結
                            result_expr += convertToStdString(var_name, it->second);
                        }
                        pos = end + 1;
                        continue;
                    }
                }
            }

            // 通常の文字列部分を収集
            size_t next_brace = str.find('{', pos);
            if (next_brace == std::string::npos) {
                next_brace = str.length();
            }

            std::string text_part = str.substr(pos, next_brace - pos);
            if (!text_part.empty()) {
                if (!first)
                    result_expr += " + ";
                first = false;
                result_expr += "std::string(\"" + escapeString(text_part) + "\")";
            }
            pos = next_brace;
        }

        if (current_function) {
            current_function->uses_string = true;
        }

        Expression expr;
        expr.kind = Expression::BINARY_OP;
        expr.type = Type::STRING;
        expr.value = result_expr;
        return expr;
    }

    // 変数をstd::stringに変換する式を生成
    std::string convertToStdString(const std::string& var_name, Type type) {
        switch (type) {
            case Type::STRING:
                return var_name;
            case Type::INT32:
                return "std::to_string(" + var_name + ")";
            case Type::DOUBLE:
                return "std::to_string(" + var_name + ")";
            case Type::BOOL:
                return "std::string(" + var_name + " ? \"true\" : \"false\")";
            default:
                return var_name;
        }
    }

    // フォーマット指定子付きの変数を文字列連結用に変換
    std::string formatVariableForConcat(const std::string& var_name, Type type,
                                        const std::string& spec) {
        // フォーマット指定子を解析
        if (spec == "x") {
            // 16進数小文字
            return "[&]{ char buf[32]; snprintf(buf, sizeof(buf), \"%x\", " + var_name +
                   "); return std::string(buf); }()";
        } else if (spec == "X") {
            // 16進数大文字
            return "[&]{ char buf[32]; snprintf(buf, sizeof(buf), \"%X\", " + var_name +
                   "); return std::string(buf); }()";
        } else if (spec == "o") {
            // 8進数
            return "[&]{ char buf[32]; snprintf(buf, sizeof(buf), \"%o\", " + var_name +
                   "); return std::string(buf); }()";
        } else if (spec == "b") {
            // 2進数（カスタム処理）
            return "[&]{ std::string r; int n = " + var_name +
                   "; if(n==0)return std::string(\"0\"); while(n>0){r=(char)('0'+(n&1))+r;n>>=1;} "
                   "return r; }()";
        } else if (spec.length() > 0 && spec[0] == '.') {
            // 精度指定（浮動小数点）
            std::string precision = spec.substr(1);
            return "[&]{ char buf[64]; snprintf(buf, sizeof(buf), \"%." + precision + "f\", " +
                   var_name + "); return std::string(buf); }()";
        } else if (spec.length() > 2 && spec[0] == '0' && spec[1] == '>') {
            // ゼロパディング（例: 0>5）
            std::string width = spec.substr(2);
            return "[&]{ char buf[64]; snprintf(buf, sizeof(buf), \"%0" + width + "d\", " +
                   var_name + "); return std::string(buf); }()";
        }
        // デフォルト
        return convertToStdString(var_name, type);
    }

    // 文字列をエスケープ
    std::string escapeString(const std::string& str) {
        std::string result;
        for (char c : str) {
            switch (c) {
                case '\\':
                    result += "\\\\";
                    break;
                case '"':
                    result += "\\\"";
                    break;
                case '\n':
                    result += "\\n";
                    break;
                case '\r':
                    result += "\\r";
                    break;
                case '\t':
                    result += "\\t";
                    break;
                default:
                    result += c;
                    break;
            }
        }
        return result;
    }
};

}  // namespace cpp_mir
}  // namespace cm