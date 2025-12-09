#pragma once

#include "../../hir/hir_nodes.hpp"
#include "ts_mir.hpp"

#include <sstream>
#include <unordered_map>

namespace cm {
namespace ts_mir {

// ============================================================
// HIR → TS-MIR 変換器
// ============================================================
class HirToTsMirConverter {
   public:
    Program convert(const hir::HirProgram& hir_program) {
        Program program;

        // 各関数を変換
        for (const auto& decl : hir_program.declarations) {
            if (auto* func = std::get_if<std::unique_ptr<hir::HirFunction>>(&decl->kind)) {
                program.functions.push_back(convertFunction(**func));
            }
        }

        return program;
    }

   private:
    Function* current_function = nullptr;
    std::unordered_map<std::string, Type> variable_types;
    std::unordered_map<std::string, bool> variable_constness;

    Function convertFunction(const hir::HirFunction& hir_func) {
        Function func;
        func.name = hir_func.name;
        func.is_main = (hir_func.name == "main");
        func.return_type = convertType(hir_func.return_type);

        // パラメータ
        for (const auto& param : hir_func.params) {
            Type param_type = convertType(param.type);
            func.parameters.emplace_back(param_type, param.name);
            variable_types[param.name] = param_type;
            variable_constness[param.name] = false;
        }

        current_function = &func;
        variable_types.clear();
        variable_constness.clear();

        // 本体を変換
        for (const auto& stmt : hir_func.body) {
            convertStatement(*stmt, func.body);
        }

        current_function = nullptr;
        return func;
    }

    void convertStatement(const hir::HirStmt& stmt, std::vector<Statement>& body) {
        std::visit(
            [this, &body](const auto& s) {
                using T = std::decay_t<decltype(s)>;

                if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirLet>>) {
                    auto& let = *s;
                    Type type = convertType(let.type);
                    bool is_const = let.is_const;
                    variable_types[let.name] = type;
                    variable_constness[let.name] = is_const;

                    if (let.init) {
                        auto init_expr = convertExpression(*let.init);
                        body.push_back(Statement::Let(type, let.name, is_const, init_expr));
                    } else {
                        body.push_back(Statement::Let(type, let.name, is_const));
                    }

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirAssign>>) {
                    auto& assign = *s;
                    auto value = convertExpression(*assign.value);
                    body.push_back(Statement::Assign(assign.target, value));

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirReturn>>) {
                    auto& ret = *s;
                    if (ret.value) {
                        body.push_back(Statement::ReturnValue(convertExpression(*ret.value)));
                    } else {
                        body.push_back(Statement::ReturnVoid());
                    }

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirExprStmt>>) {
                    auto& expr_stmt = *s;

                    // 代入式かチェック
                    if (auto* bin =
                            std::get_if<std::unique_ptr<hir::HirBinary>>(&expr_stmt.expr->kind)) {
                        if ((*bin)->op == hir::HirBinaryOp::Assign) {
                            std::string target = extractTargetName(*(*bin)->lhs);
                            auto value = convertExpression(*(*bin)->rhs);
                            body.push_back(Statement::Assign(target, value));
                            return;
                        }
                    }

                    auto expr = convertExpression(*expr_stmt.expr);

                    // println/print の最適化 → console.log
                    if (expr.kind == Expression::CALL) {
                        if (expr.func_name == "println" || expr.func_name == "print") {
                            auto log_stmt = optimizePrintCall(expr.args);
                            body.push_back(log_stmt);
                            return;
                        }
                    }

                    body.push_back(Statement::Expr(expr));

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirIf>>) {
                    auto& if_stmt = *s;
                    std::vector<StatementPtr> then_body, else_body;

                    for (const auto& inner_stmt : if_stmt.then_block) {
                        std::vector<Statement> temp;
                        convertStatement(*inner_stmt, temp);
                        for (auto& st : temp) {
                            then_body.push_back(std::make_shared<Statement>(std::move(st)));
                        }
                    }
                    for (const auto& inner_stmt : if_stmt.else_block) {
                        std::vector<Statement> temp;
                        convertStatement(*inner_stmt, temp);
                        for (auto& st : temp) {
                            else_body.push_back(std::make_shared<Statement>(std::move(st)));
                        }
                    }

                    body.push_back(Statement::If(convertExpression(*if_stmt.cond),
                                                 std::move(then_body), std::move(else_body)));

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirLoop>>) {
                    auto& loop = *s;
                    std::vector<StatementPtr> loop_body;

                    for (const auto& inner_stmt : loop.body) {
                        std::vector<Statement> temp;
                        convertStatement(*inner_stmt, temp);
                        for (auto& st : temp) {
                            loop_body.push_back(std::make_shared<Statement>(std::move(st)));
                        }
                    }

                    // while (true) { ... }
                    body.push_back(Statement::WhileLoop(Expression::Literal("true", Type::BOOLEAN),
                                                        std::move(loop_body)));

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirWhile>>) {
                    auto& while_stmt = *s;
                    std::vector<StatementPtr> while_body;

                    for (const auto& inner_stmt : while_stmt.body) {
                        std::vector<Statement> temp;
                        convertStatement(*inner_stmt, temp);
                        for (auto& st : temp) {
                            while_body.push_back(std::make_shared<Statement>(std::move(st)));
                        }
                    }

                    body.push_back(Statement::WhileLoop(convertExpression(*while_stmt.cond),
                                                        std::move(while_body)));

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirFor>>) {
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

                    // condition
                    std::optional<Expression> cond_expr;
                    if (for_stmt.cond) {
                        cond_expr = convertExpression(*for_stmt.cond);
                    }

                    // update
                    StatementPtr update_ptr = nullptr;
                    if (for_stmt.update) {
                        auto update_expr = convertExpression(*for_stmt.update);
                        auto update_stmt = Statement::Expr(update_expr);
                        update_ptr = std::make_shared<Statement>(std::move(update_stmt));
                    }

                    // body
                    std::vector<StatementPtr> for_body;
                    for (const auto& inner_stmt : for_stmt.body) {
                        std::vector<Statement> temp;
                        convertStatement(*inner_stmt, temp);
                        for (auto& st : temp) {
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
                    auto& block = *s;
                    for (const auto& inner_stmt : block.stmts) {
                        convertStatement(*inner_stmt, body);
                    }

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirSwitch>>) {
                    // switchをif-elseに変換
                    convertSwitchToIfElse(*s, body);
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
                        // 文字列リテラルはそのまま保持（フォーマット処理はprintln変換時に行う）
                        // 名前付き変数補間 {var} のみテンプレートリテラルに変換
                        std::string content = *str_val;
                        if (hasNamedInterpolation(content)) {
                            return processToTemplateLiteral(content);
                        }
                        // 元の文字列を保持（エスケープはコード生成時に行う）
                        return Expression::Literal("\"" + *str_val + "\"", Type::STRING);
                    } else if (auto* int_val = std::get_if<int64_t>(&lit.value)) {
                        return Expression::Literal(std::to_string(*int_val), Type::NUMBER);
                    } else if (auto* bool_val = std::get_if<bool>(&lit.value)) {
                        return Expression::Literal(*bool_val ? "true" : "false", Type::BOOLEAN);
                    } else if (auto* double_val = std::get_if<double>(&lit.value)) {
                        return Expression::Literal(formatNumber(*double_val), Type::NUMBER);
                    } else if (auto* char_val = std::get_if<char>(&lit.value)) {
                        // char型は文字列として出力（TypeScriptにはcharがないため）
                        std::string char_str(1, *char_val);
                        return Expression::Literal("\"" + char_str + "\"", Type::STRING);
                    }
                    return Expression::Literal("0", Type::NUMBER);

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirVarRef>>) {
                    auto& var = *e;
                    auto it = variable_types.find(var.name);
                    Type type = (it != variable_types.end()) ? it->second : Type::NUMBER;
                    return Expression::Variable(var.name, type);

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirCall>>) {
                    auto& call = *e;
                    std::vector<Expression> args;
                    for (const auto& arg : call.args) {
                        args.push_back(convertExpression(*arg));
                    }
                    std::string func_name = extractFunctionName(call.func_name);
                    return Expression::Call(func_name, args);

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirBinary>>) {
                    auto& bin = *e;
                    auto lhs = convertExpression(*bin.lhs);
                    auto rhs = convertExpression(*bin.rhs);

                    std::string op_str = convertBinaryOp(bin.op);

                    // 整数除算の特別処理
                    if (bin.op == hir::HirBinaryOp::Div && lhs.type == Type::NUMBER &&
                        rhs.type == Type::NUMBER) {
                        // Math.floor() でラップ
                        std::string result = "Math.floor(" + exprToString(lhs) + " " + op_str +
                                             " " + exprToString(rhs) + ")";
                        return Expression::BinaryOp(result, Type::NUMBER);
                    }

                    std::string result =
                        "(" + exprToString(lhs) + " " + op_str + " " + exprToString(rhs) + ")";
                    Type result_type = inferBinaryType(bin.op, lhs.type, rhs.type);
                    return Expression::BinaryOp(result, result_type);

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirUnary>>) {
                    auto& unary = *e;
                    auto operand = convertExpression(*unary.operand);
                    std::string result = convertUnaryOp(unary.op, operand);
                    return Expression::BinaryOp(result, operand.type);

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirIndex>>) {
                    auto& idx = *e;
                    auto obj = convertExpression(*idx.object);
                    auto index = convertExpression(*idx.index);
                    std::string result = exprToString(obj) + "[" + exprToString(index) + "]";
                    Expression expr;
                    expr.kind = Expression::VARIABLE;
                    expr.type = Type::ANY;
                    expr.value = result;
                    return expr;

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirMember>>) {
                    auto& mem = *e;
                    auto obj = convertExpression(*mem.object);
                    std::string result = exprToString(obj) + "." + mem.member;
                    Expression expr;
                    expr.kind = Expression::VARIABLE;
                    expr.type = Type::ANY;
                    expr.value = result;
                    return expr;

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirTernary>>) {
                    auto& tern = *e;
                    auto cond = convertExpression(*tern.condition);
                    auto then_expr = convertExpression(*tern.then_expr);
                    auto else_expr = convertExpression(*tern.else_expr);

                    std::string result = "(" + exprToString(cond) + " ? " +
                                         exprToString(then_expr) + " : " + exprToString(else_expr) +
                                         ")";
                    return Expression::BinaryOp(result, then_expr.type);
                }

                return Expression::Literal("0", Type::NUMBER);
            },
            expr.kind);
    }

    Type convertType(const hir::TypePtr& hir_type) {
        if (!hir_type)
            return Type::VOID;

        switch (hir_type->kind) {
            case ast::TypeKind::Void:
                return Type::VOID;
            case ast::TypeKind::Bool:
                return Type::BOOLEAN;
            case ast::TypeKind::Int:
            case ast::TypeKind::Double:
                return Type::NUMBER;
            case ast::TypeKind::String:
                return Type::STRING;
            default:
                return Type::ANY;
        }
    }

    Statement optimizePrintCall(const std::vector<Expression>& args) {
        if (args.empty()) {
            return Statement::Log({});
        }

        // 最初の引数が文字列リテラルの場合、テンプレートリテラルに変換
        if (args[0].kind == Expression::LITERAL && args[0].type == Type::STRING) {
            std::string str_val = args[0].value;
            // 引用符を除去
            if (str_val.length() >= 2 && str_val[0] == '"' && str_val.back() == '"') {
                str_val = str_val.substr(1, str_val.length() - 2);
            }

            // フォーマット文字列（位置引数または名前付き変数）を処理
            if (hasFormatPlaceholders(str_val)) {
                auto result = processFormatStringForConsole(str_val, args);
                return Statement::Log(result);
            }
        }

        return Statement::Log(args);
    }

    // フォーマットプレースホルダー（{}、{:x}、{var}など）があるかチェック
    bool hasFormatPlaceholders(const std::string& str) {
        size_t pos = 0;
        while ((pos = str.find('{', pos)) != std::string::npos) {
            // エスケープされた {{ はスキップ
            if (pos + 1 < str.size() && str[pos + 1] == '{') {
                pos += 2;
                continue;
            }
            size_t end = str.find('}', pos);
            if (end != std::string::npos) {
                return true;
            }
            pos++;
        }
        return false;
    }

    // 名前付き変数補間 {var} があるかチェック（位置引数 {} はチェックしない）
    bool hasNamedInterpolation(const std::string& str) {
        size_t pos = 0;
        while ((pos = str.find('{', pos)) != std::string::npos) {
            // エスケープされた {{ はスキップ
            if (pos + 1 < str.size() && str[pos + 1] == '{') {
                pos += 2;
                continue;
            }
            size_t end = str.find('}', pos);
            if (end != std::string::npos && end > pos + 1) {
                // 内容がある場合
                std::string content = str.substr(pos + 1, end - pos - 1);
                // : で始まるフォーマット指定子のみの場合はスキップ
                if (!content.empty() && content[0] != ':') {
                    // 変数名が含まれている（真の名前付き補間）
                    return true;
                }
            }
            pos++;
        }
        return false;
    }

    // フォーマット文字列をTypeScriptのテンプレートリテラルに変換
    // 位置引数 {} と名前付き変数 {var} をサポート
    std::vector<Expression> processFormatStringForConsole(
        const std::string& str, const std::vector<Expression>& original_args) {
        std::string template_str = "`";
        size_t arg_index = 1;  // 引数[0]はフォーマット文字列自体
        size_t i = 0;

        while (i < str.length()) {
            // エスケープされた {{ と }}
            if (str[i] == '{' && i + 1 < str.length() && str[i + 1] == '{') {
                template_str += "{";
                i += 2;
                continue;
            }
            if (str[i] == '}' && i + 1 < str.length() && str[i + 1] == '}') {
                template_str += "}";
                i += 2;
                continue;
            }

            if (str[i] == '{') {
                size_t end = str.find('}', i);
                if (end != std::string::npos) {
                    std::string content = str.substr(i + 1, end - i - 1);

                    if (content.empty()) {
                        // {} - 位置引数
                        if (arg_index < original_args.size()) {
                            template_str +=
                                "${" + exprToStringForTemplate(original_args[arg_index]) + "}";
                            arg_index++;
                        }
                    } else if (content[0] == ':') {
                        // {:spec} - フォーマット指定子付き位置引数
                        std::string spec = content.substr(1);
                        if (arg_index < original_args.size()) {
                            template_str +=
                                "${" + formatWithSpecAndExpr(original_args[arg_index], spec) + "}";
                            arg_index++;
                        }
                    } else {
                        // {var} または {var:spec} - 名前付き変数
                        size_t colon = content.find(':');
                        std::string var_name =
                            (colon != std::string::npos) ? content.substr(0, colon) : content;
                        std::string spec =
                            (colon != std::string::npos) ? content.substr(colon + 1) : "";

                        if (!var_name.empty()) {
                            if (!spec.empty()) {
                                template_str += "${" + formatWithSpec(var_name, spec) + "}";
                            } else {
                                template_str += "${" + var_name + "}";
                            }
                        }
                    }
                    i = end + 1;
                    continue;
                }
            }

            // バッククォートとドル記号をエスケープ
            if (str[i] == '`') {
                template_str += "\\`";
            } else if (str[i] == '$') {
                template_str += "\\$";
            } else {
                template_str += str[i];
            }
            i++;
        }

        template_str += "`";

        std::vector<Expression> result;
        result.push_back(Expression::TemplateLiteral(template_str));
        return result;
    }

    // 式をテンプレートリテラル内で使用できる文字列に変換
    std::string exprToStringForTemplate(const Expression& expr) {
        switch (expr.kind) {
            case Expression::LITERAL:
                // テンプレートリテラル内では式をそのまま返す
                // 数値リテラルは括弧で囲む（メソッド呼び出しが可能になるように）
                if (expr.type == Type::NUMBER) {
                    return "(" + expr.value + ")";
                }
                return expr.value;
            case Expression::VARIABLE:
            case Expression::BINARY_OP:
            case Expression::UNARY_OP:
                return expr.value;
            case Expression::CALL: {
                std::string result = expr.func_name + "(";
                for (size_t i = 0; i < expr.args.size(); ++i) {
                    if (i > 0)
                        result += ", ";
                    result += exprToString(expr.args[i]);
                }
                result += ")";
                return result;
            }
            default:
                return expr.value;
        }
    }

    // フォーマット指定子と式を組み合わせる
    std::string formatWithSpecAndExpr(const Expression& expr, const std::string& spec) {
        std::string expr_str = exprToStringForTemplate(expr);

        if (spec == "x") {
            return expr_str + ".toString(16)";
        } else if (spec == "X") {
            return expr_str + ".toString(16).toUpperCase()";
        } else if (spec == "b") {
            return expr_str + ".toString(2)";
        } else if (spec == "o") {
            return expr_str + ".toString(8)";
        } else if (spec == "e") {
            return expr_str + ".toExponential()";
        } else if (spec == "E") {
            return expr_str + ".toExponential().toUpperCase()";
        } else if (spec.length() > 0 && spec[0] == '.') {
            // 精度指定 .N
            std::string precision = spec.substr(1);
            return expr_str + ".toFixed(" + precision + ")";
        } else if (spec.length() > 1 && spec[0] == '<') {
            // 左寄せ <N
            std::string width = spec.substr(1);
            return expr_str + ".toString().padEnd(" + width + ")";
        } else if (spec.length() > 1 && spec[0] == '>') {
            // 右寄せ >N
            std::string width = spec.substr(1);
            return expr_str + ".toString().padStart(" + width + ")";
        } else if (spec.length() > 1 && spec[0] == '^') {
            // 中央寄せ ^N (JavaScriptでは近似)
            std::string width = spec.substr(1);
            return "(() => { const s = " + expr_str + ".toString(); const pad = " + width +
                   " - s.length; return pad > 0 ? ' '.repeat(Math.floor(pad/2)) + s + ' "
                   "'.repeat(Math.ceil(pad/2)) : s; })()";
        } else if (spec.length() > 2 && spec[0] == '0' && spec[1] == '>') {
            // ゼロパディング 0>N
            std::string width = spec.substr(2);
            return expr_str + ".toString().padStart(" + width + ", '0')";
        }
        return expr_str;
    }

    // 既存のhasInterpolation関数は使わないが、互換性のため残す
    bool hasInterpolation(const std::string& str) { return hasFormatPlaceholders(str); }

    Expression processToTemplateLiteral(const std::string& str) {
        std::string result = "`";
        size_t i = 0;

        while (i < str.length()) {
            // エスケープされたブレース
            if (str[i] == '{' && i + 1 < str.length() && str[i + 1] == '{') {
                result += "{";
                i += 2;
                continue;
            }
            if (str[i] == '}' && i + 1 < str.length() && str[i + 1] == '}') {
                result += "}";
                i += 2;
                continue;
            }

            if (str[i] == '{') {
                size_t end = str.find('}', i);
                if (end != std::string::npos) {
                    std::string content = str.substr(i + 1, end - i - 1);

                    // フォーマット指定子を処理
                    size_t colon = content.find(':');
                    std::string var_name =
                        (colon != std::string::npos) ? content.substr(0, colon) : content;
                    std::string spec =
                        (colon != std::string::npos) ? content.substr(colon + 1) : "";

                    if (!var_name.empty()) {
                        result += "${";
                        if (!spec.empty()) {
                            result += formatWithSpec(var_name, spec);
                        } else {
                            result += var_name;
                        }
                        result += "}";
                    }
                    i = end + 1;
                    continue;
                }
            }

            // バッククォートとドル記号をエスケープ
            if (str[i] == '`') {
                result += "\\`";
            } else if (str[i] == '$') {
                result += "\\$";
            } else {
                result += str[i];
            }
            i++;
        }

        result += "`";
        return Expression::TemplateLiteral(result);
    }

    std::vector<Expression> processInterpolationForConsole(
        const std::string& str, const std::vector<Expression>& original_args) {
        return processFormatStringForConsole(str, original_args);
    }

    std::string formatWithSpec(const std::string& var_name, const std::string& spec) {
        // フォーマット指定子をJavaScript形式に変換
        if (spec == "x") {
            return var_name + ".toString(16)";
        } else if (spec == "X") {
            return var_name + ".toString(16).toUpperCase()";
        } else if (spec == "b") {
            return var_name + ".toString(2)";
        } else if (spec == "o") {
            return var_name + ".toString(8)";
        } else if (spec.length() > 0 && spec[0] == '.') {
            // 精度指定
            std::string precision = spec.substr(1);
            return var_name + ".toFixed(" + precision + ")";
        } else if (spec.length() > 2 && spec[0] == '0' && spec[1] == '>') {
            // ゼロパディング
            std::string width = spec.substr(2);
            return var_name + ".toString().padStart(" + width + ", '0')";
        }
        return var_name;
    }

    void convertSwitchToIfElse(const hir::HirSwitch& sw, std::vector<Statement>& body) {
        auto switch_expr = convertExpression(*sw.expr);
        std::string expr_str = exprToString(switch_expr);

        std::vector<StatementPtr> current_else;

        for (size_t i = sw.cases.size(); i > 0; --i) {
            const auto& case_stmt = sw.cases[i - 1];

            std::vector<StatementPtr> case_body;
            for (const auto& inner_stmt : case_stmt.stmts) {
                std::vector<Statement> temp;
                convertStatement(*inner_stmt, temp);
                for (auto& st : temp) {
                    case_body.push_back(std::make_shared<Statement>(std::move(st)));
                }
            }

            // default case: patternもvalueもnullの場合
            bool is_default = !case_stmt.pattern && !case_stmt.value;
            if (is_default) {
                current_else = std::move(case_body);
            } else {
                std::string cond_str;
                if (case_stmt.pattern) {
                    cond_str = generatePatternCondition(expr_str, *case_stmt.pattern);
                } else if (case_stmt.value) {
                    auto val = convertExpression(*case_stmt.value);
                    cond_str = expr_str + " === " + exprToString(val);
                }

                auto cond = Expression::BinaryOp(cond_str, Type::BOOLEAN);
                auto if_stmt = Statement::If(cond, std::move(case_body), std::move(current_else));
                current_else.clear();
                current_else.push_back(std::make_shared<Statement>(std::move(if_stmt)));
            }
        }

        if (!current_else.empty()) {
            for (auto& st : current_else) {
                body.push_back(Statement());
                body.back() = *st;
            }
        }
    }

    std::string generatePatternCondition(const std::string& expr_str,
                                         const hir::HirSwitchPattern& pattern) {
        switch (pattern.kind) {
            case hir::HirSwitchPattern::SingleValue:
                if (pattern.value) {
                    auto val = convertExpression(*pattern.value);
                    return expr_str + " === " + exprToString(val);
                }
                return "true";
            case hir::HirSwitchPattern::Range:
                if (pattern.range_start && pattern.range_end) {
                    auto start = convertExpression(*pattern.range_start);
                    auto end = convertExpression(*pattern.range_end);
                    return "(" + expr_str + " >= " + exprToString(start) + " && " + expr_str +
                           " <= " + exprToString(end) + ")";
                }
                return "true";
            case hir::HirSwitchPattern::Or: {
                std::string result = "(";
                bool first = true;
                for (const auto& sub : pattern.or_patterns) {
                    if (!first)
                        result += " || ";
                    first = false;
                    result += generatePatternCondition(expr_str, *sub);
                }
                result += ")";
                return result;
            }
            default:
                return "true";
        }
    }

    std::string convertBinaryOp(hir::HirBinaryOp op) {
        switch (op) {
            case hir::HirBinaryOp::Add:
                return "+";
            case hir::HirBinaryOp::Sub:
                return "-";
            case hir::HirBinaryOp::Mul:
                return "*";
            case hir::HirBinaryOp::Div:
                return "/";
            case hir::HirBinaryOp::Mod:
                return "%";
            case hir::HirBinaryOp::Eq:
                return "===";  // TypeScriptでは === を使用
            case hir::HirBinaryOp::Ne:
                return "!==";
            case hir::HirBinaryOp::Lt:
                return "<";
            case hir::HirBinaryOp::Gt:
                return ">";
            case hir::HirBinaryOp::Le:
                return "<=";
            case hir::HirBinaryOp::Ge:
                return ">=";
            case hir::HirBinaryOp::And:
                return "&&";
            case hir::HirBinaryOp::Or:
                return "||";
            case hir::HirBinaryOp::BitAnd:
                return "&";
            case hir::HirBinaryOp::BitOr:
                return "|";
            case hir::HirBinaryOp::BitXor:
                return "^";
            case hir::HirBinaryOp::Shl:
                return "<<";
            case hir::HirBinaryOp::Shr:
                return ">>";
            case hir::HirBinaryOp::Assign:
                return "=";
            default:
                return "+";
        }
    }

    std::string convertUnaryOp(hir::HirUnaryOp op, const Expression& operand) {
        std::string op_str = exprToString(operand);
        switch (op) {
            case hir::HirUnaryOp::Neg:
                return "(-" + op_str + ")";
            case hir::HirUnaryOp::Not:
                return "(!" + op_str + ")";
            case hir::HirUnaryOp::BitNot:
                return "(~" + op_str + ")";
            case hir::HirUnaryOp::PreInc:
                return "(++" + op_str + ")";
            case hir::HirUnaryOp::PreDec:
                return "(--" + op_str + ")";
            case hir::HirUnaryOp::PostInc:
                return "(" + op_str + "++)";
            case hir::HirUnaryOp::PostDec:
                return "(" + op_str + "--)";
            default:
                return op_str;
        }
    }

    Type inferBinaryType(hir::HirBinaryOp op, Type lhs, Type rhs) {
        switch (op) {
            case hir::HirBinaryOp::Eq:
            case hir::HirBinaryOp::Ne:
            case hir::HirBinaryOp::Lt:
            case hir::HirBinaryOp::Gt:
            case hir::HirBinaryOp::Le:
            case hir::HirBinaryOp::Ge:
            case hir::HirBinaryOp::And:
            case hir::HirBinaryOp::Or:
                return Type::BOOLEAN;
            case hir::HirBinaryOp::Add:
                if (lhs == Type::STRING || rhs == Type::STRING) {
                    return Type::STRING;
                }
                return Type::NUMBER;
            default:
                return lhs;
        }
    }

    std::string extractTargetName(const hir::HirExpr& expr) {
        if (auto* var = std::get_if<std::unique_ptr<hir::HirVarRef>>(&expr.kind)) {
            return (*var)->name;
        } else if (auto* idx = std::get_if<std::unique_ptr<hir::HirIndex>>(&expr.kind)) {
            return exprToString(convertExpression(*(*idx)->object)) + "[" +
                   exprToString(convertExpression(*(*idx)->index)) + "]";
        } else if (auto* mem = std::get_if<std::unique_ptr<hir::HirMember>>(&expr.kind)) {
            return exprToString(convertExpression(*(*mem)->object)) + "." + (*mem)->member;
        }
        return "unknown";
    }

    std::string extractFunctionName(const std::string& qualified) {
        size_t last = qualified.rfind("::");
        if (last != std::string::npos) {
            return qualified.substr(last + 2);
        }
        return qualified;
    }

    std::string exprToString(const Expression& expr) {
        switch (expr.kind) {
            case Expression::LITERAL:
            case Expression::VARIABLE:
            case Expression::BINARY_OP:
            case Expression::UNARY_OP:
            case Expression::TEMPLATE_LIT:
                return expr.value;
            case Expression::CALL: {
                std::string result = expr.func_name + "(";
                for (size_t i = 0; i < expr.args.size(); ++i) {
                    if (i > 0)
                        result += ", ";
                    result += exprToString(expr.args[i]);
                }
                result += ")";
                return result;
            }
            default:
                return expr.value;
        }
    }

    std::string escapeTsString(const std::string& str) {
        std::string result;
        size_t i = 0;
        while (i < str.length()) {
            // Cm言語の {{ → { と }} → } のエスケープを処理
            if (str[i] == '{' && i + 1 < str.length() && str[i + 1] == '{') {
                result += '{';
                i += 2;
                continue;
            }
            if (str[i] == '}' && i + 1 < str.length() && str[i + 1] == '}') {
                result += '}';
                i += 2;
                continue;
            }

            switch (str[i]) {
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
                    result += str[i];
                    break;
            }
            i++;
        }
        return result;
    }

    std::string formatNumber(double val) {
        std::ostringstream oss;
        oss << std::fixed << val;
        std::string str = oss.str();
        size_t dot = str.find('.');
        if (dot != std::string::npos) {
            size_t last = str.find_last_not_of('0');
            if (last > dot) {
                str = str.substr(0, last + 1);
            } else {
                str = str.substr(0, dot + 2);
            }
        }
        return str;
    }
};

}  // namespace ts_mir
}  // namespace cm
