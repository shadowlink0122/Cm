#include "expr_tree.hpp"

#include <string>
#include <utility>

namespace cm::codegen::sv {

// IEEE 1800-2017 Table 11-2 の演算子優先順位（大きいほど強く結合）
int sv_operator_precedence(const std::string& op) {
    if (op == "!" || op == "~" || op == "-u" || op == "+u") {
        return 11;  // 単項演算子
    }
    if (op == "**") {
        return 10;
    }
    if (op == "*" || op == "/" || op == "%") {
        return 9;
    }
    if (op == "+" || op == "-") {
        return 8;
    }
    if (op == "<<" || op == ">>" || op == "<<<" || op == ">>>") {
        return 7;
    }
    if (op == "<" || op == "<=" || op == ">" || op == ">=") {
        return 6;
    }
    if (op == "==" || op == "!=" || op == "===" || op == "!==") {
        return 5;
    }
    if (op == "&") {
        return 4;
    }
    if (op == "^" || op == "~^" || op == "^~") {
        return 3;
    }
    if (op == "|") {
        return 2;
    }
    if (op == "&&") {
        return 1;
    }
    if (op == "||") {
        return 0;
    }
    return -1;  // 不明な演算子は常に括弧を付ける
}

bool sv_operator_is_associative(const std::string& op) {
    // 加算・乗算・ビット演算・論理演算は結合法則を満たす
    return op == "+" || op == "*" || op == "&" || op == "|" || op == "^" || op == "&&" ||
           op == "||";
}

SVExprPtr SVExpr::atom(std::string text) {
    auto e = SVExprPtr(new SVExpr());
    e->kind_ = Kind::Atom;
    e->text_ = std::move(text);
    return e;
}

SVExprPtr SVExpr::unary(std::string op, SVExprPtr operand) {
    auto e = SVExprPtr(new SVExpr());
    e->kind_ = Kind::Unary;
    e->op_ = std::move(op);
    e->operands_.push_back(std::move(operand));
    return e;
}

SVExprPtr SVExpr::binary(std::string op, SVExprPtr lhs, SVExprPtr rhs) {
    auto e = SVExprPtr(new SVExpr());
    e->kind_ = Kind::Binary;
    e->op_ = std::move(op);
    e->operands_.push_back(std::move(lhs));
    e->operands_.push_back(std::move(rhs));
    return e;
}

int SVExpr::precedence() const {
    switch (kind_) {
        case Kind::Atom:
            return 100;  // 原子は常に括弧不要
        case Kind::Unary:
            return 11;
        case Kind::Binary:
            return sv_operator_precedence(op_);
    }
    return -1;
}

std::string SVExpr::to_string() const {
    std::string out;
    print(out, /*parent_prec=*/-1, /*is_right_operand=*/false);
    return out;
}

void SVExpr::print(std::string& out, int parent_prec, bool is_right_operand) const {
    const int prec = precedence();

    // 括弧の要否判定:
    // - 自身の優先順位が親より弱ければ必須
    // - 同順位でも、右オペランド位置では結合法則を満たす演算子以外は必須（例: a - (b - c) / a / (b / c)）
    bool need_paren = false;
    if (kind_ != Kind::Atom && parent_prec >= 0) {
        if (prec < parent_prec) {
            need_paren = true;
        } else if (prec == parent_prec && is_right_operand &&
                   !(kind_ == Kind::Binary && sv_operator_is_associative(op_))) {
            need_paren = true;
        }
    }

    if (need_paren) {
        out += "(";
    }

    switch (kind_) {
        case Kind::Atom:
            out += text_;
            break;
        case Kind::Unary: {
            // 単項の -u/+u は表示上は -/+
            std::string display_op = op_;
            if (display_op == "-u" || display_op == "+u") {
                display_op = display_op.substr(0, 1);
            }
            out += display_op;
            operands_[0]->print(out, prec, /*is_right_operand=*/false);
            break;
        }
        case Kind::Binary:
            operands_[0]->print(out, prec, /*is_right_operand=*/false);
            out += " " + op_ + " ";
            operands_[1]->print(out, prec, /*is_right_operand=*/true);
            break;
    }

    if (need_paren) {
        out += ")";
    }
}

}  // namespace cm::codegen::sv
