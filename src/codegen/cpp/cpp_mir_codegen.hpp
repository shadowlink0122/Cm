#pragma once

#include "cpp_mir.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace cm {
namespace cpp_mir {

class CppCodeGenerator {
   public:
    std::string generate(const Program& program) {
        reset();

        // ヘッダー部分
        generateHeaders(program);

        // ヘルパー関数（必要な場合）
        if (program.needs_format_helpers) {
            generateFormatHelpers();
        }

        // メイン関数と他の関数
        for (const auto& func : program.functions) {
            generateFunction(func);
        }

        return output.str();
    }

   private:
    std::ostringstream output;
    int indent_level = 0;
    const std::string indent_str = "    ";  // 4スペース

    void reset() {
        output.str("");
        output.clear();
        indent_level = 0;
    }

    void emit(const std::string& code) {
        for (int i = 0; i < indent_level; ++i) {
            output << indent_str;
        }
        output << code << "\n";
    }

    // 改行なしで出力（else if用）
    void emitInline(const std::string& code) { output << code; }

    void generateHeaders(const Program& program) {
        // 標準ヘッダー
        for (const auto& header : program.includes) {
            emit("#include <" + header + ">");
        }
        emit("");
    }

    void generateFormatHelpers() {
        emit("// 文字列フォーマットヘルパー");
        emit("std::string to_binary(int n) {");
        indent_level++;
        emit("if (n == 0) return \"0\";");
        emit("std::string result;");
        emit("while (n > 0) { result = (char)('0' + (n & 1)) + result; n >>= 1; }");
        emit("return result;");
        indent_level--;
        emit("}");
        emit("");
    }

    void generateFunction(const Function& func) {
        // 関数シグネチャ
        std::string return_type_str = typeToString(func.return_type);
        std::string params_str;

        if (func.parameters.empty()) {
            params_str = "";
        } else {
            for (size_t i = 0; i < func.parameters.size(); ++i) {
                if (i > 0)
                    params_str += ", ";
                params_str +=
                    typeToString(func.parameters[i].first) + " " + func.parameters[i].second;
            }
        }

        emit(return_type_str + " " + func.name + "(" + params_str + ") {");
        indent_level++;

        // 関数本体
        for (const auto& stmt : func.body) {
            generateStatement(stmt);
        }

        // main関数で明示的なreturnがない場合、return 0;を追加
        if (func.name == "main" && func.return_type == Type::INT32) {
            bool has_return = false;
            for (const auto& stmt : func.body) {
                if (stmt.kind == StatementKind::RETURN) {
                    has_return = true;
                    break;
                }
            }
            if (!has_return) {
                emit("return 0;");
            }
        }

        indent_level--;
        emit("}");
        emit("");
    }

    void generateStatement(const Statement& stmt) {
        switch (stmt.kind) {
            case StatementKind::DECLARATION:
                generateDeclaration(stmt);
                break;
            case StatementKind::ASSIGNMENT:
                generateAssignment(stmt);
                break;
            case StatementKind::PRINTF:
                generatePrintf(stmt);
                break;
            case StatementKind::EXPRESSION:
                generateExpressionStmt(stmt);
                break;
            case StatementKind::IF_ELSE:
                generateIfElse(stmt);
                break;
            case StatementKind::WHILE:
                generateWhile(stmt);
                break;
            case StatementKind::FOR:
                generateFor(stmt);
                break;
            case StatementKind::RETURN:
                generateReturn(stmt);
                break;
            case StatementKind::BREAK:
                emit("break;");
                break;
            case StatementKind::CONTINUE:
                emit("continue;");
                break;
        }
    }

    void generateDeclaration(const Statement& stmt) {
        const auto& decl = stmt.decl_data;
        std::string type_str = typeToString(decl.type);
        std::string code = type_str + " " + decl.name;

        if (decl.init) {
            code += " = " + expressionToString(*decl.init);
        }

        emit(code + ";");
    }

    void generateAssignment(const Statement& stmt) {
        const auto& assign = stmt.assign_data;
        emit(assign.target + " = " + expressionToString(assign.value) + ";");
    }

    void generatePrintf(const Statement& stmt) {
        const auto& printf_data = stmt.printf_data;
        std::string code = "printf(\"" + printf_data.format + "\"";

        for (const auto& arg : printf_data.args) {
            code += ", ";

            // bool型の特別処理
            if (arg.type == Type::BOOL) {
                code += "(" + expressionToString(arg) + " ? \"true\" : \"false\")";
            } else if (arg.type == Type::STRING) {
                // std::stringの場合は.c_str()を追加
                // ただし、文字列リテラル（"で始まる）は追加不要
                std::string expr_str = expressionToString(arg);
                if (expr_str.find(".c_str()") == std::string::npos && !expr_str.empty() &&
                    expr_str[0] != '"') {
                    code += expr_str + ".c_str()";
                } else {
                    code += expr_str;
                }
            } else {
                code += expressionToString(arg);
            }
        }

        code += ")";
        emit(code + ";");
    }

    void generateExpressionStmt(const Statement& stmt) {
        emit(expressionToString(stmt.expr_data) + ";");
    }

    void generateIfElse(const Statement& stmt) {
        if (!stmt.if_data)
            return;
        const auto& if_else = *stmt.if_data;

        emit("if (" + expressionToString(if_else.condition) + ") {");
        indent_level++;
        for (const auto& s : if_else.then_body) {
            if (s)
                generateStatement(*s);
        }
        indent_level--;

        if (!if_else.else_body.empty()) {
            // else_bodyの最初の要素がIF_ELSEの場合は "} else if" として出力
            if (if_else.else_body.size() == 1 && if_else.else_body[0] &&
                if_else.else_body[0]->kind == StatementKind::IF_ELSE) {
                // インデント出力
                for (int i = 0; i < indent_level; ++i) {
                    output << indent_str;
                }
                emitInline("} else ");
                // generateStatementを呼ぶと"if (...)"から始まるので、そのまま続けられる
                generateIfElseInline(*if_else.else_body[0]);
            } else {
                emit("} else {");
                indent_level++;
                for (const auto& s : if_else.else_body) {
                    if (s)
                        generateStatement(*s);
                }
                indent_level--;
                emit("}");
            }
        } else {
            emit("}");
        }
    }

    // else if チェーン用のインライン生成（} else if 形式）
    void generateIfElseInline(const Statement& stmt) {
        if (!stmt.if_data)
            return;
        const auto& if_else = *stmt.if_data;

        // "if (...) {"をインラインで出力（emitを使わない）
        output << "if (" << expressionToString(if_else.condition) << ") {\n";
        indent_level++;
        for (const auto& s : if_else.then_body) {
            if (s)
                generateStatement(*s);
        }
        indent_level--;

        if (!if_else.else_body.empty()) {
            // 再帰的にelse ifを処理
            if (if_else.else_body.size() == 1 && if_else.else_body[0] &&
                if_else.else_body[0]->kind == StatementKind::IF_ELSE) {
                for (int i = 0; i < indent_level; ++i) {
                    output << indent_str;
                }
                emitInline("} else ");
                generateIfElseInline(*if_else.else_body[0]);
            } else {
                emit("} else {");
                indent_level++;
                for (const auto& s : if_else.else_body) {
                    if (s)
                        generateStatement(*s);
                }
                indent_level--;
                emit("}");
            }
        } else {
            emit("}");
        }
    }

    void generateWhile(const Statement& stmt) {
        if (!stmt.while_data)
            return;
        const auto& while_loop = *stmt.while_data;

        emit("while (" + expressionToString(while_loop.condition) + ") {");
        indent_level++;
        for (const auto& s : while_loop.body) {
            if (s)
                generateStatement(*s);
        }
        indent_level--;
        emit("}");
    }

    void generateFor(const Statement& stmt) {
        if (!stmt.for_data)
            return;
        const auto& for_loop = *stmt.for_data;

        std::string for_header = "for (";

        // 初期化部
        if (for_loop.init) {
            if (for_loop.init->kind == StatementKind::DECLARATION) {
                const auto& decl = for_loop.init->decl_data;
                for_header += typeToString(decl.type) + " " + decl.name;
                if (decl.init) {
                    for_header += " = " + expressionToString(*decl.init);
                }
            } else if (for_loop.init->kind == StatementKind::ASSIGNMENT) {
                const auto& assign = for_loop.init->assign_data;
                for_header += assign.target + " = " + expressionToString(assign.value);
            }
        }
        for_header += "; ";

        // 条件部
        if (for_loop.condition) {
            for_header += expressionToString(*for_loop.condition);
        }
        for_header += "; ";

        // 更新部
        if (for_loop.update) {
            if (for_loop.update->kind == StatementKind::ASSIGNMENT) {
                const auto& assign = for_loop.update->assign_data;
                for_header += assign.target + " = " + expressionToString(assign.value);
            } else if (for_loop.update->kind == StatementKind::EXPRESSION) {
                // 式文（++i, i++, i = i + 1 等）
                for_header += expressionToString(for_loop.update->expr_data);
            }
        }

        for_header += ") {";
        emit(for_header);

        indent_level++;
        for (const auto& s : for_loop.body) {
            if (s)
                generateStatement(*s);
        }
        indent_level--;
        emit("}");
    }

    void generateReturn(const Statement& stmt) {
        const auto& ret = stmt.return_data;
        if (ret.value) {
            emit("return " + expressionToString(*ret.value) + ";");
        } else {
            emit("return;");
        }
    }

    std::string expressionToString(const Expression& expr) {
        switch (expr.kind) {
            case Expression::LITERAL:
            case Expression::VARIABLE:
            case Expression::BINARY_OP:
            case Expression::UNARY_OP:
                return expr.value;

            case Expression::CALL: {
                std::string result = expr.func_name + "(";
                for (size_t i = 0; i < expr.args.size(); ++i) {
                    if (i > 0)
                        result += ", ";
                    result += expressionToString(expr.args[i]);
                }
                result += ")";
                return result;
            }

            case Expression::CAST: {
                return "(" + typeToString(expr.type) + ")" + expr.value;
            }

            default:
                return "";
        }
    }

    std::string typeToString(Type type) {
        switch (type) {
            case Type::VOID:
                return "void";
            case Type::BOOL:
                return "bool";
            case Type::CHAR:
                return "char";
            case Type::INT8:
                return "int8_t";
            case Type::INT16:
                return "int16_t";
            case Type::INT32:
                return "int32_t";
            case Type::INT64:
                return "int64_t";
            case Type::UINT8:
                return "uint8_t";
            case Type::UINT16:
                return "uint16_t";
            case Type::UINT32:
                return "uint32_t";
            case Type::UINT64:
                return "uint64_t";
            case Type::FLOAT:
                return "float";
            case Type::DOUBLE:
                return "double";
            case Type::STRING:
                return "std::string";
            case Type::CHAR_PTR:
                return "const char*";
            default:
                return "int32_t";
        }
    }
};

}  // namespace cpp_mir
}  // namespace cm