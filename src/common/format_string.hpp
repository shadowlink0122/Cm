#pragma once

#include <any>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <typeinfo>
#include <variant>
#include <vector>

namespace cm {

// フォーマット指定子の種類
enum class FormatSpec {
    Default,      // デフォルト表示
    Binary,       // :b - 2進数
    Octal,        // :o - 8進数
    Hex,          // :x - 16進数（小文字）
    HexUpper,     // :X - 16進数（大文字）
    Exponential,  // :e - 指数表記（小文字）
    ExpUpper,     // :E - 指数表記（大文字）
};

// アライメント指定
enum class Alignment {
    None,
    Left,    // <
    Right,   // >
    Center,  // ^
};

// プレースホルダーの情報
struct Placeholder {
    enum Type {
        Positional,  // {} または {0}, {1} など
        Named,       // {name}
    };

    Type type;
    size_t position;     // 位置引数のインデックス
    std::string name;    // 名前付き引数の名前
    FormatSpec spec;     // フォーマット指定子
    Alignment align;     // アライメント
    size_t width;        // 最小幅
    size_t precision;    // 精度（小数点以下の桁数）
    char fill_char;      // パディング文字
    bool has_width;      // 幅指定があるか
    bool has_precision;  // 精度指定があるか
};

// フォーマット文字列パーサー
class FormatStringParser {
   public:
    struct ParseResult {
        std::vector<std::string> literal_parts;  // リテラル部分
        std::vector<Placeholder> placeholders;   // プレースホルダー
        bool success;
        std::string error_message;
    };

    static ParseResult parse(const std::string& format) {
        ParseResult result;
        result.success = true;

        std::string current_literal;
        size_t pos = 0;
        size_t next_positional = 0;

        while (pos < format.size()) {
            if (format[pos] == '{') {
                if (pos + 1 < format.size() && format[pos + 1] == '{') {
                    // エスケープされた '{'
                    current_literal += '{';
                    pos += 2;
                } else {
                    // プレースホルダーの開始
                    result.literal_parts.push_back(current_literal);
                    current_literal.clear();

                    auto [placeholder, end_pos] = parse_placeholder(format, pos);
                    if (!placeholder.has_value()) {
                        result.success = false;
                        result.error_message =
                            "Invalid placeholder at position " + std::to_string(pos);
                        return result;
                    }

                    // 位置引数のインデックスを自動割り当て
                    if (placeholder->type == Placeholder::Positional &&
                        placeholder->position == SIZE_MAX) {
                        placeholder->position = next_positional++;
                    }

                    result.placeholders.push_back(*placeholder);
                    pos = end_pos;
                }
            } else if (format[pos] == '}') {
                if (pos + 1 < format.size() && format[pos + 1] == '}') {
                    // エスケープされた '}'
                    current_literal += '}';
                    pos += 2;
                } else {
                    // 閉じカッコが単独で現れた（エラー）
                    result.success = false;
                    result.error_message = "Unmatched '}' at position " + std::to_string(pos);
                    return result;
                }
            } else {
                current_literal += format[pos];
                pos++;
            }
        }

        // 最後のリテラル部分を追加
        result.literal_parts.push_back(current_literal);

        return result;
    }

   private:
    static std::pair<std::optional<Placeholder>, size_t> parse_placeholder(
        const std::string& format, size_t start) {
        Placeholder placeholder;
        placeholder.type = Placeholder::Positional;
        placeholder.position = SIZE_MAX;  // 未指定
        placeholder.spec = FormatSpec::Default;
        placeholder.align = Alignment::None;
        placeholder.width = 0;
        placeholder.precision = 6;  // デフォルト精度
        placeholder.fill_char = ' ';
        placeholder.has_width = false;
        placeholder.has_precision = false;

        size_t pos = start + 1;  // '{' の次から開始

        // '}' を探す
        size_t end = format.find('}', pos);
        if (end == std::string::npos) {
            return {std::nullopt, pos};
        }

        std::string content = format.substr(pos, end - pos);

        // コロンでフォーマット指定子を分離
        size_t colon_pos = content.find(':');
        std::string arg_part =
            (colon_pos != std::string::npos) ? content.substr(0, colon_pos) : content;
        std::string spec_part =
            (colon_pos != std::string::npos) ? content.substr(colon_pos + 1) : "";

        // 引数部分をパース
        if (arg_part.empty()) {
            // {} - 位置引数（自動インデックス）
            placeholder.type = Placeholder::Positional;
        } else if (std::isdigit(arg_part[0])) {
            // {0}, {1} - インデックス指定の位置引数
            placeholder.type = Placeholder::Positional;
            placeholder.position = std::stoul(arg_part);
        } else {
            // {name} - 名前付き引数（現在は位置引数として扱う）
            // 将来的に真の名前付き引数をサポートするまでの暫定実装
            placeholder.type = Placeholder::Positional;
            placeholder.name = arg_part;
            // 位置は自動割り当て（SIZE_MAXのままにして、後で割り当てる）
        }

        // フォーマット指定子をパース
        if (!spec_part.empty()) {
            parse_format_spec(spec_part, placeholder);
        }

        return {placeholder, end + 1};
    }

    static void parse_format_spec(const std::string& spec, Placeholder& placeholder) {
        size_t pos = 0;

        // アライメントとパディング文字
        if (pos < spec.size() && spec.size() > 1) {
            char next = spec[pos + 1];
            if (next == '<' || next == '>' || next == '^') {
                placeholder.fill_char = spec[pos];
                placeholder.align = (next == '<')   ? Alignment::Left
                                    : (next == '>') ? Alignment::Right
                                                    : Alignment::Center;
                pos += 2;
            } else if (spec[pos] == '<' || spec[pos] == '>' || spec[pos] == '^') {
                placeholder.align = (spec[pos] == '<')   ? Alignment::Left
                                    : (spec[pos] == '>') ? Alignment::Right
                                                         : Alignment::Center;
                pos++;
            }
        }

        // 幅と精度
        std::string num_str;
        while (pos < spec.size() && std::isdigit(spec[pos])) {
            num_str += spec[pos++];
        }
        if (!num_str.empty()) {
            placeholder.width = std::stoul(num_str);
            placeholder.has_width = true;
        }

        if (pos < spec.size() && spec[pos] == '.') {
            pos++;  // '.' をスキップ
            num_str.clear();
            while (pos < spec.size() && std::isdigit(spec[pos])) {
                num_str += spec[pos++];
            }
            if (!num_str.empty()) {
                placeholder.precision = std::stoul(num_str);
                placeholder.has_precision = true;
            }
        }

        // フォーマット型
        if (pos < spec.size()) {
            switch (spec[pos]) {
                case 'b':
                    placeholder.spec = FormatSpec::Binary;
                    break;
                case 'o':
                    placeholder.spec = FormatSpec::Octal;
                    break;
                case 'x':
                    placeholder.spec = FormatSpec::Hex;
                    break;
                case 'X':
                    placeholder.spec = FormatSpec::HexUpper;
                    break;
                case 'e':
                    placeholder.spec = FormatSpec::Exponential;
                    break;
                case 'E':
                    placeholder.spec = FormatSpec::ExpUpper;
                    break;
                default:
                    break;
            }
        }
    }
};

// 値をフォーマット文字列に従って文字列化
class FormatStringFormatter {
   public:
    static std::string format(const std::string& format_str, const std::vector<std::any>& args) {
        auto parse_result = FormatStringParser::parse(format_str);
        if (!parse_result.success) {
            return "Format error: " + parse_result.error_message;
        }

        std::stringstream result;
        size_t literal_index = 0;

        for (const auto& placeholder : parse_result.placeholders) {
            // リテラル部分を出力
            if (literal_index < parse_result.literal_parts.size()) {
                result << parse_result.literal_parts[literal_index++];
            }

            // プレースホルダーに対応する値を取得
            if (placeholder.type == Placeholder::Positional) {
                if (placeholder.position < args.size()) {
                    result << format_value(args[placeholder.position], placeholder);
                } else {
                    result << "{missing}";
                }
            } else {
                // 名前付き引数はここでは未対応（将来的に実装）
                result << "{" << placeholder.name << "}";
            }
        }

        // 最後のリテラル部分
        if (literal_index < parse_result.literal_parts.size()) {
            result << parse_result.literal_parts[literal_index];
        }

        return result.str();
    }

   private:
    static std::string format_value(const std::any& value, const Placeholder& placeholder) {
        std::stringstream ss;

        // 数値型のフォーマット
        if (value.type() == typeid(int64_t)) {
            int64_t v = std::any_cast<int64_t>(value);
            return format_integer(v, placeholder);
        } else if (value.type() == typeid(int32_t)) {
            int32_t v = std::any_cast<int32_t>(value);
            return format_integer(v, placeholder);
        } else if (value.type() == typeid(double)) {
            double v = std::any_cast<double>(value);
            return format_floating(v, placeholder);
        } else if (value.type() == typeid(float)) {
            float v = std::any_cast<float>(value);
            return format_floating(v, placeholder);
        } else if (value.type() == typeid(bool)) {
            bool v = std::any_cast<bool>(value);
            ss << (v ? "true" : "false");
        } else if (value.type() == typeid(char)) {
            char v = std::any_cast<char>(value);
            ss << v;  // 文字として出力
        } else if (value.type() == typeid(std::string)) {
            ss << std::any_cast<std::string>(value);
        } else {
            ss << "(unknown type)";
        }

        return apply_alignment(ss.str(), placeholder);
    }

    template <typename T>
    static std::string format_integer(T value, const Placeholder& placeholder) {
        std::stringstream ss;

        switch (placeholder.spec) {
            case FormatSpec::Binary: {
                // 2進数（プレフィックスなし）
                for (int i = sizeof(T) * 8 - 1; i >= 0; --i) {
                    if ((value >> i) & 1 || i == 0) {
                        for (; i >= 0; --i) {
                            ss << ((value >> i) & 1);
                        }
                        break;
                    }
                }
                break;
            }
            case FormatSpec::Octal:
                ss << std::oct << value;
                break;
            case FormatSpec::Hex:
                ss << std::hex << value;
                break;
            case FormatSpec::HexUpper:
                ss << std::hex << std::uppercase << value;
                break;
            default:
                ss << value;
                break;
        }

        return apply_alignment(ss.str(), placeholder);
    }

    template <typename T>
    static std::string format_floating(T value, const Placeholder& placeholder) {
        std::stringstream ss;

        if (placeholder.has_precision) {
            ss << std::fixed << std::setprecision(placeholder.precision);
        }

        switch (placeholder.spec) {
            case FormatSpec::Exponential:
                ss << std::scientific << value;
                break;
            case FormatSpec::ExpUpper:
                ss << std::scientific << std::uppercase << value;
                break;
            default:
                ss << value;
                break;
        }

        return apply_alignment(ss.str(), placeholder);
    }

    static std::string apply_alignment(const std::string& str, const Placeholder& placeholder) {
        if (!placeholder.has_width || str.length() >= placeholder.width) {
            return str;
        }

        size_t padding = placeholder.width - str.length();
        std::string pad(padding, placeholder.fill_char);

        switch (placeholder.align) {
            case Alignment::Left:
                return str + pad;
            case Alignment::Right:
            case Alignment::None:  // デフォルトは右寄せ
                return pad + str;
            case Alignment::Center: {
                size_t left_pad = padding / 2;
                size_t right_pad = padding - left_pad;
                return std::string(left_pad, placeholder.fill_char) + str +
                       std::string(right_pad, placeholder.fill_char);
            }
        }

        return str;
    }
};

}  // namespace cm