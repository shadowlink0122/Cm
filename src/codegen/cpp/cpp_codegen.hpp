#pragma once

#include "../../common/debug.hpp"
#include "../../hir/hir_nodes.hpp"
#include "../../mir/mir_nodes.hpp"

#include <bitset>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace cm::codegen {

// C++コード生成器
class CppCodeGenerator {
   public:
    struct Options {
        std::string output_dir = "";
        bool optimize = false;
        bool debug_info = false;
    };

   private:
    Options opts;
    std::ostringstream output;
    int indent_level = 0;
    std::unordered_map<size_t, std::string> inferred_types;  // 型推論結果
    std::unordered_map<size_t, std::string> const_strings;   // 定数文字列の値を保持

    // インデント付き出力
    void emit_line(const std::string& line) {
        for (int i = 0; i < indent_level; ++i) {
            output << "    ";
        }
        output << line << "\n";
    }

    void emit(const std::string& text) {
        for (int i = 0; i < indent_level; ++i) {
            output << "    ";
        }
        output << text;
    }

    // オペランドの型を取得するヘルパー関数
    hir::TypePtr get_operand_type(const mir::MirOperand& operand, const mir::MirFunction& func) {
        switch (operand.kind) {
            case mir::MirOperand::Move:
            case mir::MirOperand::Copy: {
                auto& place = std::get<mir::MirPlace>(operand.data);
                if (place.local < func.locals.size()) {
                    auto type = func.locals[place.local].type;
                    // 型が不明な場合は変数の型を推論
                    if (!type || type->name == "T" || type->name.empty()) {
                        // 推論された型から判断
                        if (inferred_types.count(place.local) &&
                            !inferred_types[place.local].empty()) {
                            const auto& inferred = inferred_types[place.local];
                            if (inferred == "string" || inferred == "std::string") {
                                return hir::make_string();
                            } else if (inferred == "int") {
                                return hir::make_int();
                            } else if (inferred == "double") {
                                return hir::make_double();
                            } else if (inferred == "bool") {
                                return hir::make_bool();
                            }
                        }
                    }
                    return type;
                }
                break;
            }
            case mir::MirOperand::Constant: {
                auto& constant = std::get<mir::MirConstant>(operand.data);
                if (std::holds_alternative<std::string>(constant.value)) {
                    // 文字列定数の型
                    return hir::make_string();
                } else if (std::holds_alternative<int64_t>(constant.value)) {
                    // 整数定数の型
                    return hir::make_int();
                } else if (std::holds_alternative<double>(constant.value)) {
                    // 浮動小数点定数の型
                    return hir::make_double();
                } else if (std::holds_alternative<bool>(constant.value)) {
                    // bool定数の型
                    return hir::make_bool();
                }
                break;
            }
            default:
                break;
        }
        return nullptr;
    }

    // Type → C++
    std::string type_to_cpp(hir::TypePtr type) {
        if (!type)
            return "int";

        // TypeKindを使って判定（プリミティブ型はnameが空）
        switch (type->kind) {
            case hir::TypeKind::Void:
                return "void";
            case hir::TypeKind::Bool:
                return "bool";
            case hir::TypeKind::Tiny:
                return "int8_t";
            case hir::TypeKind::Short:
                return "int16_t";
            case hir::TypeKind::Int:
                return "int";
            case hir::TypeKind::Long:
                return "long";
            case hir::TypeKind::UTiny:
                return "uint8_t";
            case hir::TypeKind::UShort:
                return "uint16_t";
            case hir::TypeKind::UInt:
                return "unsigned int";
            case hir::TypeKind::ULong:
                return "unsigned long";
            case hir::TypeKind::Float:
                return "float";
            case hir::TypeKind::Double:
                return "double";
            case hir::TypeKind::Char:
                return "char";
            case hir::TypeKind::String:
                return "std::string";
            case hir::TypeKind::Pointer:
                return type_to_cpp(type->element_type) + "*";
            case hir::TypeKind::Reference:
                return type_to_cpp(type->element_type) + "&";
            case hir::TypeKind::Array:
                if (type->array_size) {
                    return type_to_cpp(type->element_type) + "[" +
                           std::to_string(*type->array_size) + "]";
                } else {
                    return "std::vector<" + type_to_cpp(type->element_type) + ">";
                }
            default:
                // ユーザー定義型の場合はnameを使用
                if (!type->name.empty()) {
                    return type->name;
                }
                return "int";  // デフォルト
        }
    }

    // Place → C++
    std::string place_to_cpp(const mir::MirPlace& place) {
        std::string result = "_" + std::to_string(place.local);

        for (const auto& proj : place.projections) {
            switch (proj.kind) {
                case mir::ProjectionKind::Field:
                    result += "." + std::to_string(proj.field_id);
                    break;
                case mir::ProjectionKind::Index:
                    result += "[_" + std::to_string(proj.index_local) + "]";
                    break;
                case mir::ProjectionKind::Deref:
                    result = "(*" + result + ")";
                    break;
            }
        }

        return result;
    }

    // Operand → C++
    std::string operand_to_cpp(const mir::MirOperand& op) {
        switch (op.kind) {
            case mir::MirOperand::Move:
            case mir::MirOperand::Copy:
                if (auto* place = std::get_if<mir::MirPlace>(&op.data)) {
                    return place_to_cpp(*place);
                }
                break;
            case mir::MirOperand::Constant:
                if (auto* constant = std::get_if<mir::MirConstant>(&op.data)) {
                    return constant_to_cpp(*constant);
                }
                break;
            case mir::MirOperand::FunctionRef:
                if (auto* func_name = std::get_if<std::string>(&op.data)) {
                    // std::io::println/print を C++のprintln/printにマップ
                    if (*func_name == "std::io::println" || *func_name == "println") {
                        return "println";
                    } else if (*func_name == "std::io::print" || *func_name == "print") {
                        return "print";
                    }
                    return *func_name;
                }
                break;
        }
        return "/* unsupported operand */";
    }

    // Constant → C++
    std::string constant_to_cpp(const mir::MirConstant& constant) {
        if (std::holds_alternative<bool>(constant.value)) {
            return std::get<bool>(constant.value) ? "true" : "false";
        }
        if (std::holds_alternative<int64_t>(constant.value)) {
            return std::to_string(std::get<int64_t>(constant.value));
        }
        if (std::holds_alternative<double>(constant.value)) {
            double val = std::get<double>(constant.value);
            // 整数値の場合
            if (val == std::floor(val)) {
                return std::to_string(static_cast<int64_t>(val));
            }
            // 浮動小数点値の場合
            if (constant.type && constant.type->name == "float") {
                return std::to_string(val) + "f";
            }
            return std::to_string(val);
        }
        if (std::holds_alternative<char>(constant.value)) {
            char c = std::get<char>(constant.value);
            return "'" + std::string(1, c) + "'";
        }
        if (auto* s = std::get_if<std::string>(&constant.value)) {
            return "\"" + escape_string(*s) + "\"";
        }
        return "/* unsupported constant */";
    }

    // 文字列エスケープ
    std::string escape_string(const std::string& str) {
        std::string result;
        size_t i = 0;
        while (i < str.length()) {
            // MIRレベルのエスケープシーケンス {{ と }} を単一の { と } に変換
            if (i + 1 < str.length()) {
                if (str[i] == '{' && str[i + 1] == '{') {
                    result += '{';
                    i += 2;
                    continue;
                } else if (str[i] == '}' && str[i + 1] == '}') {
                    result += '}';
                    i += 2;
                    continue;
                }
            }

            // 通常の文字エスケープ
            char c = str[i];
            switch (c) {
                case '\n':
                    result += "\\n";
                    break;
                case '\r':
                    result += "\\r";
                    break;
                case '\t':
                    result += "\\t";
                    break;
                case '\\':
                    result += "\\\\";
                    break;
                case '"':
                    result += "\\\"";
                    break;
                default:
                    result += c;
                    break;
            }
            i++;
        }
        return result;
    }

    // RValue → C++
    std::string rvalue_to_cpp(const mir::MirRvalue& rvalue, const mir::MirFunction& func) {
        switch (rvalue.kind) {
            case mir::MirRvalue::Use: {
                auto& data = std::get<mir::MirRvalue::UseData>(rvalue.data);
                return operand_to_cpp(*data.operand);
            }
            case mir::MirRvalue::BinaryOp: {
                auto& data = std::get<mir::MirRvalue::BinaryOpData>(rvalue.data);

                // 文字列連結の特別処理
                if (data.op == mir::MirBinaryOp::Add) {
                    auto lhs_type = get_operand_type(*data.lhs, func);
                    auto rhs_type = get_operand_type(*data.rhs, func);

                    bool lhs_is_string = lhs_type && lhs_type->kind == hir::TypeKind::String;
                    bool rhs_is_string = rhs_type && rhs_type->kind == hir::TypeKind::String;

                    if (lhs_is_string || rhs_is_string) {
                        std::string lhs_expr = operand_to_cpp(*data.lhs);
                        std::string rhs_expr = operand_to_cpp(*data.rhs);

                        // 非文字列型をstd::to_stringでラップ
                        if (!lhs_is_string && lhs_type) {
                            if (lhs_type->kind == hir::TypeKind::Int ||
                                lhs_type->kind == hir::TypeKind::Double ||
                                lhs_type->kind == hir::TypeKind::Bool) {
                                lhs_expr = "std::to_string(" + lhs_expr + ")";
                            }
                        }
                        if (!rhs_is_string && rhs_type) {
                            if (rhs_type->kind == hir::TypeKind::Int ||
                                rhs_type->kind == hir::TypeKind::Double ||
                                rhs_type->kind == hir::TypeKind::Bool) {
                                rhs_expr = "std::to_string(" + rhs_expr + ")";
                            }
                        }

                        return lhs_expr + " + " + rhs_expr;
                    }
                }

                // 通常の二項演算
                return operand_to_cpp(*data.lhs) + " " + binary_op_to_cpp(data.op) + " " +
                       operand_to_cpp(*data.rhs);
            }
            case mir::MirRvalue::UnaryOp: {
                auto& data = std::get<mir::MirRvalue::UnaryOpData>(rvalue.data);
                return unary_op_to_cpp(data.op) + operand_to_cpp(*data.operand);
            }
            case mir::MirRvalue::FormatConvert: {
                auto& data = std::get<mir::MirRvalue::FormatConvertData>(rvalue.data);
                std::string operand = operand_to_cpp(*data.operand);

                // ラムダ式を使って即座にフォーマット変換を実行
                if (data.format_spec == "x") {
                    // 16進数小文字
                    return "[&]{ std::stringstream ss; ss << std::hex << " + operand +
                           "; return ss.str(); }()";
                } else if (data.format_spec == "X") {
                    // 16進数大文字
                    return "[&]{ std::stringstream ss; ss << std::hex << std::uppercase << " +
                           operand + "; std::string s = ss.str(); return s; }()";
                } else if (data.format_spec == "b") {
                    // 2進数
                    return "[&]{ std::bitset<32> bs(" + operand +
                           "); std::string s = bs.to_string(); s.erase(0, "
                           "s.find_first_not_of('0')); return s.empty() ? \"0\" : s; }()";
                } else if (data.format_spec == "o") {
                    // 8進数
                    return "[&]{ std::stringstream ss; ss << std::oct << " + operand +
                           "; return ss.str(); }()";
                } else if (data.format_spec.size() > 1 && data.format_spec[0] == '.') {
                    // 小数点精度
                    int precision = std::stoi(data.format_spec.substr(1));
                    return "[&]{ std::stringstream ss; ss << std::fixed << std::setprecision(" +
                           std::to_string(precision) + ") << " + operand + "; return ss.str(); }()";
                } else {
                    // デフォルト
                    return "std::to_string(" + operand + ")";
                }
            }
            default:
                return "/* unsupported rvalue */";
        }
    }

    // 二項演算子 → C++
    std::string binary_op_to_cpp(mir::MirBinaryOp op) {
        switch (op) {
            case mir::MirBinaryOp::Add:
                return "+";
            case mir::MirBinaryOp::Sub:
                return "-";
            case mir::MirBinaryOp::Mul:
                return "*";
            case mir::MirBinaryOp::Div:
                return "/";
            case mir::MirBinaryOp::Mod:
                return "%";
            case mir::MirBinaryOp::BitAnd:
                return "&";
            case mir::MirBinaryOp::BitOr:
                return "|";
            case mir::MirBinaryOp::BitXor:
                return "^";
            case mir::MirBinaryOp::Shl:
                return "<<";
            case mir::MirBinaryOp::Shr:
                return ">>";
            case mir::MirBinaryOp::Eq:
                return "==";
            case mir::MirBinaryOp::Ne:
                return "!=";
            case mir::MirBinaryOp::Lt:
                return "<";
            case mir::MirBinaryOp::Le:
                return "<=";
            case mir::MirBinaryOp::Gt:
                return ">";
            case mir::MirBinaryOp::Ge:
                return ">=";
            case mir::MirBinaryOp::And:
                return "&&";
            case mir::MirBinaryOp::Or:
                return "||";
            default:
                return "+";
        }
    }

    // 単項演算子 → C++
    std::string unary_op_to_cpp(mir::MirUnaryOp op) {
        switch (op) {
            case mir::MirUnaryOp::Neg:
                return "-";
            case mir::MirUnaryOp::Not:
                return "!";
            case mir::MirUnaryOp::BitNot:
                return "~";
            default:
                return "";
        }
    }

   public:
    CppCodeGenerator(const Options& options) : opts(options) {}

    // MIRプログラムからC++コードを生成
    std::string generate(const mir::MirProgram& program) {
        if (debug::g_debug_mode) {
            debug::log(debug::Stage::CodegenCpp, debug::Level::Info,
                       "Starting C++ code generation");
        }

        // ヘッダー
        emit_line("// Generated by Cm C++ Codegen");
        emit_line("// Target: C++17 or later");
        emit_line("");
        emit_line("#include <iostream>");
        emit_line("#include <string>");
        emit_line("#include <cstdlib>");
        emit_line("#include <sstream>");
        emit_line("#include <iomanip>");
        emit_line("#include <bitset>");
        emit_line("#include <tuple>");
        emit_line("#include <type_traits>");
        emit_line("");

        // 標準ライブラリ関数
        emit_line("// Standard library functions");
        emit_line("");
        emit_line("template<typename T>");
        emit_line("void print_value(const T& value) {");
        indent_level++;
        emit_line("if constexpr (std::is_same_v<T, bool>) {");
        indent_level++;
        emit_line("std::cout << (value ? \"true\" : \"false\");");
        indent_level--;
        emit_line("} else {");
        indent_level++;
        emit_line("std::cout << value;");
        indent_level--;
        emit_line("}");
        indent_level--;
        emit_line("}");
        emit_line("");
        emit_line("template<typename... Args>");
        emit_line("void print(Args... args) {");
        indent_level++;
        emit_line("((print_value(args)), ...);");
        indent_level--;
        emit_line("}");
        emit_line("");
        emit_line("// Convert value to string with proper formatting");
        emit_line("template<typename T>");
        emit_line("std::string to_string_fmt(const T& value) {");
        indent_level++;
        emit_line("std::ostringstream oss;");
        emit_line("if constexpr (std::is_same_v<T, bool>) {");
        indent_level++;
        emit_line("oss << (value ? \"true\" : \"false\");");
        indent_level--;
        emit_line("} else {");
        indent_level++;
        emit_line("oss << value;");
        indent_level--;
        emit_line("}");
        emit_line("return oss.str();");
        indent_level--;
        emit_line("}");
        emit_line("");
        emit_line("// println with format string support");
        emit_line("template<typename... Args>");
        emit_line("void println(Args... args) {");
        indent_level++;
        emit_line("auto args_tuple = std::make_tuple(args...);");
        emit_line("constexpr size_t num_args = sizeof...(args);");
        emit_line("");
        emit_line("if constexpr (num_args == 0) {");
        indent_level++;
        emit_line("std::cout << std::endl;");
        indent_level--;
        emit_line("} else if constexpr (num_args == 1) {");
        indent_level++;
        emit_line("print_value(std::get<0>(args_tuple));");
        emit_line("std::cout << std::endl;");
        indent_level--;
        emit_line("} else {");
        indent_level++;
        emit_line("// Check if first argument is a string with {}");
        emit_line(
            "if constexpr (std::is_convertible_v<decltype(std::get<0>(args_tuple)), std::string>) "
            "{");
        indent_level++;
        emit_line("std::string fmt = std::get<0>(args_tuple);");
        emit_line("if (fmt.find('{') != std::string::npos) {");
        indent_level++;
        emit_line("// Format string processing with format specifiers");
        emit_line("std::ostringstream oss;");
        emit_line("size_t arg_index = 1;");
        emit_line("size_t pos = 0;");
        emit_line("while (pos < fmt.length()) {");
        indent_level++;
        emit_line("if (fmt[pos] == '{' && pos + 1 < fmt.length()) {");
        indent_level++;
        emit_line("if (fmt[pos + 1] == '{') { oss << '{'; pos += 2; continue; }");
        emit_line("size_t end = fmt.find('}', pos);");
        emit_line("if (end != std::string::npos && arg_index < num_args) {");
        indent_level++;
        emit_line("std::string spec = fmt.substr(pos + 1, end - pos - 1);");
        emit_line("// Apply format specifier to argument");
        emit_line("std::apply([&](auto first, auto... rest) {");
        indent_level++;
        emit_line("size_t idx = 1;");
        emit_line("((idx++ == arg_index ? [&]{");
        indent_level++;
        emit_line("if (spec.empty() || spec == \"\") { oss << to_string_fmt(rest); }");
        emit_line("else if (spec == \":x\") {");
        indent_level++;
        emit_line(
            "if constexpr (std::is_integral_v<decltype(rest)>) { oss << std::hex << rest << "
            "std::dec; }");
        emit_line("else { oss << rest; }");
        indent_level--;
        emit_line("} else if (spec == \":X\") {");
        indent_level++;
        emit_line(
            "if constexpr (std::is_integral_v<decltype(rest)>) { oss << std::hex << std::uppercase "
            "<< rest << std::nouppercase << std::dec; }");
        emit_line("else { oss << rest; }");
        indent_level--;
        emit_line("} else if (spec == \":b\") {");
        indent_level++;
        emit_line(
            "if constexpr (std::is_integral_v<decltype(rest)>) { "
            "auto s = std::bitset<32>(rest).to_string(); "
            "auto pos = s.find('1'); "
            "oss << (pos != std::string::npos ? s.substr(pos) : \"0\"); }");
        emit_line("else { oss << rest; }");
        indent_level--;
        emit_line("} else if (spec == \":o\") {");
        indent_level++;
        emit_line(
            "if constexpr (std::is_integral_v<decltype(rest)>) { oss << std::oct << rest << "
            "std::dec; }");
        emit_line("else { oss << rest; }");
        indent_level--;
        emit_line("} else if (spec.size() > 2 && spec[0] == ':' && spec[1] == '.') {");
        indent_level++;
        emit_line("int prec = std::stoi(spec.substr(2));");
        emit_line(
            "if constexpr (std::is_floating_point_v<decltype(rest)>) { oss << std::fixed << "
            "std::setprecision(prec) << rest; }");
        emit_line("else { oss << rest; }");
        indent_level--;
        emit_line("} else if (spec == \":e\") {");
        indent_level++;
        emit_line(
            "if constexpr (std::is_floating_point_v<decltype(rest)>) { oss << std::scientific << "
            "rest; }");
        emit_line("else { oss << rest; }");
        indent_level--;
        emit_line("} else if (spec == \":E\") {");
        indent_level++;
        emit_line(
            "if constexpr (std::is_floating_point_v<decltype(rest)>) { oss << std::scientific << "
            "std::uppercase << rest << std::nouppercase; }");
        emit_line("else { oss << rest; }");
        indent_level--;
        emit_line("} else if (spec.size() > 2 && spec[0] == ':' && spec[1] == '<') {");
        indent_level++;
        emit_line("int width = std::stoi(spec.substr(2));");
        emit_line("oss << std::left << std::setw(width) << to_string_fmt(rest);");
        indent_level--;
        emit_line("} else if (spec.size() > 2 && spec[0] == ':' && spec[1] == '>') {");
        indent_level++;
        emit_line("int width = std::stoi(spec.substr(2));");
        emit_line("oss << std::right << std::setw(width) << to_string_fmt(rest);");
        indent_level--;
        emit_line("} else if (spec.size() > 2 && spec[0] == ':' && spec[1] == '^') {");
        indent_level++;
        emit_line("int width = std::stoi(spec.substr(2));");
        emit_line("std::string s = to_string_fmt(rest);");
        emit_line("int pad = (width - s.length()) / 2;");
        emit_line(
            "oss << std::string(pad, ' ') << s << std::string(width - s.length() - pad, ' ');");
        indent_level--;
        emit_line("} else if (spec.size() > 4 && spec.substr(0, 4) == \":0>\") {");
        indent_level++;
        emit_line("int width = std::stoi(spec.substr(4));");
        emit_line("oss << std::setfill('0') << std::setw(width) << rest << std::setfill(' ');");
        indent_level--;
        emit_line("} else { oss << to_string_fmt(rest); }");
        indent_level--;
        emit_line("}() : void()), ...);");
        indent_level--;
        emit_line("}, args_tuple);");
        emit_line("arg_index++;");
        emit_line("pos = end + 1;");
        indent_level--;
        emit_line("} else { oss << fmt[pos++]; }");
        indent_level--;
        emit_line("} else if (fmt[pos] == '}' && pos + 1 < fmt.length() && fmt[pos + 1] == '}') {");
        indent_level++;
        emit_line("oss << '}'; pos += 2;");
        indent_level--;
        emit_line("} else { oss << fmt[pos++]; }");
        indent_level--;
        emit_line("}");
        emit_line("std::cout << oss.str() << std::endl;");
        indent_level--;
        emit_line("} else {");
        indent_level++;
        emit_line("// Not a format string, print all arguments");
        emit_line("((print_value(args)), ...);");
        emit_line("std::cout << std::endl;");
        indent_level--;
        emit_line("}");
        indent_level--;
        emit_line("} else {");
        indent_level++;
        emit_line("// First argument is not a string, print all arguments");
        emit_line("((print_value(args)), ...);");
        emit_line("std::cout << std::endl;");
        indent_level--;
        emit_line("}");
        indent_level--;
        emit_line("}");
        indent_level--;
        emit_line("}");
        emit_line("");

        // 各関数の生成
        for (const auto& func : program.functions) {
            if (func) {
                generate_function(*func);
            }
        }

        // main関数のラッパー（エントリーポイント）
        if (!program.functions.empty() && program.functions[0] &&
            program.functions[0]->name == "main") {
            emit_line("// Entry point");
            emit_line("int main(int argc, char* argv[]) {");
            indent_level++;
            emit_line("return cm_main();");
            indent_level--;
            emit_line("}");
        }

        return output.str();
    }

    // 関数生成
    void generate_function(const mir::MirFunction& func) {
        if (debug::g_debug_mode) {
            debug::log(debug::Stage::CodegenCpp, debug::Level::Debug,
                       "Generating function: " + func.name);
        }

        // main関数の特別処理
        std::string func_name = func.name;
        bool is_main = (func_name == "main");
        if (is_main) {
            func_name = "cm_main";  // C++のmainと衝突を避ける
        }

        // 関数シグネチャ
        std::string return_type;
        if (is_main) {
            // main関数は常にintを返す（C++のmain互換性のため）
            return_type = "int";
        } else {
            return_type = type_to_cpp(func.locals[func.return_local].type);
        }
        emit_line(return_type + " " + func_name + "() {");
        indent_level++;

        // ローカル変数宣言
        generate_locals(func);

        // ステートマシンで基本ブロック処理
        emit_line("int __bb = 0;");
        emit_line("while (true) {");
        indent_level++;
        emit_line("switch (__bb) {");
        indent_level++;

        for (const auto& block : func.basic_blocks) {
            if (block) {
                emit_line("case " + std::to_string(block->id) + ":");
                indent_level++;
                generate_basic_block(*block, func);
                indent_level--;
            }
        }

        emit_line("default:");
        indent_level++;
        emit_line("std::cerr << \"Invalid basic block: \" << __bb << std::endl;");
        emit_line("std::abort();");
        indent_level--;

        indent_level--;
        emit_line("}");
        indent_level--;
        emit_line("}");

        indent_level--;
        emit_line("}");
        emit_line("");
    }

    // ローカル変数生成
    void generate_locals(const mir::MirFunction& func) {
        // 型推論のために文を先読み
        inferred_types.clear();
        const_strings.clear();  // 定数文字列もクリア

        // 型推論を複数回実行して伝播を完全にする
        for (int pass = 0; pass < 3; pass++) {
            // 定数と演算から型を推論
            for (const auto& block : func.basic_blocks) {
                if (!block)
                    continue;
                for (const auto& stmt : block->statements) {
                    if (!stmt)
                        continue;
                    if (stmt->kind == mir::MirStatement::Assign) {
                        auto& data = std::get<mir::MirStatement::AssignData>(stmt->data);
                        size_t dest_local = data.place.local;

                        // 比較演算の結果はbool
                        if (data.rvalue && data.rvalue->kind == mir::MirRvalue::BinaryOp) {
                            auto& binop_data =
                                std::get<mir::MirRvalue::BinaryOpData>(data.rvalue->data);
                            if (binop_data.op >= mir::MirBinaryOp::Eq &&
                                binop_data.op <= mir::MirBinaryOp::Ge) {
                                inferred_types[dest_local] = "bool";
                                if (debug::g_debug_mode) {
                                    debug::log(debug::Stage::CodegenCpp, debug::Level::Debug,
                                               "Inferred bool type for _" +
                                                   std::to_string(dest_local) +
                                                   " from comparison op");
                                }
                            }
                            // 文字列連結の結果はstring
                            else if (binop_data.op == mir::MirBinaryOp::Add) {
                                // いずれかのオペランドが文字列型かどうかをチェック
                                bool is_string_concat = false;

                                // 左辺のチェック
                                if (binop_data.lhs) {
                                    if (binop_data.lhs->kind == mir::MirOperand::Constant) {
                                        auto& constant =
                                            std::get<mir::MirConstant>(binop_data.lhs->data);
                                        if (std::holds_alternative<std::string>(constant.value)) {
                                            is_string_concat = true;
                                        }
                                    } else if (binop_data.lhs->kind == mir::MirOperand::Copy ||
                                               binop_data.lhs->kind == mir::MirOperand::Move) {
                                        auto& place = std::get<mir::MirPlace>(binop_data.lhs->data);
                                        if (inferred_types.count(place.local) &&
                                            (inferred_types[place.local] == "std::string" ||
                                             inferred_types[place.local] == "string")) {
                                            is_string_concat = true;
                                        }
                                    }
                                }

                                // 右辺のチェック
                                if (!is_string_concat && binop_data.rhs) {
                                    if (binop_data.rhs->kind == mir::MirOperand::Constant) {
                                        auto& constant =
                                            std::get<mir::MirConstant>(binop_data.rhs->data);
                                        if (std::holds_alternative<std::string>(constant.value)) {
                                            is_string_concat = true;
                                        }
                                    } else if (binop_data.rhs->kind == mir::MirOperand::Copy ||
                                               binop_data.rhs->kind == mir::MirOperand::Move) {
                                        auto& place = std::get<mir::MirPlace>(binop_data.rhs->data);
                                        if (inferred_types.count(place.local) &&
                                            (inferred_types[place.local] == "std::string" ||
                                             inferred_types[place.local] == "string")) {
                                            is_string_concat = true;
                                        }
                                    }
                                }

                                if (is_string_concat) {
                                    inferred_types[dest_local] = "std::string";
                                    if (debug::g_debug_mode) {
                                        debug::log(debug::Stage::CodegenCpp, debug::Level::Debug,
                                                   "Inferred std::string type for _" +
                                                       std::to_string(dest_local) +
                                                       " from string concatenation");
                                    }
                                }
                            }
                        }
                        // Use文の処理
                        else if (data.rvalue && data.rvalue->kind == mir::MirRvalue::Use) {
                            auto& use_data = std::get<mir::MirRvalue::UseData>(data.rvalue->data);

                            // 定数の場合
                            if (use_data.operand &&
                                use_data.operand->kind == mir::MirOperand::Constant) {
                                auto& constant = std::get<mir::MirConstant>(use_data.operand->data);

                                if (std::holds_alternative<bool>(constant.value)) {
                                    inferred_types[dest_local] = "bool";
                                } else if (std::holds_alternative<int64_t>(constant.value)) {
                                    inferred_types[dest_local] = "int";
                                } else if (std::holds_alternative<double>(constant.value)) {
                                    // 整数値の場合
                                    double val = std::get<double>(constant.value);
                                    if (val == std::floor(val)) {
                                        inferred_types[dest_local] = "int";
                                    } else {
                                        inferred_types[dest_local] = "double";
                                    }
                                } else if (std::holds_alternative<char>(constant.value)) {
                                    inferred_types[dest_local] = "char";
                                } else if (std::holds_alternative<std::string>(constant.value)) {
                                    inferred_types[dest_local] = "std::string";
                                    // 定数文字列の値を保存
                                    const_strings[dest_local] =
                                        std::get<std::string>(constant.value);
                                    if (debug::g_debug_mode) {
                                        debug::log(debug::Stage::CodegenCpp, debug::Level::Debug,
                                                   "Inferred std::string type for _" +
                                                       std::to_string(dest_local) +
                                                       " from string constant");
                                    }
                                }
                            }
                            // 変数からのコピーの場合、型を伝播
                            else if (use_data.operand &&
                                     (use_data.operand->kind == mir::MirOperand::Copy ||
                                      use_data.operand->kind == mir::MirOperand::Move)) {
                                auto& place = std::get<mir::MirPlace>(use_data.operand->data);
                                if (inferred_types.count(place.local)) {
                                    inferred_types[dest_local] = inferred_types[place.local];
                                    if (debug::g_debug_mode) {
                                        debug::log(debug::Stage::CodegenCpp, debug::Level::Debug,
                                                   "Propagated type " +
                                                       inferred_types[place.local] + " from _" +
                                                       std::to_string(place.local) + " to _" +
                                                       std::to_string(dest_local));
                                    }
                                }
                                // 定数文字列の値も伝播
                                if (const_strings.count(place.local)) {
                                    const_strings[dest_local] = const_strings[place.local];
                                }
                            }
                        }
                        // フォーマット変換の結果は常にstd::string
                        else if (data.rvalue &&
                                 data.rvalue->kind == mir::MirRvalue::FormatConvert) {
                            inferred_types[dest_local] = "std::string";
                            if (debug::g_debug_mode) {
                                debug::log(
                                    debug::Stage::CodegenCpp, debug::Level::Debug,
                                    "Inferred std::string type for format conversion result _" +
                                        std::to_string(dest_local));
                            }
                        }
                    }
                }
            }
        }  // 型推論パスのループ終了

        // ローカル変数宣言
        for (const auto& local : func.locals) {
            // 引数とreturn値以外を宣言
            bool is_arg = false;
            for (auto arg_id : func.arg_locals) {
                if (arg_id == local.id) {
                    is_arg = true;
                    break;
                }
            }

            if (!is_arg && local.id != func.return_local) {
                std::string type_str;

                // 型推論の結果がある場合はそれを優先
                if (inferred_types.count(local.id)) {
                    type_str = inferred_types[local.id];
                    if (debug::g_debug_mode) {
                        // HIR型情報と比較
                        if (local.type) {
                            std::string hir_type = type_to_cpp(local.type);
                            if (hir_type != type_str) {
                                debug::log(debug::Stage::CodegenCpp, debug::Level::Debug,
                                           "Overriding HIR type " + hir_type +
                                               " with inferred type " + type_str + " for local _" +
                                               std::to_string(local.id));
                            }
                        } else {
                            debug::log(debug::Stage::CodegenCpp, debug::Level::Debug,
                                       "Using inferred type " + type_str + " for local _" +
                                           std::to_string(local.id) + " (no HIR type)");
                        }
                    }
                } else if (local.type) {
                    // 型推論がない場合はHIR型情報を使用
                    type_str = type_to_cpp(local.type);
                    // デバッグ: 型情報を確認（比較演算に関わる可能性のある変数のみ）
                    if (local.name.find("eq") != std::string::npos ||
                        local.name.find("ne") != std::string::npos ||
                        local.name.find("lt") != std::string::npos ||
                        local.name.find("le") != std::string::npos ||
                        local.name.find("gt") != std::string::npos ||
                        local.name.find("ge") != std::string::npos || type_str == "bool") {
                        std::cerr << "[CPP] Local _" << local.id << " (" << local.name
                                  << ") has type: " << type_str
                                  << " (HIR type: " << (local.type ? local.type->name : "null")
                                  << ")" << std::endl;
                    }
                }

                // どちらもない場合はデフォルトでint
                if (type_str.empty()) {
                    if (debug::g_debug_mode) {
                        debug::log(debug::Stage::CodegenCpp, debug::Level::Debug,
                                   "WARNING: No type info for local _" + std::to_string(local.id) +
                                       " (" + local.name + "), defaulting to int");
                    }
                    type_str = "int";
                }

                // デフォルト値で初期化
                std::string default_val;
                if (type_str == "bool") {
                    default_val = " = false";
                } else if (type_str == "int" || type_str == "long" || type_str == "unsigned int" ||
                           type_str == "unsigned long") {
                    default_val = " = 0";
                } else if (type_str == "float" || type_str == "double") {
                    default_val = " = 0.0";
                } else if (type_str == "std::string") {
                    default_val = "";  // デフォルトコンストラクタ
                } else if (type_str == "char") {
                    default_val = " = '\\0'";
                }

                emit_line(type_str + " _" + std::to_string(local.id) + default_val + ";");
            }
        }

        // main関数の戻り値変数
        if (func.name == "main") {
            emit_line("int _0 = 0;  // return value");
        }

        emit_line("");
    }

    // 基本ブロック生成
    void generate_basic_block(const mir::BasicBlock& block, const mir::MirFunction& func) {
        // コメント
        emit_line("// bb" + std::to_string(block.id));

        // 文の生成
        for (const auto& stmt : block.statements) {
            if (stmt) {
                generate_statement(*stmt, func);
            }
        }

        // 終端命令の生成
        if (block.terminator) {
            generate_terminator(*block.terminator, func);
        }
    }

    // 文の生成
    void generate_statement(const mir::MirStatement& stmt, const mir::MirFunction& func) {
        switch (stmt.kind) {
            case mir::MirStatement::Assign: {
                auto& data = std::get<mir::MirStatement::AssignData>(stmt.data);
                std::string lhs = place_to_cpp(data.place);
                std::string rhs = rvalue_to_cpp(*data.rvalue, func);
                emit_line(lhs + " = " + rhs + ";");

                // 文字列定数の追跡
                if (data.rvalue && data.rvalue->kind == mir::MirRvalue::Use) {
                    auto& use_data = std::get<mir::MirRvalue::UseData>(data.rvalue->data);
                    if (use_data.operand && use_data.operand->kind == mir::MirOperand::Constant) {
                        auto& constant = std::get<mir::MirConstant>(use_data.operand->data);
                        if (auto* str_val = std::get_if<std::string>(&constant.value)) {
                            const_strings[data.place.local] = *str_val;
                        }
                    }
                }
                break;
            }
            case mir::MirStatement::StorageLive:
            case mir::MirStatement::StorageDead:
                // C++では明示的なStorage宣言は不要
                break;
            case mir::MirStatement::Nop:
                emit_line("// nop");
                break;
        }
    }

    // 終端命令の生成
    void generate_terminator(const mir::MirTerminator& term, const mir::MirFunction& func) {
        switch (term.kind) {
            case mir::MirTerminator::Return: {
                if (func.name == "main" && func.return_local < func.locals.size()) {
                    auto return_type = func.locals[func.return_local].type;
                    if (return_type && return_type->kind != hir::TypeKind::Void &&
                        return_type->name != "void") {
                        emit_line("return _" + std::to_string(func.return_local) + ";");
                    } else {
                        emit_line("return 0;");
                    }
                } else if (func.return_local != 0) {
                    emit_line("return _" + std::to_string(func.return_local) + ";");
                } else {
                    emit_line("return;");
                }
                break;
            }

            case mir::MirTerminator::Goto: {
                auto target = std::get<mir::MirTerminator::GotoData>(term.data).target;
                emit_line("__bb = " + std::to_string(target) + ";");
                emit_line("continue;");
                break;
            }

            case mir::MirTerminator::SwitchInt: {
                auto& data = std::get<mir::MirTerminator::SwitchIntData>(term.data);
                emit_line("switch (" + operand_to_cpp(*data.discriminant) + ") {");
                indent_level++;

                for (const auto& [value, target] : data.targets) {
                    emit_line("case " + std::to_string(value) + ":");
                    indent_level++;
                    emit_line("__bb = " + std::to_string(target) + ";");
                    emit_line("break;");
                    indent_level--;
                }

                emit_line("default:");
                indent_level++;
                emit_line("__bb = " + std::to_string(data.otherwise) + ";");
                emit_line("break;");
                indent_level--;

                indent_level--;
                emit_line("}");
                emit_line("break;");
                break;
            }

            case mir::MirTerminator::Call: {
                // 内部スコープで変数を宣言
                {
                    auto& data = std::get<mir::MirTerminator::CallData>(term.data);

                    // 関数名を取得
                    std::string func_name = operand_to_cpp(*data.func);

                    // println/printの特別処理
                    if (func_name == "println" || func_name == "print") {
                        // 引数が0の場合の処理
                        if (data.args.empty()) {
                            if (func_name == "println") {
                                emit_line("println();");  // 空行を出力
                            }
                            // printの場合は何も出力しない
                            break;
                        }
                        std::string format_str;
                        bool has_format_str = false;

                        // デバッグ出力
                        emit_line("// DEBUG: println detected with " +
                                  std::to_string(data.args.size()) + " args");

                        // 第1引数が文字列定数の場合
                        if (data.args[0]->kind == mir::MirOperand::Constant) {
                            if (auto* constant =
                                    std::get_if<mir::MirConstant>(&data.args[0]->data)) {
                                if (auto* str_val = std::get_if<std::string>(&constant->value)) {
                                    format_str = *str_val;
                                    has_format_str = true;
                                }
                            }
                        }
                        // 第1引数が変数の場合、保存された定数文字列を確認
                        else if (data.args[0]->kind == mir::MirOperand::Copy ||
                                 data.args[0]->kind == mir::MirOperand::Move) {
                            if (auto* place = std::get_if<mir::MirPlace>(&data.args[0]->data)) {
                                emit_line("// DEBUG: Looking for const_string for local " +
                                          std::to_string(place->local));
                                if (const_strings.count(place->local)) {
                                    format_str = const_strings[place->local];
                                    has_format_str = true;
                                    // エスケープ処理: 改行を \\n に置換してデバッグ出力
                                    std::string escaped_str = format_str;
                                    size_t pos = 0;
                                    while ((pos = escaped_str.find('\n', pos)) !=
                                           std::string::npos) {
                                        escaped_str.replace(pos, 1, "\\n");
                                        pos += 2;
                                    }
                                    emit_line("// DEBUG: Found const_string: " + escaped_str);
                                } else {
                                    emit_line("// DEBUG: No const_string found");
                                }
                            }
                        }

                        if (has_format_str) {
                            std::vector<std::string> processed_args;
                            size_t arg_index = 1;

                            // フォーマット文字列を解析して処理
                            size_t pos = 0;
                            std::string result;
                            while (pos < format_str.length()) {
                                if (format_str[pos] == '{') {
                                    if (pos + 1 < format_str.length() &&
                                        format_str[pos + 1] == '{') {
                                        // {{ はエスケープ
                                        result += '{';
                                        pos += 2;
                                        continue;
                                    }

                                    size_t end_pos = format_str.find('}', pos);
                                    if (end_pos != std::string::npos &&
                                        arg_index < data.args.size()) {
                                        // フォーマット指定子を抽出
                                        std::string spec =
                                            format_str.substr(pos + 1, end_pos - pos - 1);
                                        std::string arg = operand_to_cpp(*data.args[arg_index]);

                                        // フォーマット指定子に基づいて処理
                                        if (spec.empty()) {
                                            // 通常の置換 {}
                                            result +=
                                                "\"; print_value(" + arg + "); std::cout << \"";
                                        } else if (spec[0] == ':') {
                                            // フォーマット指定子のみ {:x}, {:.2} など
                                            std::string fmt = spec.substr(1);

                                            if (fmt == "x") {
                                                result += "\" << std::hex << " + arg +
                                                          " << std::dec << \"";
                                            } else if (fmt == "X") {
                                                result += "\" << std::hex << std::uppercase << " +
                                                          arg +
                                                          " << std::nouppercase << std::dec << \"";
                                            } else if (fmt == "b") {
                                                result +=
                                                    "\" << [&]{ std::bitset<32> bs(" + arg +
                                                    "); std::string s = bs.to_string(); s.erase(0, "
                                                    "s.find_first_not_of('0')); return s.empty() ? "
                                                    "\"0\" : s; }() << \"";
                                            } else if (fmt == "o") {
                                                result += "\" << std::oct << " + arg +
                                                          " << std::dec << \"";
                                            } else if (fmt.size() > 1 && fmt[0] == '.') {
                                                // 小数点精度
                                                std::string precision = fmt.substr(1);
                                                result += "\" << std::fixed << std::setprecision(" +
                                                          precision + ") << " + arg + " << \"";
                                            } else if (fmt == "e") {
                                                // 科学記法（小文字）- デフォルト精度6にリセット
                                                result +=
                                                    "\" << std::setprecision(6) << std::scientific "
                                                    "<< " +
                                                    arg + " << \"";
                                            } else if (fmt == "E") {
                                                // 科学記法（大文字）- デフォルト精度6にリセット
                                                result +=
                                                    "\" << std::setprecision(6) << std::scientific "
                                                    "<< std::uppercase << " +
                                                    arg + " << std::nouppercase << \"";
                                            } else if (fmt[0] == '<' || fmt[0] == '>' ||
                                                       fmt[0] == '^') {
                                                // アライメント
                                                char align = fmt[0];
                                                std::string width = fmt.substr(1);
                                                if (align == '<') {
                                                    result += "\" << std::left << std::setw(" +
                                                              width + ") << " + arg + " << \"";
                                                } else if (align == '>') {
                                                    result += "\" << std::right << std::setw(" +
                                                              width + ") << " + arg + " << \"";
                                                } else if (align == '^') {
                                                    // 中央揃えの実装
                                                    result +=
                                                        "\" << [&]{ std::string s = "
                                                        "to_string_fmt(" +
                                                        arg +
                                                        "); "
                                                        "int pad = " +
                                                        width +
                                                        " - s.length(); "
                                                        "int left = pad / 2; int right = pad - "
                                                        "left; "
                                                        "return std::string(left, ' ') + s + "
                                                        "std::string(right, ' '); }() << \"";
                                                }
                                            } else if (fmt.size() > 2 && fmt[0] == '0' &&
                                                       fmt[1] == '>') {
                                                // ゼロパディング
                                                std::string width = fmt.substr(2);
                                                result += "\" << std::setfill('0') << std::setw(" +
                                                          width + ") << " + arg +
                                                          " << std::setfill(' ') << \"";
                                            } else {
                                                // 不明なフォーマット
                                                result +=
                                                    "\"; print_value(" + arg + "); std::cout << \"";
                                            }
                                        } else if (spec.find(':') != std::string::npos) {
                                            // 変数名:フォーマット指定子 {name:x} など
                                            size_t colon_pos = spec.find(':');
                                            std::string fmt = spec.substr(colon_pos + 1);

                                            // 上記と同じフォーマット処理（変数名は無視して処理）
                                            if (fmt == "x") {
                                                result += "\" << std::hex << " + arg +
                                                          " << std::dec << \"";
                                            } else if (fmt == "X") {
                                                result += "\" << std::hex << std::uppercase << " +
                                                          arg +
                                                          " << std::nouppercase << std::dec << \"";
                                            } else if (fmt == "b") {
                                                result +=
                                                    "\" << [&]{ std::bitset<32> bs(" + arg +
                                                    "); std::string s = bs.to_string(); s.erase(0, "
                                                    "s.find_first_not_of('0')); return s.empty() ? "
                                                    "\"0\" : s; }() << \"";
                                            } else if (fmt == "o") {
                                                result += "\" << std::oct << " + arg +
                                                          " << std::dec << \"";
                                            } else if (fmt.size() > 1 && fmt[0] == '.') {
                                                std::string precision = fmt.substr(1);
                                                result += "\" << std::fixed << std::setprecision(" +
                                                          precision + ") << " + arg + " << \"";
                                            } else {
                                                result +=
                                                    "\"; print_value(" + arg + "); std::cout << \"";
                                            }
                                        } else {
                                            // 変数名のみ {name} など
                                            result +=
                                                "\"; print_value(" + arg + "); std::cout << \"";
                                        }

                                        arg_index++;
                                        pos = end_pos + 1;
                                    } else {
                                        result += format_str[pos];
                                        pos++;
                                    }
                                } else if (format_str[pos] == '}' &&
                                           pos + 1 < format_str.length() &&
                                           format_str[pos + 1] == '}') {
                                    // }} はエスケープ
                                    result += '}';
                                    pos += 2;
                                } else {
                                    result += format_str[pos];
                                    pos++;
                                }
                            }

                            // 生成されたコード
                            if (arg_index > 1) {
                                // フォーマット処理された出力
                                emit_line("std::cout << \"" + result + "\"" +
                                          (func_name == "println" ? " << std::endl;"
                                                                  : " << std::flush;"));
                                emit_line("__bb = " + std::to_string(data.success) + ";");
                                emit_line("break;");
                                break;
                            }
                        }

                        // 通常の関数呼び出し
                        std::string call = func_name + "(";
                        bool first = true;
                        for (const auto& arg : data.args) {
                            if (!first)
                                call += ", ";
                            first = false;
                            call += operand_to_cpp(*arg);
                        }
                        call += ")";

                        // println/printは値を返さないので代入はスキップ
                        if (data.destination && func_name != "println" && func_name != "print") {
                            emit_line(place_to_cpp(*data.destination) + " = " + call + ";");
                        } else {
                            emit_line(call + ";");
                        }

                        // 次のブロックへ遷移
                        emit_line("__bb = " + std::to_string(data.success) + ";");
                        emit_line("break;");
                    }  // 内部スコープの終了
                    break;
                }  // Call caseの終了

                case mir::MirTerminator::Unreachable: {
                    emit_line("std::abort();");
                    break;
                }

                default: {
                    emit_line("// Unknown terminator kind");
                    break;
                }
            }
        }
    };

};  // Extra semicolon to fix compiler error

}  // namespace cm::codegen