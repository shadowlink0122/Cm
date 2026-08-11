// ============================================================
// TypeChecker 実装 - 属性の検証レジストリとデフォルト引数式のパラメータ参照診断
// ============================================================

#include "internal/base/i18n.hpp"
#include "internal/types/type_checker.hpp"

#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace cm {

// デフォルト引数式が同じ宣言のパラメータを参照していないか検査する。
// R8: 従来は前パラメータ名の参照が名前解決を通過し、呼び出し時のデフォルト補完が呼び出し側の文脈で式をloweringするため値だけが黙って0になっていた（f(int a, int b = a)でf(3)=30）。
// デフォルト引数はパラメータ束縛前に呼び出し側で評価される仕様のため、パラメータ参照は診断で拒否する（C++と同じ方針）
void TypeChecker::check_default_param_refs(const std::vector<ast::Param>& params,
                                           const Span& span) {
    std::unordered_set<std::string> param_names;
    for (const auto& p : params) {
        param_names.insert(p.name);
    }

    // デフォルト式に現れうる主要な式種を再帰走査して識別子を検査する（lambda.cppのキャプチャ検出と同型の局所ウォーカー）
    std::function<void(ast::Expr&, const std::string&)> walk = [&](ast::Expr& expr,
                                                                   const std::string& def_param) {
        if (auto* ident = expr.as<ast::IdentExpr>()) {
            if (param_names.count(ident->name) > 0) {
                error(span,
                      i18n::msgf(i18n::MsgId::TcDefaultArgReferencesParam, def_param, ident->name));
            }
        } else if (auto* binary = expr.as<ast::BinaryExpr>()) {
            walk(*binary->left, def_param);
            walk(*binary->right, def_param);
        } else if (auto* unary = expr.as<ast::UnaryExpr>()) {
            walk(*unary->operand, def_param);
        } else if (auto* call = expr.as<ast::CallExpr>()) {
            // 呼び出し先は関数名のため引数のみ検査する（同名パラメータでの関数呼び出しシャドウは別診断の領分）
            for (auto& arg : call->args) {
                walk(*arg, def_param);
            }
        } else if (auto* member = expr.as<ast::MemberExpr>()) {
            walk(*member->object, def_param);
            for (auto& arg : member->args) {
                walk(*arg, def_param);
            }
        } else if (auto* index = expr.as<ast::IndexExpr>()) {
            walk(*index->object, def_param);
            walk(*index->index, def_param);
        } else if (auto* ternary = expr.as<ast::TernaryExpr>()) {
            walk(*ternary->condition, def_param);
            walk(*ternary->then_expr, def_param);
            walk(*ternary->else_expr, def_param);
        } else if (auto* arr = expr.as<ast::ArrayLiteralExpr>()) {
            for (auto& elem : arr->elements) {
                walk(*elem, def_param);
            }
        } else if (auto* slit = expr.as<ast::StructLiteralExpr>()) {
            for (auto& field : slit->fields) {
                if (field.value) {
                    walk(*field.value, def_param);
                }
            }
        }
    };

    for (const auto& p : params) {
        if (p.default_value) {
            walk(*p.default_value, p.name);
        }
    }
}

// 属性の検証レジストリ（R7）。既知属性の単一ソース表と突き合わせ、(a)未知・タイポ属性は警告（--strictエラー）、
// (b)既知だが未実装の属性は未実装診断、(c)実装済み属性は解釈（#[deprecated]は関数名を収集し呼び出しサイトで警告）に3分類する。
// 従来は属性パーサが任意識別子を構文受理しフィルタリング側がtest/target以外を全て無視したため、#[tset]等のタイポでテストが黙って実行されずcm testが緑になっていた
void TypeChecker::check_attribute_list(const std::vector<ast::AttributeNode>& attrs,
                                       const Span& fallback_span) {
    // 実装済み（コンパイラのいずれかの段で解釈される）属性
    static const std::unordered_set<std::string> kImplemented = {
        "test",
        "target",
        "derive",
        "deprecated",
        "input",
        "output",
        "inout",
        "sv::param",
        "sv::parameter",
        "sv::module_name",
        "sv::pin",
        "sv::iostandard",
        "sv::bram",
        "sv::lutram",
        "sv::memfile",
        "sv::clock_domain",
        "sv::latch",
        "sv::instance_array",
        "sv::packed",
        "sv::packed_union",
        "sv::unpacked",
        "sv::pipeline",
        "sv::share",
        "sv::sync",
        "sv::tri",
        "verilog::param",
        "verilog::parameter",
        "verilog::bram",
        "verilog::lutram",
    };
    // 既知だが未実装（付けても効果がない）属性
    static const std::unordered_set<std::string> kUnimplemented = {"bench", "optimize", "inline",
                                                                   "cfg"};
    // #[target(...)]の条件に書ける名前（string_to_targetが認識する名前+native+active。tsはjs出力の別名だがtarget条件では未認識のため案内する）
    static const std::unordered_set<std::string> kTargetNames = {
        "native", "js", "web",           "wasm",          "sv",     "verilog", "systemverilog",
        "uefi",   "bm", "baremetal-arm", "baremetal-x86", "active",
    };

    for (const auto& attr : attrs) {
        // 内部マーカー（prelude注入等）は診断対象外
        if (attr.name.rfind("__", 0) == 0) {
            continue;
        }
        const Span span = (attr.span.start == 0 && attr.span.end == 0) ? fallback_span : attr.span;
        if (kUnimplemented.count(attr.name) > 0) {
            const std::string msg = i18n::msgf(i18n::MsgId::TcUnimplementedAttribute, attr.name);
            if (enable_naming_check_) {
                error(span, msg);
            } else {
                warning(span, msg);
            }
            continue;
        }
        if (kImplemented.count(attr.name) == 0) {
            const std::string msg = i18n::msgf(i18n::MsgId::TcUnknownAttribute, attr.name);
            if (enable_naming_check_) {
                error(span, msg);
            } else {
                warning(span, msg);
            }
            continue;
        }
        // #[sv::pin]は引数（ピン名文字列）必須（R16。非文字列リテラルはパーサ段で診断される）
        if ((attr.name == "sv::pin" || attr.name == "verilog::pin") && attr.args.empty()) {
            const std::string msg =
                i18n::msgf(i18n::MsgId::TcSvPinRequiresStringArgument, attr.name);
            if (enable_naming_check_) {
                error(span, msg);
            } else {
                warning(span, msg);
            }
        }
        // #[target(...)]の未知ターゲット名はNative縮退で意味が反転するため検証する
        if (attr.name == "target") {
            for (const auto& raw : attr.args) {
                std::string name = raw;
                if (!name.empty() && name[0] == '!') {
                    name = name.substr(1);
                }
                if (kTargetNames.count(name) == 0) {
                    const std::string msg = i18n::msgf(i18n::MsgId::TcUnknownTargetName, name);
                    if (enable_naming_check_) {
                        error(span, msg);
                    } else {
                        warning(span, msg);
                    }
                }
            }
        }
    }
}

void TypeChecker::check_attributes(const ast::Program& program) {
    // 宣言ツリーを辿って全属性を検証し、#[deprecated]関数を収集する（名前空間内は修飾名でも登録する）
    std::function<void(const std::vector<ast::DeclPtr>&, const std::string&)> walk =
        [&](const std::vector<ast::DeclPtr>& decls, const std::string& ns) {
            for (const auto& decl : decls) {
                if (!decl) {
                    continue;
                }
                if (const auto* func = decl->as<ast::FunctionDecl>()) {
                    check_attribute_list(func->attributes, func->name_span);
                    for (const auto& attr : func->attributes) {
                        if (attr.name == "deprecated") {
                            deprecated_functions_.insert(func->name);
                            if (!ns.empty()) {
                                deprecated_functions_.insert(ns + "::" + func->name);
                            }
                        }
                    }
                } else if (const auto* st = decl->as<ast::StructDecl>()) {
                    check_attribute_list(st->attributes, st->name_span);
                    for (const auto& field : st->fields) {
                        check_attribute_list(field.attributes, st->name_span);
                    }
                } else if (const auto* en = decl->as<ast::EnumDecl>()) {
                    check_attribute_list(en->attributes, decl->span);
                } else if (const auto* impl = decl->as<ast::ImplDecl>()) {
                    check_attribute_list(impl->attributes, decl->span);
                    for (const auto& method : impl->methods) {
                        if (method) {
                            check_attribute_list(method->attributes, method->name_span);
                        }
                    }
                } else if (const auto* gv = decl->as<ast::GlobalVarDecl>()) {
                    // SVポート宣言（#[output] int led等）の属性もここで検証する（R16: 従来は
                    // グローバル変数が走査外で#[sv::pinn]等のタイポが黙殺されピン制約が静かに欠落した）
                    check_attribute_list(gv->attributes, decl->span);
                } else if (const auto* ib = decl->as<ast::InitialBlockDecl>()) {
                    check_attribute_list(ib->attributes, decl->span);
                } else if (auto* mod = const_cast<ast::Decl&>(*decl).as<ast::ModuleDecl>()) {
                    std::string inner_ns = mod->path.to_string();
                    if (!ns.empty()) {
                        inner_ns = ns + "::" + inner_ns;
                    }
                    walk(mod->declarations, inner_ns);
                }
            }
        };
    walk(program.declarations, "");
}

}  // namespace cm
