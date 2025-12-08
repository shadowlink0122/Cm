#pragma once

#include "../hir/hir_nodes.hpp"
#include "../hir/hir_types.hpp"
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
        func.return_type = convertType(hir_func.return_type);

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

                    if (let.init) {
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
                    // ループ（whileとして処理）
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
                    // switch文 - if-else連鎖に変換
                    auto& sw = *s;
                    auto switch_expr = convertExpression(*sw.expr);

                    // switch文をif-else連鎖に変換
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

                        if (case_stmt.value) {
                            // 値がある場合はif文を生成
                            auto case_val = convertExpression(*case_stmt.value);
                            // 条件: switch_expr == case_val
                            std::string cond_str = "(" + exprToString(switch_expr) +
                                                   " == " + exprToString(case_val) + ")";
                            auto cond = Expression::BinaryOp(cond_str, Type::BOOL);

                            body.push_back(Statement::If(cond, std::move(case_body)));
                        } else {
                            // else/defaultケース - 直接文を展開
                            for (auto& st : case_body) {
                                body.push_back(*st);
                            }
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
                        return Expression::Literal("\"" + *str_val + "\"", Type::STRING);
                    } else if (auto* int_val = std::get_if<int64_t>(&lit.value)) {
                        return Expression::Literal(std::to_string(*int_val), Type::INT);
                    } else if (auto* bool_val = std::get_if<bool>(&lit.value)) {
                        return Expression::Literal(*bool_val ? "true" : "false", Type::BOOL);
                    } else if (auto* double_val = std::get_if<double>(&lit.value)) {
                        return Expression::Literal(std::to_string(*double_val), Type::DOUBLE);
                    }
                    return Expression::Literal("0", Type::INT);

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirVarRef>>) {
                    auto& var = *e;
                    auto it = variable_types.find(var.name);
                    Type type = (it != variable_types.end()) ? it->second : Type::INT;
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

                    std::string op_str;
                    switch (unary.op) {
                        case hir::HirUnaryOp::Neg:
                            op_str = "-";
                            break;
                        case hir::HirUnaryOp::Not:
                            op_str = "!";
                            break;
                        case hir::HirUnaryOp::BitNot:
                            op_str = "~";
                            break;
                        default:
                            op_str = "-";
                            break;
                    }

                    std::string result_str = "(" + op_str + exprToString(operand) + ")";
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
                    result.type = Type::INT;  // TODO: 正確な型を推論
                    result.value = result_str;
                    return result;

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirMember>>) {
                    auto& mem = *e;
                    auto obj = convertExpression(*mem.object);

                    std::string result_str = exprToString(obj) + "." + mem.member;
                    Expression result;
                    result.kind = Expression::VARIABLE;
                    result.type = Type::INT;  // TODO: 正確な型を推論
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

                return Expression::Literal("0", Type::INT);
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
            case ast::TypeKind::Int:
                return Type::INT;
            case ast::TypeKind::Double:
                return Type::DOUBLE;
            case ast::TypeKind::String:
                return Type::STRING;
            default:
                return Type::INT;
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

            // 文字列補間の処理
            auto interpolated = processStringInterpolation(str_value, args);
            format_string = interpolated.first;
            printf_args = interpolated.second;
        } else {
            // 文字列でない場合は適切なフォーマット指定子を使用
            format_string = getFormatSpecifier(args[0].type);
            printf_args.push_back(args[0]);
        }

        // 改行を追加
        if (add_newline) {
            format_string += "\\n";
        }

        current_function->uses_printf = true;
        return Statement::PrintF(format_string, printf_args);
    }

    std::pair<std::string, std::vector<Expression>> processStringInterpolation(
        const std::string& str, [[maybe_unused]] const std::vector<Expression>& original_args) {
        std::string format_str;
        std::vector<Expression> args;

        // 簡単な文字列補間の処理（{変数名}の形式）
        std::regex interpolation_regex(R"(\{([^}]+)\})");
        std::smatch match;
        std::string remaining = str;

        while (std::regex_search(remaining, match, interpolation_regex)) {
            format_str += remaining.substr(0, match.position());

            std::string var_name = match[1].str();

            // フォーマット指定子の解析（例: {x:02d}）
            size_t colon_pos = var_name.find(':');
            if (colon_pos != std::string::npos) {
                std::string actual_var = var_name.substr(0, colon_pos);
                std::string format_spec = var_name.substr(colon_pos + 1);

                // フォーマット指定子をprintf形式に変換
                format_str += convertFormatSpec(format_spec);

                // 変数を引数に追加
                auto it = variable_types.find(actual_var);
                Type type = (it != variable_types.end()) ? it->second : Type::INT;
                args.push_back(Expression::Variable(actual_var, type));
            } else {
                // 単純な変数参照
                auto it = variable_types.find(var_name);
                Type type = (it != variable_types.end()) ? it->second : Type::INT;
                format_str += getFormatSpecifier(type);
                args.push_back(Expression::Variable(var_name, type));
            }

            remaining = match.suffix();
        }

        format_str += remaining;

        // 補間がない場合
        if (args.empty() && !str.empty()) {
            format_str = str;
        }

        return {format_str, args};
    }

    std::string convertFormatSpec(const std::string& spec) {
        // Cm形式からprintf形式への変換
        // 例: "02d" → "%02d", "02" → "%02d"
        if (spec.empty())
            return "%d";

        std::string result = "%";

        // 数字で始まる場合はパディング
        size_t i = 0;
        if (std::isdigit(spec[i])) {
            while (i < spec.length() && std::isdigit(spec[i])) {
                result += spec[i++];
            }
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
                case 'o':
                    result += "o";
                    break;
                case 'f':
                    result += "f";
                    break;
                case 'e':
                    result += "e";
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
            case Type::INT:
                return "%d";
            case Type::DOUBLE:
                return "%f";
            case Type::STRING:
                return "%s";
            case Type::BOOL:
                return "%s";  // true/false として出力
            case Type::CHAR_PTR:
                return "%s";
            default:
                return "%d";
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
};

}  // namespace cpp_mir
}  // namespace cm