#pragma once

#include "../../hir/hir_nodes.hpp"
#include "rust_mir.hpp"

#include <sstream>
#include <unordered_map>

namespace cm {
namespace rust_mir {

// ============================================================
// HIR → Rust-MIR 変換器
// ============================================================
class HirToRustMirConverter {
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
    std::unordered_map<std::string, bool> variable_mutability;

    Function convertFunction(const hir::HirFunction& hir_func) {
        Function func;
        func.name = hir_func.name;
        func.is_main = (hir_func.name == "main");

        // 戻り値型
        func.return_type = convertType(hir_func.return_type);

        // パラメータ
        for (const auto& param : hir_func.params) {
            Type param_type = convertType(param.type);
            func.parameters.emplace_back(param_type, param.name);
            variable_types[param.name] = param_type;
            variable_mutability[param.name] = false;
        }

        current_function = &func;
        variable_types.clear();
        variable_mutability.clear();

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
                    bool is_mut = !let.is_const;
                    variable_types[let.name] = type;
                    variable_mutability[let.name] = is_mut;

                    if (let.init) {
                        auto init_expr = convertExpression(*let.init);
                        // float型の場合、リテラルのサフィックスを調整
                        if (type == Type::F32 && init_expr.kind == Expression::LITERAL &&
                            init_expr.type == Type::F64) {
                            // _f64 を _f32 に置換
                            std::string val = init_expr.value;
                            size_t pos = val.rfind("_f64");
                            if (pos != std::string::npos) {
                                val = val.substr(0, pos) + "_f32";
                            }
                            init_expr.value = val;
                            init_expr.type = Type::F32;
                        }
                        body.push_back(Statement::Let(type, let.name, is_mut, init_expr));
                    } else {
                        body.push_back(Statement::Let(type, let.name, is_mut));
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

                    // println/print の最適化
                    if (expr.kind == Expression::CALL || expr.kind == Expression::MACRO_CALL) {
                        if (expr.func_name == "println" || expr.func_name == "print") {
                            auto println_stmt = optimizePrintCall(expr.func_name, expr.args);
                            body.push_back(println_stmt);
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

                    body.push_back(Statement::InfiniteLoop(std::move(loop_body)));

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

                    // Rust風forループへの変換を試みる
                    // 単純な for (int i = 0; i < n; i++) 形式の場合のみ
                    if (tryConvertToRustFor(for_stmt, body)) {
                        return;
                    }

                    // それ以外はwhileループに変換
                    convertForToWhile(for_stmt, body);

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
                    // switchをif-elseチェインに変換
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
                        // 文字列補間をチェック
                        if (hasInterpolation(*str_val)) {
                            return processStringInterpolation(*str_val);
                        }
                        // エスケープされたブレースを変換 ({{ -> {, }} -> })
                        std::string processed = processEscapedBraces(*str_val);
                        std::string escaped = escapeRustString(processed);
                        return Expression::Literal("\"" + escaped + "\"", Type::STR_SLICE);
                    } else if (auto* int_val = std::get_if<int64_t>(&lit.value)) {
                        return Expression::Literal(std::to_string(*int_val), Type::I32);
                    } else if (auto* bool_val = std::get_if<bool>(&lit.value)) {
                        return Expression::Literal(*bool_val ? "true" : "false", Type::BOOL);
                    } else if (auto* double_val = std::get_if<double>(&lit.value)) {
                        return Expression::Literal(formatFloat(*double_val), Type::F64);
                    } else if (auto* char_val = std::get_if<char>(&lit.value)) {
                        // Rustのchar型はシングルクォートで表現
                        std::string char_str = "'";
                        char_str += *char_val;
                        char_str += "'";
                        return Expression::Literal(char_str, Type::CHAR);
                    }
                    return Expression::Literal("0", Type::I32);

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirVarRef>>) {
                    auto& var = *e;
                    auto it = variable_types.find(var.name);
                    Type type = (it != variable_types.end()) ? it->second : Type::I32;
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

                    // 文字列連結の特別処理
                    if (bin.op == hir::HirBinaryOp::Add &&
                        (lhs.type == Type::STRING || lhs.type == Type::STR_SLICE ||
                         lhs.type == Type::STR_REF || rhs.type == Type::STRING ||
                         rhs.type == Type::STR_SLICE || rhs.type == Type::STR_REF)) {
                        // Rustではformat!マクロを使用
                        return createStringConcat(lhs, rhs);
                    }

                    std::string op_str = convertBinaryOp(bin.op);
                    std::string result =
                        "(" + exprToString(lhs) + " " + op_str + " " + exprToString(rhs) + ")";

                    // 結果の型を推論
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
                    expr.type = Type::I32;
                    expr.value = result;
                    return expr;

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirMember>>) {
                    auto& mem = *e;
                    auto obj = convertExpression(*mem.object);
                    std::string result = exprToString(obj) + "." + mem.member;
                    Expression expr;
                    expr.kind = Expression::VARIABLE;
                    expr.type = Type::I32;
                    expr.value = result;
                    return expr;

                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirTernary>>) {
                    auto& tern = *e;
                    auto cond = convertExpression(*tern.condition);
                    auto then_expr = convertExpression(*tern.then_expr);
                    auto else_expr = convertExpression(*tern.else_expr);

                    // Rust風: if cond { then } else { else }
                    std::string result = "if " + exprToString(cond) + " { " +
                                         exprToString(then_expr) + " } else { " +
                                         exprToString(else_expr) + " }";
                    return Expression::BinaryOp(result, then_expr.type);
                }

                return Expression::Literal("0", Type::I32);
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
                return Type::BOOL;
            case ast::TypeKind::Char:
                return Type::CHAR;
            case ast::TypeKind::Tiny:
                return Type::I8;
            case ast::TypeKind::Short:
                return Type::I16;
            case ast::TypeKind::Int:
                return Type::I32;
            case ast::TypeKind::Long:
                return Type::I64;
            case ast::TypeKind::UTiny:
                return Type::U8;
            case ast::TypeKind::UShort:
                return Type::U16;
            case ast::TypeKind::UInt:
                return Type::U32;
            case ast::TypeKind::ULong:
                return Type::U64;
            case ast::TypeKind::Float:
                return Type::F32;
            case ast::TypeKind::Double:
                return Type::F64;
            case ast::TypeKind::String:
                return Type::STRING;
            default:
                return Type::I32;
        }
    }

    Statement optimizePrintCall(const std::string& func_name, const std::vector<Expression>& args) {
        bool add_newline = (func_name == "println");

        if (args.empty()) {
            return Statement::PrintLn("", {}, add_newline);
        }

        // 最初の引数が文字列リテラルかチェック
        if (args[0].kind == Expression::LITERAL &&
            (args[0].type == Type::STRING || args[0].type == Type::STR_SLICE ||
             args[0].type == Type::STR_REF)) {
            std::string str_val = args[0].value;
            // 引用符を除去
            if (str_val.length() >= 2 && str_val[0] == '"' && str_val.back() == '"') {
                str_val = str_val.substr(1, str_val.length() - 2);
            }

            // 文字列補間の処理
            auto result = processStringInterpolation(str_val, args);
            return Statement::PrintLn(result.first, result.second, add_newline);
        }

        // 文字列でない場合は全ての引数を{}で出力
        std::string format;
        for (size_t i = 0; i < args.size(); ++i) {
            format += "{}";
        }
        return Statement::PrintLn(format, args, add_newline);
    }

    std::pair<std::string, std::vector<Expression>> processStringInterpolation(
        const std::string& str, const std::vector<Expression>& original_args) {
        std::string format_str;
        std::vector<Expression> args;
        size_t arg_index = 1;

        size_t i = 0;
        while (i < str.length()) {
            // エスケープされたブレース（元の {{, }} は既に { } に変換されている可能性がある）
            // ここでは単独の { または } をエスケープする必要がある
            if (str[i] == '{') {
                // 次の文字が { なら既にエスケープ済み
                if (i + 1 < str.length() && str[i + 1] == '{') {
                    format_str += "{{";
                    i += 2;
                    continue;
                }
                // 補間パターンの開始かチェック
                size_t end = str.find('}', i);
                if (end != std::string::npos && end > i + 1) {
                    // {content} の形式
                    std::string placeholder = str.substr(i + 1, end - i - 1);

                    if (placeholder.empty()) {
                        // {}の場合 - 位置引数
                        if (arg_index < original_args.size()) {
                            format_str += "{}";
                            args.push_back(original_args[arg_index++]);
                        }
                    } else if (placeholder[0] == ':') {
                        // {:x}形式 - フォーマット指定子
                        std::string rust_spec = convertToRustFormat(placeholder.substr(1));
                        format_str += "{" + rust_spec + "}";
                        if (arg_index < original_args.size()) {
                            args.push_back(original_args[arg_index++]);
                        }
                    } else if (placeholder.find(':') != std::string::npos) {
                        // {var:spec}形式
                        size_t colon = placeholder.find(':');
                        std::string var_name = placeholder.substr(0, colon);
                        std::string spec = placeholder.substr(colon + 1);

                        auto it = variable_types.find(var_name);
                        if (it != variable_types.end()) {
                            std::string rust_spec = convertToRustFormat(spec);
                            format_str += "{" + rust_spec + "}";
                            args.push_back(Expression::Variable(var_name, it->second));
                        } else {
                            // 変数が見つからない - リテラルとしてエスケープ
                            format_str += "{{" + placeholder + "}}";
                        }
                    } else {
                        // {var}形式 - 変数参照
                        auto it = variable_types.find(placeholder);
                        if (it != variable_types.end()) {
                            format_str += "{}";
                            args.push_back(Expression::Variable(placeholder, it->second));
                        } else {
                            // 変数が見つからない - リテラルとしてエスケープ
                            format_str += "{{" + placeholder + "}}";
                        }
                    }
                    i = end + 1;
                    continue;
                } else if (end == i + 1) {
                    // {} - 空のプレースホルダー
                    if (arg_index < original_args.size()) {
                        format_str += "{}";
                        args.push_back(original_args[arg_index++]);
                    }
                    i = end + 1;
                    continue;
                } else {
                    // 対応する } がない - リテラルの { としてエスケープ
                    format_str += "{{";
                    i++;
                    continue;
                }
            }

            if (str[i] == '}') {
                // 次の文字が } なら既にエスケープ済み
                if (i + 1 < str.length() && str[i + 1] == '}') {
                    format_str += "}}";
                    i += 2;
                    continue;
                }
                // 単独の } はエスケープ
                format_str += "}}";
                i++;
                continue;
            }

            format_str += str[i];
            i++;
        }

        return {format_str, args};
    }

    std::string convertToRustFormat(const std::string& spec) {
        // Cmフォーマット → Rustフォーマット変換
        if (spec == "x")
            return ":x";
        if (spec == "X")
            return ":X";
        if (spec == "o")
            return ":o";
        if (spec == "b")
            return ":b";
        if (spec.length() > 0 && spec[0] == '.') {
            // 精度指定 .N → :.Nf
            return ":" + spec;
        }
        if (spec.length() > 2 && spec[0] == '0' && spec[1] == '>') {
            // ゼロパディング 0>N → :0N
            return ":0" + spec.substr(2);
        }
        if (spec.length() > 1 && spec[0] == '<') {
            // 左寄せ <N → :<N
            return ":<" + spec.substr(1);
        }
        if (spec.length() > 1 && spec[0] == '>') {
            // 右寄せ >N → :>N
            return ":>" + spec.substr(1);
        }
        if (spec.length() > 1 && spec[0] == '^') {
            // 中央寄せ ^N → :^N
            return ":^" + spec.substr(1);
        }
        return ":" + spec;
    }

    bool tryConvertToRustFor(const hir::HirFor& for_stmt, std::vector<Statement>& body) {
        // 単純なforループのパターンを検出
        // for (int i = 0; i < n; i++) → for i in 0..n
        // for (int i = 0; i <= n; i++) → for i in 0..=n

        if (!for_stmt.init || !for_stmt.cond || !for_stmt.update) {
            return false;
        }

        // 初期化が let i = 0 形式かチェック
        std::string loop_var;
        int64_t start_val = 0;

        if (auto* let = std::get_if<std::unique_ptr<hir::HirLet>>(&for_stmt.init->kind)) {
            loop_var = (*let)->name;
            if ((*let)->init) {
                if (auto* lit =
                        std::get_if<std::unique_ptr<hir::HirLiteral>>(&(*let)->init->kind)) {
                    if (auto* int_val = std::get_if<int64_t>(&(*lit)->value)) {
                        start_val = *int_val;
                    }
                }
            }
        } else {
            return false;
        }

        // 条件が i < n または i <= n 形式かチェック
        Expression end_expr;
        bool inclusive = false;

        if (auto* bin = std::get_if<std::unique_ptr<hir::HirBinary>>(&for_stmt.cond->kind)) {
            if ((*bin)->op != hir::HirBinaryOp::Lt && (*bin)->op != hir::HirBinaryOp::Le) {
                return false;
            }
            inclusive = ((*bin)->op == hir::HirBinaryOp::Le);

            // 左辺が loop_var かチェック
            if (auto* var = std::get_if<std::unique_ptr<hir::HirVarRef>>(&(*bin)->lhs->kind)) {
                if ((*var)->name != loop_var) {
                    return false;
                }
            } else {
                return false;
            }

            end_expr = convertExpression(*(*bin)->rhs);
        } else {
            return false;
        }

        // 更新が i++ 形式かチェック
        if (auto* unary = std::get_if<std::unique_ptr<hir::HirUnary>>(&for_stmt.update->kind)) {
            if ((*unary)->op != hir::HirUnaryOp::PostInc &&
                (*unary)->op != hir::HirUnaryOp::PreInc) {
                return false;
            }
            if (auto* var =
                    std::get_if<std::unique_ptr<hir::HirVarRef>>(&(*unary)->operand->kind)) {
                if ((*var)->name != loop_var) {
                    return false;
                }
            }
        } else {
            return false;
        }

        // Rust風forループを生成
        variable_types[loop_var] = Type::I32;
        variable_mutability[loop_var] = false;

        std::vector<StatementPtr> for_body;
        for (const auto& inner_stmt : for_stmt.body) {
            std::vector<Statement> temp;
            convertStatement(*inner_stmt, temp);
            for (auto& st : temp) {
                for_body.push_back(std::make_shared<Statement>(std::move(st)));
            }
        }

        body.push_back(
            Statement::ForRange(loop_var, Expression::Literal(std::to_string(start_val), Type::I32),
                                end_expr, inclusive, std::move(for_body)));

        return true;
    }

    void convertForToWhile(const hir::HirFor& for_stmt, std::vector<Statement>& body) {
        // 初期化
        if (for_stmt.init) {
            convertStatement(*for_stmt.init, body);
        }

        // 条件
        Expression cond;
        if (for_stmt.cond) {
            cond = convertExpression(*for_stmt.cond);
        } else {
            cond = Expression::Literal("true", Type::BOOL);
        }

        // 本体 + 更新
        std::vector<StatementPtr> while_body;
        for (const auto& inner_stmt : for_stmt.body) {
            std::vector<Statement> temp;
            convertStatement(*inner_stmt, temp);
            for (auto& st : temp) {
                while_body.push_back(std::make_shared<Statement>(std::move(st)));
            }
        }

        // 更新式
        if (for_stmt.update) {
            auto update_expr = convertExpression(*for_stmt.update);
            while_body.push_back(std::make_shared<Statement>(Statement::Expr(update_expr)));
        }

        body.push_back(Statement::WhileLoop(cond, std::move(while_body)));
    }

    void convertSwitchToIfElse(const hir::HirSwitch& sw, std::vector<Statement>& body) {
        auto switch_expr = convertExpression(*sw.expr);
        std::string expr_str = exprToString(switch_expr);

        // 各caseをif-else if-elseに変換
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
                    cond_str = expr_str + " == " + exprToString(val);
                }

                auto cond = Expression::BinaryOp(cond_str, Type::BOOL);
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
                    return expr_str + " == " + exprToString(val);
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
                return "==";
            case hir::HirBinaryOp::Ne:
                return "!=";
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
                return "(!" + op_str + ")";
            case hir::HirUnaryOp::PreInc:
                return "{ " + op_str + " += 1; " + op_str + " }";
            case hir::HirUnaryOp::PreDec:
                return "{ " + op_str + " -= 1; " + op_str + " }";
            case hir::HirUnaryOp::PostInc:
                return "{ let _t = " + op_str + "; " + op_str + " += 1; _t }";
            case hir::HirUnaryOp::PostDec:
                return "{ let _t = " + op_str + "; " + op_str + " -= 1; _t }";
            case hir::HirUnaryOp::Deref:
                return "(*" + op_str + ")";
            case hir::HirUnaryOp::AddrOf:
                return "(&" + op_str + ")";
            default:
                return op_str;
        }
    }

    Type inferBinaryType(hir::HirBinaryOp op, Type lhs, Type rhs) {
        // 比較演算子はbool
        switch (op) {
            case hir::HirBinaryOp::Eq:
            case hir::HirBinaryOp::Ne:
            case hir::HirBinaryOp::Lt:
            case hir::HirBinaryOp::Gt:
            case hir::HirBinaryOp::Le:
            case hir::HirBinaryOp::Ge:
            case hir::HirBinaryOp::And:
            case hir::HirBinaryOp::Or:
                return Type::BOOL;
            default:
                break;
        }

        // 浮動小数点が含まれていればF64
        if (lhs == Type::F64 || rhs == Type::F64)
            return Type::F64;

        return lhs;
    }

    // 文字列連結をformat!マクロで実現
    Expression createStringConcat(const Expression& lhs, const Expression& rhs) {
        Expression result;
        result.kind = Expression::MACRO_CALL;
        result.type = Type::STRING;
        result.func_name = "format";

        // フォーマット文字列
        Expression fmt_arg;
        fmt_arg.kind = Expression::LITERAL;
        fmt_arg.type = Type::STR_SLICE;
        fmt_arg.value = "\"{}{}\"";
        result.args.push_back(fmt_arg);

        // 左辺と右辺を引数として追加
        result.args.push_back(lhs);
        result.args.push_back(rhs);

        return result;
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
        if (expr.kind == Expression::LITERAL || expr.kind == Expression::VARIABLE ||
            expr.kind == Expression::BINARY_OP) {
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
        } else if (expr.kind == Expression::MACRO_CALL) {
            std::string result = expr.func_name + "!(";
            for (size_t i = 0; i < expr.args.size(); ++i) {
                if (i > 0)
                    result += ", ";
                result += exprToString(expr.args[i]);
            }
            result += ")";
            return result;
        }
        return expr.value;
    }

    std::string escapeRustString(const std::string& str) {
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

    // エスケープされたブレースを変換 ({{ -> {, }} -> })
    std::string processEscapedBraces(const std::string& str) {
        std::string result;
        size_t i = 0;
        while (i < str.length()) {
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
            result += str[i];
            i++;
        }
        return result;
    }

    // 文字列に補間パターン {var} があるかチェック
    // {}, {:x} などの位置引数形式は補間ではない
    bool hasInterpolation(const std::string& str) {
        size_t pos = 0;
        while ((pos = str.find('{', pos)) != std::string::npos) {
            // エスケープされた {{ はスキップ
            if (pos + 1 < str.size() && str[pos + 1] == '{') {
                pos += 2;
                continue;
            }
            size_t end = str.find('}', pos);
            if (end != std::string::npos && end > pos + 1) {
                std::string content = str.substr(pos + 1, end - pos - 1);
                // :で始まる場合はフォーマット指定子のみ（位置引数）
                if (content[0] != ':') {
                    // 変数名が含まれている（真の補間）
                    return true;
                }
            }
            pos++;
        }
        return false;
    }

    // 文字列補間を format! マクロに変換
    Expression processStringInterpolation(const std::string& str) {
        std::string format_str;
        std::vector<std::string> args;

        size_t i = 0;
        while (i < str.length()) {
            // エスケープされた {{ と }}
            if (str[i] == '{' && i + 1 < str.length() && str[i + 1] == '{') {
                format_str += "{{";
                i += 2;
                continue;
            }
            if (str[i] == '}' && i + 1 < str.length() && str[i + 1] == '}') {
                format_str += "}}";
                i += 2;
                continue;
            }

            if (str[i] == '{') {
                size_t end = str.find('}', i);
                if (end != std::string::npos) {
                    std::string content = str.substr(i + 1, end - i - 1);

                    // フォーマット指定子を分離
                    size_t colon = content.find(':');
                    std::string var_name =
                        (colon != std::string::npos) ? content.substr(0, colon) : content;
                    std::string format_spec =
                        (colon != std::string::npos) ? content.substr(colon + 1) : "";

                    if (!var_name.empty()) {
                        args.push_back(var_name);

                        // Rustのフォーマット指定子に変換
                        if (!format_spec.empty()) {
                            format_str += "{:" + convertFormatSpec(format_spec) + "}";
                        } else {
                            format_str += "{}";
                        }
                    }
                    i = end + 1;
                    continue;
                }
            }

            format_str += str[i];
            i++;
        }

        // format!("...", arg1, arg2, ...) を生成
        Expression result;
        result.kind = Expression::MACRO_CALL;
        result.type = Type::STRING;
        result.func_name = "format";

        // フォーマット文字列を最初の引数として追加
        Expression fmt_arg;
        fmt_arg.kind = Expression::LITERAL;
        fmt_arg.type = Type::STR_SLICE;
        fmt_arg.value = "\"" + format_str + "\"";
        result.args.push_back(fmt_arg);

        // 変数引数を追加
        for (const auto& arg : args) {
            Expression var_arg;
            var_arg.kind = Expression::VARIABLE;
            var_arg.type = Type::STRING;  // 実際の型は不明だが問題なし
            var_arg.value = arg;
            result.args.push_back(var_arg);
        }

        return result;
    }

    // フォーマット指定子をRust形式に変換
    std::string convertFormatSpec(const std::string& spec) {
        // x -> :x (16進小文字)
        // X -> :X (16進大文字)
        // b -> :b (2進数)
        // o -> :o (8進数)
        // .N -> :.N (小数点以下N桁)
        if (spec == "x" || spec == "X" || spec == "b" || spec == "o") {
            return spec;
        }
        if (!spec.empty() && spec[0] == '.') {
            return spec;
        }
        // パディング指定 (0>5 など)
        if (spec.length() > 2 && spec[0] == '0' && spec[1] == '>') {
            return "0>" + spec.substr(2);
        }
        return spec;
    }

    std::string formatFloat(double val, Type type = Type::F64) {
        std::ostringstream oss;
        oss << std::fixed << val;
        std::string str = oss.str();
        // 末尾のゼロを削除（ただし小数点の直後のゼロは残す）
        size_t dot = str.find('.');
        if (dot != std::string::npos) {
            size_t last = str.find_last_not_of('0');
            if (last > dot) {
                str = str.substr(0, last + 1);
            } else {
                str = str.substr(0, dot + 2);
            }
        }
        return str + (type == Type::F32 ? "_f32" : "_f64");
    }
};

}  // namespace rust_mir
}  // namespace cm
