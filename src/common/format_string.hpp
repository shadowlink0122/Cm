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

    static ParseResult parse(const std::string& format);

   private:
    static std::pair<std::optional<Placeholder>, size_t> parse_placeholder(
        const std::string& format, size_t start);

    static void parse_format_spec(const std::string& spec, Placeholder& placeholder);
};

// 値をフォーマット文字列に従って文字列化
class FormatStringFormatter {
   public:
    static std::string format(const std::string& format_str, const std::vector<std::any>& args);

   private:
    static std::string format_value(const std::any& value, const Placeholder& placeholder);

    // テンプレートメソッド: ヘッダーに実装を残す
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

    // テンプレートメソッド: ヘッダーに実装を残す
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

    static std::string apply_alignment(const std::string& str, const Placeholder& placeholder);
};

}  // namespace cm