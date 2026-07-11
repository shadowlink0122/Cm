#pragma once

// ============================================================
// 文字列補間lowering内部ヘルパー（expr_call / expr_interp で共有）
// ============================================================

#include "../../hir/lowering/fwd.hpp"
#include "expr.hpp"

#include <functional>
#include <string>

namespace cm::mir {

// 補間内容が「単純な変数参照・メンバ・添字・関数呼び出し」ではなく
// 演算子を含む一般式かどうかを判定する（トップレベルの演算子のみ検出）。
// 該当する場合は式パーサへのフォールバックを試みる
inline bool interp_content_is_expression(const std::string& s) {
    int paren = 0;
    int bracket = 0;
    bool in_string = false;
    char quote = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (in_string) {
            if (c == quote && (i == 0 || s[i - 1] != '\\')) {
                in_string = false;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            in_string = true;
            quote = c;
            continue;
        }
        if (c == '(') {
            paren++;
            continue;
        }
        if (c == ')') {
            paren--;
            continue;
        }
        if (c == '[') {
            bracket++;
            continue;
        }
        if (c == ']') {
            bracket--;
            continue;
        }
        if (paren != 0 || bracket != 0) {
            // 括弧内の ':'（ビットスライス x[3:0] / パートセレクト +: ）は
            // 本物の式パーサで処理すべき式マーカー
            if (c == ':' && bracket != 0) {
                return true;
            }
            continue;
        }
        // 括弧外の as キャスト（例: (*pc) as int）は式として扱う
        if (c == ' ' && s.compare(i, 4, " as ") == 0) {
            return true;
        }
        switch (c) {
            case '+':
            case '/':
            case '%':
            case '^':
            case '~':
            case '?':
            case '<':
            case '>':
            case '=':
                return true;
            case '-':
                // "->" はポインタメンバアクセスなので除外
                if (i + 1 < s.size() && s[i + 1] == '>') {
                    ++i;
                    break;
                }
                return true;
            case '*':
                // 先頭の * はデリファレンス（既存パターンで処理）
                if (i > 0) {
                    return true;
                }
                break;
            case '&':
                // 先頭の & はアドレス取得（既存パターンで処理）
                if (i > 0) {
                    return true;
                }
                break;
            case '!':
                // 先頭の ! は論理否定（既存パターンで処理）
                if (i > 0) {
                    return true;
                }
                break;
            case '|':
                return true;
            default:
                break;
        }
    }
    return false;
}

// 補間内の関数呼び出し引数文字列をMIRオペランドへ変換するヘルパー
// 整数リテラル・boolリテラル・ローカル変数名をサポートする
// （それ以外の複雑な式は従来どおりダミーの0を返す）
// ジェネリック構造体の特殊化名（Box<int> → Box__int）を構成する。
// 補間内メソッド呼び出しの関数名解決で、型引数を落とすと
// 未定義シンボル参照（Box__get）になるため必ず型引数を反映する
inline std::string interp_specialized_struct_name(const hir::TypePtr& t) {
    if (!t) {
        return "";
    }
    std::function<std::string(const hir::TypePtr&)> piece =
        [&](const hir::TypePtr& p) -> std::string {
        if (!p) {
            return "unknown";
        }
        switch (p->kind) {
            case hir::TypeKind::Int:
                return "int";
            case hir::TypeKind::UInt:
                return "uint";
            case hir::TypeKind::Long:
                return "long";
            case hir::TypeKind::ULong:
                return "ulong";
            case hir::TypeKind::Short:
                return "short";
            case hir::TypeKind::UShort:
                return "ushort";
            case hir::TypeKind::Tiny:
                return "tiny";
            case hir::TypeKind::UTiny:
                return "utiny";
            case hir::TypeKind::Float:
                return "float";
            case hir::TypeKind::Double:
                return "double";
            case hir::TypeKind::Bool:
                return "bool";
            case hir::TypeKind::Char:
                return "char";
            case hir::TypeKind::String:
                return "string";
            case hir::TypeKind::Pointer:
                return "ptr_" + piece(p->element_type);
            default:
                return p->name.empty() ? "unknown" : p->name;
        }
    };
    std::string name = t->name;
    for (const auto& ta : t->type_args) {
        name += "__" + piece(ta);
    }
    return name;
}

inline MirOperandPtr lower_interp_call_arg(LoweringContext& ctx, const std::string& raw_arg) {
    // 前後の空白を除去
    std::string arg = raw_arg;
    size_t first = arg.find_first_not_of(" \t");
    if (first == std::string::npos) {
        arg.clear();
    } else {
        size_t last = arg.find_last_not_of(" \t");
        arg = arg.substr(first, last - first + 1);
    }

    // 整数リテラル（16進等はstoullのbase=0で解釈、負数はstollで解釈）
    if (!arg.empty() &&
        (std::isdigit(static_cast<unsigned char>(arg[0])) ||
         (arg[0] == '-' && arg.size() > 1 && std::isdigit(static_cast<unsigned char>(arg[1]))))) {
        try {
            int64_t value;
            // 0b/0B 2進リテラルはstoullのbase=0が解釈しないため手動対応
            if (arg.size() > 2 && arg[0] == '0' && (arg[1] == 'b' || arg[1] == 'B')) {
                value = static_cast<int64_t>(std::stoull(arg.substr(2), nullptr, 2));
            } else if (arg[0] == '-') {
                value = std::stoll(arg, nullptr, 0);
            } else {
                value = static_cast<int64_t>(std::stoull(arg, nullptr, 0));
            }
            MirConstant arg_const;
            arg_const.type = hir::make_int();
            arg_const.value = value;
            return MirOperand::constant(arg_const);
        } catch (...) {
            // 数値として解釈できない場合は下の変数解決へフォールスルー
        }
    }

    // boolリテラル
    if (arg == "true" || arg == "false") {
        MirConstant arg_const;
        arg_const.type = hir::make_bool();
        arg_const.value = (arg == "true");
        return MirOperand::constant(arg_const);
    }

    // ローカル変数（従来は整数リテラル以外がすべてダミー0になっていた）
    if (auto var_id = ctx.resolve_variable(arg)) {
        hir::TypePtr var_type = nullptr;
        if (*var_id < ctx.func->locals.size()) {
            var_type = ctx.func->locals[*var_id].type;
        }
        return MirOperand::copy(MirPlace{*var_id}, var_type);
    }

    // 未対応の式はダミー値（従来挙動を維持）
    MirConstant arg_const;
    arg_const.type = hir::make_int();
    arg_const.value = 0;
    return MirOperand::constant(arg_const);
}

}  // namespace cm::mir
