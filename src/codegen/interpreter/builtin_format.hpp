#pragma once

#include "types.hpp"

#include <cstdio>
#include <sstream>
#include <string>

namespace cm::mir::interp {

/// フォーマット処理のユーティリティ関数
class FormatUtils {
   public:
    /// フォーマット文字列を処理
    static std::string format_with_args(const std::string& format, const std::vector<Value>& args,
                                        int64_t argc, size_t start_idx) {
        std::string result;
        size_t pos = 0;
        int arg_index = 0;

        while (pos < format.length()) {
            // エスケープされた {{ を処理
            if (pos + 1 < format.length() && format[pos] == '{' && format[pos + 1] == '{') {
                result += '{';
                pos += 2;
                continue;
            }
            // エスケープされた }} を処理
            if (pos + 1 < format.length() && format[pos] == '}' && format[pos + 1] == '}') {
                result += '}';
                pos += 2;
                continue;
            }

            if (format[pos] == '{') {
                size_t end = pos + 1;
                while (end < format.length() && format[end] != '}') {
                    end++;
                }

                if (end < format.length() && arg_index < argc &&
                    start_idx + arg_index < args.size()) {
                    std::string spec = format.substr(pos + 1, end - pos - 1);
                    const auto& arg = args[start_idx + arg_index];
                    result += format_value(arg, spec);
                    arg_index++;
                }
                pos = (end < format.length()) ? end + 1 : format.length();
            } else {
                result += format[pos];
                pos++;
            }
        }

        return result;
    }

    /// 値をフォーマット
    static std::string format_value(const Value& arg, const std::string& spec) {
        // フォーマット指定子を解析
        std::string alignment;
        char fill_char = ' ';
        int width = 0;
        int precision = -1;
        std::string type_spec;

        size_t spec_pos = 0;
        if (!spec.empty() && spec[0] == ':') {
            spec_pos = 1;

            // ゼロパディング
            if (spec_pos < spec.length() && spec[spec_pos] == '0') {
                fill_char = '0';
                spec_pos++;
            }

            // アラインメント
            if (spec_pos < spec.length() &&
                (spec[spec_pos] == '<' || spec[spec_pos] == '>' || spec[spec_pos] == '^')) {
                alignment = spec[spec_pos];
                spec_pos++;
            }

            // 幅
            std::string width_str;
            while (spec_pos < spec.length() && std::isdigit(spec[spec_pos])) {
                width_str += spec[spec_pos++];
            }
            if (!width_str.empty()) {
                width = std::stoi(width_str);
            }

            // 精度
            if (spec_pos < spec.length() && spec[spec_pos] == '.') {
                spec_pos++;
                std::string prec_str;
                while (spec_pos < spec.length() && std::isdigit(spec[spec_pos])) {
                    prec_str += spec[spec_pos++];
                }
                if (!prec_str.empty()) {
                    precision = std::stoi(prec_str);
                }
            }

            // 型指定子
            if (spec_pos < spec.length()) {
                type_spec = spec.substr(spec_pos);
            }
        }

        std::string formatted = format_value_with_type(arg, type_spec, precision);

        // 幅とアラインメントを適用
        if (width > 0 && static_cast<int>(formatted.length()) < width) {
            int padding = width - formatted.length();
            if (alignment == ">") {
                formatted = std::string(padding, fill_char) + formatted;
            } else if (alignment == "^") {
                int left_pad = padding / 2;
                int right_pad = padding - left_pad;
                formatted = std::string(left_pad, ' ') + formatted + std::string(right_pad, ' ');
            } else {
                formatted = formatted + std::string(padding, ' ');
            }
        }

        return formatted;
    }

    /// 型指定子に従って値をフォーマット
    static std::string format_value_with_type(const Value& arg, const std::string& type_spec,
                                              int precision) {
        if (arg.type() == typeid(int64_t)) {
            int64_t val = std::any_cast<int64_t>(arg);
            if (type_spec == "x") {
                char buf[32];
                snprintf(buf, sizeof(buf), "%llx", (long long)val);
                return buf;
            } else if (type_spec == "X") {
                char buf[32];
                snprintf(buf, sizeof(buf), "%llX", (long long)val);
                return buf;
            } else if (type_spec == "b") {
                if (val == 0)
                    return "0";
                std::string binary;
                int64_t temp = val;
                while (temp > 0) {
                    binary = (char)('0' + (temp & 1)) + binary;
                    temp >>= 1;
                }
                return binary;
            } else if (type_spec == "o") {
                char buf[32];
                snprintf(buf, sizeof(buf), "%llo", (long long)val);
                return buf;
            }
            return std::to_string(val);
        } else if (arg.type() == typeid(uint64_t)) {
            uint64_t val = std::any_cast<uint64_t>(arg);
            if (type_spec == "x") {
                char buf[32];
                snprintf(buf, sizeof(buf), "%llx", (unsigned long long)val);
                return buf;
            } else if (type_spec == "X") {
                char buf[32];
                snprintf(buf, sizeof(buf), "%llX", (unsigned long long)val);
                return buf;
            } else if (type_spec == "b") {
                if (val == 0)
                    return "0";
                std::string binary;
                uint64_t temp = val;
                while (temp > 0) {
                    binary = (char)('0' + (temp & 1)) + binary;
                    temp >>= 1;
                }
                return binary;
            } else if (type_spec == "o") {
                char buf[32];
                snprintf(buf, sizeof(buf), "%llo", (unsigned long long)val);
                return buf;
            }
            return std::to_string(val);
        } else if (arg.type() == typeid(double)) {
            double val = std::any_cast<double>(arg);
            char buf[64];

            // 科学記法（小文字）
            if (type_spec == "e") {
                if (precision >= 0) {
                    snprintf(buf, sizeof(buf), "%.*e", precision, val);
                } else {
                    snprintf(buf, sizeof(buf), "%e", val);
                }
                return buf;
            }
            // 科学記法（大文字）
            else if (type_spec == "E") {
                if (precision >= 0) {
                    snprintf(buf, sizeof(buf), "%.*E", precision, val);
                } else {
                    snprintf(buf, sizeof(buf), "%E", val);
                }
                return buf;
            }
            // 固定小数点
            else if (type_spec == "f" || type_spec == "F") {
                if (precision >= 0) {
                    snprintf(buf, sizeof(buf), "%.*f", precision, val);
                } else {
                    snprintf(buf, sizeof(buf), "%f", val);
                }
                return buf;
            }
            // デフォルト
            else if (precision >= 0) {
                snprintf(buf, sizeof(buf), "%.*f", precision, val);
                return buf;
            } else if (val == static_cast<int64_t>(val)) {
                return std::to_string(static_cast<int64_t>(val));
            } else {
                snprintf(buf, sizeof(buf), "%g", val);
                return buf;
            }
        } else if (arg.type() == typeid(bool)) {
            return std::any_cast<bool>(arg) ? "true" : "false";
        } else if (arg.type() == typeid(char)) {
            return std::string(1, std::any_cast<char>(arg));
        } else if (arg.type() == typeid(std::string)) {
            return std::any_cast<std::string>(arg);
        } else if (arg.type() == typeid(PointerValue)) {
            // ポインタ値（アドレス）を表示
            PointerValue ptr = std::any_cast<PointerValue>(arg);
            char buf[32];
            uintptr_t base_addr = 0x7fff0000;
            uintptr_t addr = base_addr + (static_cast<uintptr_t>(ptr.target_local) * 8);
            if (ptr.array_index) {
                addr += (*ptr.array_index * 8);
            }

            if (type_spec == "x") {
                snprintf(buf, sizeof(buf), "0x%lx", (unsigned long)addr);
            } else if (type_spec == "X") {
                snprintf(buf, sizeof(buf), "0x%lX", (unsigned long)addr);
            } else {
                snprintf(buf, sizeof(buf), "%lu", (unsigned long)addr);
            }
            return buf;
        }
        return "{}";
    }

    /// 値を文字列に変換
    static std::string value_to_string(const Value& val) {
        if (val.type() == typeid(std::string)) {
            return std::any_cast<std::string>(val);
        } else if (val.type() == typeid(int64_t)) {
            return std::to_string(std::any_cast<int64_t>(val));
        } else if (val.type() == typeid(uint64_t)) {
            return std::to_string(std::any_cast<uint64_t>(val));
        } else if (val.type() == typeid(double)) {
            return std::to_string(std::any_cast<double>(val));
        } else if (val.type() == typeid(char)) {
            return std::string(1, std::any_cast<char>(val));
        } else if (val.type() == typeid(bool)) {
            return std::any_cast<bool>(val) ? "true" : "false";
        } else if (val.type() == typeid(PointerValue)) {
            PointerValue ptr = std::any_cast<PointerValue>(val);
            char buf[32];
            uintptr_t base_addr = 0x7fff0000;
            uintptr_t addr = base_addr + (static_cast<uintptr_t>(ptr.target_local) * 8);
            if (ptr.array_index) {
                addr += (*ptr.array_index * 8);
            }
            snprintf(buf, sizeof(buf), "%lu", (unsigned long)addr);
            return buf;
        }
        return "";
    }
};

}  // namespace cm::mir::interp
