#pragma once

#include "../frontend/ast/nodes.hpp"
#include "nodes.hpp"

#include <memory>
#include <string>
#include <vector>

namespace cm::hir {

// 文字列埋め込み処理用のユーティリティ
class StringInterpolationProcessor {
   public:
    // 埋め込み変数の情報
    struct InterpolatedVar {
        std::string name;         // 変数名
        std::string format_spec;  // フォーマット指定子（オプション）
        size_t position;          // 元の文字列内の位置
    };

    // 文字列に埋め込み式が含まれているかチェック
    static bool hasInterpolation(const std::string& str) {
        size_t pos = 0;
        while ((pos = str.find('{', pos)) != std::string::npos) {
            // エスケープ {{ をスキップ
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

    // 文字列から埋め込み変数を抽出
    static std::vector<InterpolatedVar> extractInterpolations(const std::string& str) {
        std::vector<InterpolatedVar> vars;
        size_t pos = 0;

        while ((pos = str.find('{', pos)) != std::string::npos) {
            // エスケープ {{ をスキップ
            if (pos + 1 < str.size() && str[pos + 1] == '{') {
                pos += 2;
                continue;
            }

            size_t end = str.find('}', pos);
            if (end != std::string::npos) {
                std::string content = str.substr(pos + 1, end - pos - 1);

                InterpolatedVar var;
                var.position = pos;

                // フォーマット指定子をチェック
                size_t colon = content.find(':');
                if (colon != std::string::npos) {
                    var.name = content.substr(0, colon);
                    var.format_spec = content.substr(colon + 1);
                } else {
                    var.name = content;
                    var.format_spec = "";
                }

                // 空でない変数名のみ追加
                if (!var.name.empty() && !std::isdigit(var.name[0])) {
                    vars.push_back(var);
                }

                pos = end + 1;
            } else {
                break;
            }
        }

        return vars;
    }

    // 文字列を部分に分割（リテラル部分と埋め込み部分）
    struct StringPart {
        enum Type { LITERAL, INTERPOLATION };
        Type type;
        std::string content;
        std::string format_spec;  // INTERPOLATION の場合のみ使用
    };

    static std::vector<StringPart> splitInterpolatedString(const std::string& str) {
        std::vector<StringPart> parts;
        auto vars = extractInterpolations(str);

        if (vars.empty()) {
            // 埋め込みがない場合は全体を1つのリテラルとして返す
            parts.push_back({StringPart::LITERAL, str, ""});
            return parts;
        }

        size_t last_pos = 0;
        for (const auto& var : vars) {
            // 変数の前の文字列部分
            if (var.position > last_pos) {
                std::string literal = str.substr(last_pos, var.position - last_pos);
                // {{ を { に、}} を } に置換
                size_t pos = 0;
                while ((pos = literal.find("{{", pos)) != std::string::npos) {
                    literal.replace(pos, 2, "{");
                    pos++;
                }
                pos = 0;
                while ((pos = literal.find("}}", pos)) != std::string::npos) {
                    literal.replace(pos, 2, "}");
                    pos++;
                }
                parts.push_back({StringPart::LITERAL, literal, ""});
            }

            // 埋め込み変数
            parts.push_back({StringPart::INTERPOLATION, var.name, var.format_spec});

            // 次の開始位置を更新（{変数名} または {変数名:format} の終わりの次）
            last_pos = str.find('}', var.position) + 1;
        }

        // 最後の部分
        if (last_pos < str.size()) {
            std::string literal = str.substr(last_pos);
            // {{ を { に、}} を } に置換
            size_t pos = 0;
            while ((pos = literal.find("{{", pos)) != std::string::npos) {
                literal.replace(pos, 2, "{");
                pos++;
            }
            pos = 0;
            while ((pos = literal.find("}}", pos)) != std::string::npos) {
                literal.replace(pos, 2, "}");
                pos++;
            }
            parts.push_back({StringPart::LITERAL, literal, ""});
        }

        return parts;
    }

    // HIR式を生成（文字列連結式として）
    static HirExprPtr createInterpolatedStringExpr(
        const std::string& str,
        const std::function<HirExprPtr(const std::string&)>& resolveVariable) {
        auto parts = splitInterpolatedString(str);

        if (parts.size() == 1 && parts[0].type == StringPart::LITERAL) {
            // 埋め込みがない単純な文字列リテラル
            auto lit = std::make_unique<HirLiteral>();
            lit->value = parts[0].content;
            return std::make_unique<HirExpr>(std::move(lit), make_string());
        }

        // 文字列連結式を構築
        HirExprPtr result = nullptr;

        for (const auto& part : parts) {
            HirExprPtr part_expr;

            if (part.type == StringPart::LITERAL) {
                // リテラル部分
                auto lit = std::make_unique<HirLiteral>();
                lit->value = part.content;
                part_expr = std::make_unique<HirExpr>(std::move(lit), make_string());
            } else {
                // 埋め込み変数部分
                auto var_expr = resolveVariable(part.content);

                // フォーマット指定がある場合はフォーマット関数を適用
                if (!part.format_spec.empty()) {
                    part_expr = applyFormat(std::move(var_expr), part.format_spec);
                } else {
                    // toString変換
                    part_expr = convertToString(std::move(var_expr));
                }
            }

            if (!result) {
                result = std::move(part_expr);
            } else {
                // 文字列連結
                result = createStringConcat(std::move(result), std::move(part_expr));
            }
        }

        return result;
    }

   private:
    // 値を文字列に変換
    static HirExprPtr convertToString(HirExprPtr expr) {
        // toString関数呼び出しを生成
        auto call = std::make_unique<HirCall>();
        call->func_name = "toString";
        call->args.push_back(std::move(expr));
        return std::make_unique<HirExpr>(std::move(call), make_string());
    }

    // フォーマット適用
    static HirExprPtr applyFormat(HirExprPtr expr, const std::string& format_spec) {
        // フォーマット関数呼び出しを生成
        auto call = std::make_unique<HirCall>();

        // フォーマット指定子に応じた関数を選択
        if (format_spec == "x") {
            call->func_name = "formatHex";
        } else if (format_spec == "X") {
            call->func_name = "formatHexUpper";
        } else if (format_spec == "b") {
            call->func_name = "formatBinary";
        } else if (format_spec == "o") {
            call->func_name = "formatOctal";
        } else if (format_spec.find('.') != std::string::npos) {
            call->func_name = "formatDecimal";
            // 精度を追加パラメータとして渡す
        } else {
            call->func_name = "toString";
        }

        call->args.push_back(std::move(expr));
        return std::make_unique<HirExpr>(std::move(call), make_string());
    }

    // 文字列連結
    static HirExprPtr createStringConcat(HirExprPtr left, HirExprPtr right) {
        auto binop = std::make_unique<HirBinary>();
        binop->op = HirBinaryOp::Add;
        binop->lhs = std::move(left);
        binop->rhs = std::move(right);
        return std::make_unique<HirExpr>(std::move(binop), make_string());
    }
};

}  // namespace cm::hir