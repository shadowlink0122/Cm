#pragma once

// SV式ツリー（svバックエンド式ツリー化 Phase 1）
//
// 「一度テキストを出力してから文字列操作で括弧を補正する」旧設計を置き換える
// ための小さな式AST。優先順位括弧はプリンタが構造から一意に決定するため、
// get_outermost_operator 等のテキストヒューリスティックが不要になる。
// （docs/design/sv_backend_missing_features.md 項目11 / docs/archive/013 §4.3-1）

#include <memory>
#include <string>
#include <vector>

namespace cm::codegen::sv {

class SVExpr;
using SVExprPtr = std::shared_ptr<SVExpr>;

// SystemVerilog式の優先順位（大きいほど強く結合する）
// IEEE 1800-2017 Table 11-2 に基づく
int sv_operator_precedence(const std::string& op);

// 演算子が結合法則を満たすか（a op (b op c) == (a op b) op c）。
// 満たす場合、同一演算子の右オペランドの括弧を省略できる
bool sv_operator_is_associative(const std::string& op);

class SVExpr {
   public:
    enum class Kind {
        Atom,   // 信号名・リテラル・キャスト済みテキスト等の完結した原子
        Unary,  // 前置単項演算（- ~ ! 等）
        Binary,  // 二項演算
    };

    // ファクトリ
    static SVExprPtr atom(std::string text);
    static SVExprPtr unary(std::string op, SVExprPtr operand);
    static SVExprPtr binary(std::string op, SVExprPtr lhs, SVExprPtr rhs);

    Kind kind() const { return kind_; }
    const std::string& op() const { return op_; }
    const std::string& text() const { return text_; }

    // 優先順位に基づく最小限の括弧で式全体を文字列化する
    std::string to_string() const;

   private:
    SVExpr() = default;

    // 親コンテキストの優先順位を考慮して文字列化する。
    // parent_prec: 親演算子の優先順位（トップレベルは-1）
    // is_right_operand: 親の右オペランド位置か（左結合演算子の括弧判定用）
    void print(std::string& out, int parent_prec, bool is_right_operand) const;

    // この式自身の優先順位（Atomは最強）
    int precedence() const;

    Kind kind_ = Kind::Atom;
    std::string op_;    // Unary/Binary用（空白なしの演算子、例: "+", ">>>"）
    std::string text_;  // Atom用
    std::vector<SVExprPtr> operands_;
};

}  // namespace cm::codegen::sv
