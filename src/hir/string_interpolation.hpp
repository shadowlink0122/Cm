#pragma once

#include "nodes.hpp"

#include <functional>
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
    static bool hasInterpolation(const std::string& str);

    // 文字列から埋め込み変数を抽出
    static std::vector<InterpolatedVar> extractInterpolations(const std::string& str);

    // 文字列を部分に分割（リテラル部分と埋め込み部分）
    struct StringPart {
        enum Type { LITERAL, INTERPOLATION };
        Type type;
        std::string content;
        std::string format_spec;  // INTERPOLATION の場合のみ使用
    };

    static std::vector<StringPart> splitInterpolatedString(const std::string& str);

    // HIR式を生成（文字列連結式として）
    static HirExprPtr createInterpolatedStringExpr(
        const std::string& str,
        const std::function<HirExprPtr(const std::string&)>& resolveVariable);

   private:
    // 値を文字列に変換
    static HirExprPtr convertToString(HirExprPtr expr);

    // フォーマット適用
    static HirExprPtr applyFormat(HirExprPtr expr, const std::string& format_spec);

    // 文字列連結
    static HirExprPtr createStringConcat(HirExprPtr left, HirExprPtr right);
};

}  // namespace cm::hir
