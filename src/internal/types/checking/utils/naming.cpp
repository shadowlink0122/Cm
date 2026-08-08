// ============================================================
// TypeChecker 実装 - 命名規則チェック（L001 naming-convention。check/lint --strict時のみ）
// ============================================================

#include "internal/base/i18n.hpp"
#include "internal/types/naming_rules.hpp"
#include "internal/types/type_checker.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace cm {

// ============================================================
// 命名規則チェック (L001 naming-convention。check/lint --strict時のみ)
// 判定ロジックは frontend/types/naming_rules.hpp に分離
// ============================================================

bool TypeChecker::is_snake_case(const std::string& name) {
    return naming::is_snake_case(name);
}

bool TypeChecker::is_pascal_case(const std::string& name) {
    return naming::is_pascal_case(name);
}

bool TypeChecker::is_upper_snake_case(const std::string& name) {
    return naming::is_upper_snake_case(name);
}

namespace {

// 内部生成名（__prelude注入等）や無名はチェック対象外
bool naming_exempt(const std::string& name) {
    return name.empty() || name == "_" || name.rfind("__", 0) == 0;
}

// importでインライン展開されるライブラリ名前空間はユーザーの責任範囲外
bool is_library_namespace(const std::string& root) {
    return root == "std" || root == "native" || root == "js" || root == "uefi" || root == "web" ||
           root == "wasm";
}

bool has_attribute(const std::vector<ast::AttributeNode>& attrs, const std::string& name) {
    for (const auto& attr : attrs) {
        if (attr.name == name) {
            return true;
        }
    }
    return false;
}

}  // namespace

void TypeChecker::report_naming(Span span, i18n::MsgId decl_kind, const std::string& name,
                                i18n::MsgId expected) {
    warning(span, i18n::msgf(i18n::MsgId::LintNamingViolation, i18n::msg(decl_kind), name,
                             i18n::msg(expected)));
}

// 関数本体の文を再帰的に走査し、ローカル変数宣言の命名を検査する
void TypeChecker::check_naming_stmts(std::vector<ast::StmtPtr>& stmts) {
    for (auto& stmt : stmts) {
        if (!stmt) {
            continue;
        }
        if (auto* let = stmt->as<ast::LetStmt>()) {
            if (!naming_exempt(let->name)) {
                Span span = (let->name_span.start != 0 || let->name_span.end != 0) ? let->name_span
                                                                                   : stmt->span;
                if (let->is_const) {
                    // ローカルconstは snake_case / UPPER_SNAKE_CASE の両方を許容
                    if (!naming::is_snake_case(let->name) &&
                        !naming::is_upper_snake_case(let->name)) {
                        report_naming(span, i18n::MsgId::LintLabelConstantName, let->name,
                                      i18n::MsgId::LintCaseSnakeOrUpper);
                    }
                } else if (!naming::is_snake_case(let->name)) {
                    report_naming(span, i18n::MsgId::LintLabelVariableName, let->name,
                                  i18n::MsgId::LintCaseSnake);
                }
            }
        } else if (auto* if_stmt = stmt->as<ast::IfStmt>()) {
            check_naming_stmts(if_stmt->then_block);
            check_naming_stmts(if_stmt->else_block);
        } else if (auto* for_stmt = stmt->as<ast::ForStmt>()) {
            if (for_stmt->init) {
                if (auto* init_let = for_stmt->init->as<ast::LetStmt>()) {
                    if (!naming_exempt(init_let->name) && !naming::is_snake_case(init_let->name)) {
                        report_naming(for_stmt->init->span, i18n::MsgId::LintLabelVariableName,
                                      init_let->name, i18n::MsgId::LintCaseSnake);
                    }
                }
            }
            check_naming_stmts(for_stmt->body);
        } else if (auto* for_in = stmt->as<ast::ForInStmt>()) {
            if (!naming_exempt(for_in->var_name) && !naming::is_snake_case(for_in->var_name)) {
                report_naming(stmt->span, i18n::MsgId::LintLabelVariableName, for_in->var_name,
                              i18n::MsgId::LintCaseSnake);
            }
            check_naming_stmts(for_in->body);
        } else if (auto* while_stmt = stmt->as<ast::WhileStmt>()) {
            check_naming_stmts(while_stmt->body);
        } else if (auto* block = stmt->as<ast::BlockStmt>()) {
            check_naming_stmts(block->stmts);
        } else if (auto* switch_stmt = stmt->as<ast::SwitchStmt>()) {
            for (auto& c : switch_stmt->cases) {
                check_naming_stmts(c.stmts);
            }
        } else if (auto* defer_stmt = stmt->as<ast::DeferStmt>()) {
            if (defer_stmt->body) {
                if (auto* body_let = defer_stmt->body->as<ast::LetStmt>()) {
                    if (!naming_exempt(body_let->name) && !naming::is_snake_case(body_let->name)) {
                        report_naming(defer_stmt->body->span, i18n::MsgId::LintLabelVariableName,
                                      body_let->name, i18n::MsgId::LintCaseSnake);
                    }
                }
            }
        } else if (auto* must_block = stmt->as<ast::MustBlockStmt>()) {
            check_naming_stmts(must_block->body);
        }
    }
}

// 関数宣言（名前・パラメータ・型パラメータ・本体）の命名を検査する
void TypeChecker::check_naming_function(ast::FunctionDecl& func) {
    // extern "C" / コンストラクタ / デストラクタ / main は対象外
    if (func.is_extern || func.is_constructor || func.is_destructor) {
        return;
    }
    Span span = func.name_span;
    if (!naming_exempt(func.name) && func.name != "main" && !naming::is_snake_case(func.name)) {
        report_naming(span, i18n::MsgId::LintLabelFunctionName, func.name,
                      i18n::MsgId::LintCaseSnake);
    }
    for (const auto& param : func.params) {
        if (!naming_exempt(param.name) && param.name != "self" &&
            !naming::is_snake_case(param.name)) {
            report_naming(span, i18n::MsgId::LintLabelParameterName, param.name,
                          i18n::MsgId::LintCaseSnake);
        }
    }
    for (const auto& gp : func.generic_params) {
        if (!naming_exempt(gp) && !naming::is_pascal_case(gp)) {
            report_naming(span, i18n::MsgId::LintLabelTypeParameterName, gp,
                          i18n::MsgId::LintCasePascal);
        }
    }
    check_naming_stmts(func.body);
}

// 宣言単位の命名検査（トップレベル・module配下の両方から呼ばれる）
void TypeChecker::check_naming_decl(ast::Decl& decl, bool top_level) {
    if (auto* func = decl.as<ast::FunctionDecl>()) {
        check_naming_function(*func);
    } else if (auto* st = decl.as<ast::StructDecl>()) {
        // extern struct はベンダプリミティブ等の外部固定名のため対象外
        if (st->is_extern) {
            return;
        }
        Span span =
            (st->name_span.start != 0 || st->name_span.end != 0) ? st->name_span : decl.span;
        if (!naming_exempt(st->name) && !naming::is_pascal_case(st->name)) {
            report_naming(span, i18n::MsgId::LintLabelStructName, st->name,
                          i18n::MsgId::LintCasePascal);
        }
        for (const auto& field : st->fields) {
            // #[sv::param]/#[verilog::param] フィールドはSVパラメータの写しなので
            // 定数と同じくUPPER_SNAKE_CASEを許容する
            bool is_param_field = false;
            for (const auto& attr : field.attributes) {
                if (attr.name == "sv::param" || attr.name == "verilog::param") {
                    is_param_field = true;
                    break;
                }
            }
            if (is_param_field) {
                if (!naming_exempt(field.name) && !naming::is_snake_case(field.name) &&
                    !naming::is_upper_snake_case(field.name)) {
                    report_naming(span, i18n::MsgId::LintLabelFieldName, field.name,
                                  i18n::MsgId::LintCaseSnakeOrUpper);
                }
                continue;
            }
            if (!naming_exempt(field.name) && !naming::is_snake_case(field.name)) {
                report_naming(span, i18n::MsgId::LintLabelFieldName, field.name,
                              i18n::MsgId::LintCaseSnake);
            }
        }
        for (const auto& gp : st->generic_params) {
            if (!naming_exempt(gp) && !naming::is_pascal_case(gp)) {
                report_naming(span, i18n::MsgId::LintLabelTypeParameterName, gp,
                              i18n::MsgId::LintCasePascal);
            }
        }
    } else if (auto* en = decl.as<ast::EnumDecl>()) {
        // 組み込みprelude（Result/Option注入）は対象外
        if (has_attribute(en->attributes, "__prelude")) {
            return;
        }
        if (!naming_exempt(en->name) && !naming::is_pascal_case(en->name)) {
            report_naming(decl.span, i18n::MsgId::LintLabelEnumName, en->name,
                          i18n::MsgId::LintCasePascal);
        }
        for (const auto& member : en->members) {
            // バリアントは PascalCase / UPPER_SNAKE_CASE の両方を許容
            if (!naming_exempt(member.name) && !naming::is_pascal_case(member.name) &&
                !naming::is_upper_snake_case(member.name)) {
                report_naming(decl.span, i18n::MsgId::LintLabelEnumVariantName, member.name,
                              i18n::MsgId::LintCasePascalOrUpper);
            }
        }
    } else if (auto* iface = decl.as<ast::InterfaceDecl>()) {
        if (!naming_exempt(iface->name) && !naming::is_pascal_case(iface->name)) {
            report_naming(decl.span, i18n::MsgId::LintLabelInterfaceName, iface->name,
                          i18n::MsgId::LintCasePascal);
        }
        for (const auto& method : iface->methods) {
            if (!naming_exempt(method.name) && !naming::is_snake_case(method.name)) {
                report_naming(decl.span, i18n::MsgId::LintLabelMethodName, method.name,
                              i18n::MsgId::LintCaseSnake);
            }
        }
    } else if (auto* impl = decl.as<ast::ImplDecl>()) {
        for (auto& method : impl->methods) {
            if (method) {
                check_naming_function(*method);
            }
        }
    } else if (auto* td = decl.as<ast::TypedefDecl>()) {
        if (!naming_exempt(td->name) && !naming::is_pascal_case(td->name)) {
            report_naming(decl.span, i18n::MsgId::LintLabelTypeAliasName, td->name,
                          i18n::MsgId::LintCasePascal);
        }
    } else if (auto* gv = decl.as<ast::GlobalVarDecl>()) {
        if (naming_exempt(gv->name)) {
            return;
        }
        if (gv->is_const && top_level) {
            // グローバルconstは UPPER_SNAKE_CASE
            if (!naming::is_upper_snake_case(gv->name)) {
                report_naming(decl.span, i18n::MsgId::LintLabelGlobalConstantName, gv->name,
                              i18n::MsgId::LintCaseUpperSnake);
            }
        } else if (!gv->is_const && !naming::is_snake_case(gv->name)) {
            report_naming(decl.span, i18n::MsgId::LintLabelGlobalVariableName, gv->name,
                          i18n::MsgId::LintCaseSnake);
        }
    } else if (auto* mod = decl.as<ast::ModuleDecl>()) {
        if (mod->path.segments.empty()) {
            return;
        }
        // ライブラリ名前空間（importでインライン展開されたもの）は対象外
        if (is_library_namespace(mod->path.segments.front())) {
            return;
        }
        for (const auto& seg : mod->path.segments) {
            if (!naming_exempt(seg) && !naming::is_snake_case(seg)) {
                report_naming(decl.span, i18n::MsgId::LintLabelModuleName, seg,
                              i18n::MsgId::LintCaseSnake);
            }
        }
        for (auto& inner : mod->declarations) {
            if (inner) {
                check_naming_decl(*inner, top_level);
            }
        }
    }
    // Import/Export/Use/Macro/ExternBlock/InitialBlock は対象外
}

// 命名規則チェックのエントリポイント（check() の末尾から呼ばれる）
void TypeChecker::check_naming_conventions(ast::Program& program) {
    if (!enable_naming_check_) {
        return;
    }
    for (auto& decl : program.declarations) {
        if (decl) {
            check_naming_decl(*decl, /*top_level=*/true);
        }
    }
}

// メソッド表キーの正準計算: ジェネリック定義キー（G<T, U>形。implブロック登録のtype_to_string形と同形）。
// 従来はmethod.cpp・function.cpp・auto_impl.cppがバイト一致必須の文字列組み立てを別々に持っていた
std::string TypeChecker::generic_def_method_key(const std::string& base_name) const {
    auto it = generic_structs_.find(base_name);
    if (it == generic_structs_.end()) {
        return base_name;
    }
    std::string key = base_name + "<";
    for (size_t i = 0; i < it->second.size(); ++i) {
        if (i > 0) {
            key += ", ";
        }
        key += it->second[i];
    }
    key += ">";
    return key;
}

// 特殊化サフィックスの除去（Result<int, string>・Result__int__string → Result）
std::string TypeChecker::strip_spec_suffix(const std::string& name) {
    std::string base = name;
    auto lt = base.find('<');
    if (lt != std::string::npos) {
        base = base.substr(0, lt);
    }
    auto us = base.find("__");
    if (us != std::string::npos && us > 0) {
        base = base.substr(0, us);
    }
    return base;
}

}  // namespace cm
