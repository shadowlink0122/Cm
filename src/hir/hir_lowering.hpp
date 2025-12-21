#pragma once

#include "../common/debug/hir.hpp"
#include "../frontend/ast/decl.hpp"
#include "hir_nodes.hpp"

#include <set>
#include <unordered_set>

namespace cm::hir {

// ============================================================
// AST → HIR 変換
// ============================================================
class HirLowering {
   public:
    HirProgram lower(ast::Program& program) {
        debug::hir::log(debug::hir::Id::LowerStart);

        HirProgram hir;
        hir.filename = program.filename;

        // Pass 1: 構造体定義、enum定義、関数定義、コンストラクタ情報を収集
        for (auto& decl : program.declarations) {
            if (auto* st = decl->as<ast::StructDecl>()) {
                struct_defs_[st->name] = st;
            } else if (auto* func = decl->as<ast::FunctionDecl>()) {
                // 関数定義を収集（デフォルト引数を持つ場合に必要）
                func_defs_[func->name] = func;
            } else if (auto* en = decl->as<ast::EnumDecl>()) {
                // enum定義を収集（メンバ名と値のマッピング）
                for (const auto& member : en->members) {
                    if (member.value.has_value()) {
                        enum_values_[en->name + "::" + member.name] = *member.value;
                    }
                }
            } else if (auto* impl = decl->as<ast::ImplDecl>()) {
                // コンストラクタ情報を収集
                if (impl->is_ctor_impl && impl->target_type) {
                    std::string type_name = type_to_string(*impl->target_type);
                    for (auto& ctor : impl->constructors) {
                        // デフォルトコンストラクタ（引数なし）の存在を記録
                        if (ctor->params.empty()) {
                            types_with_default_ctor_.insert(type_name);
                        }
                    }
                }
            }
        }

        // Pass 2: 宣言を変換
        for (auto& decl : program.declarations) {
            // ModuleDecl/Namespace は特別に処理
            if (auto* mod = decl->as<ast::ModuleDecl>()) {
                process_namespace(*mod, "", hir);
            } else if (auto hir_decl = lower_decl(*decl)) {
                hir.declarations.push_back(std::move(hir_decl));
            }
        }

        debug::hir::log(debug::hir::Id::LowerEnd,
                        std::to_string(hir.declarations.size()) + " declarations");
        return hir;
    }

    // ネストしたnamespaceを再帰的に処理
    void process_namespace(ast::ModuleDecl& mod, const std::string& parent_namespace,
                           HirProgram& hir) {
        std::string namespace_name = mod.path.segments.empty() ? "" : mod.path.segments[0];
        std::string full_namespace =
            parent_namespace.empty() ? namespace_name : parent_namespace + "::" + namespace_name;

        debug::hir::log(debug::hir::Id::NodeCreate, "processing namespace " + full_namespace,
                        debug::Level::Debug);

        // namespace内の各宣言を処理
        for (auto& inner_decl : mod.declarations) {
            // ネストしたnamespaceの処理
            if (auto* nested_mod = inner_decl->as<ast::ModuleDecl>()) {
                process_namespace(*nested_mod, full_namespace, hir);
            } else if (auto* func = inner_decl->as<ast::FunctionDecl>()) {
                // 関数名に完全なnamespaceプレフィックスを追加
                std::string original_name = func->name;
                func->name = full_namespace + "::" + original_name;
                if (auto hir_decl = lower_function(*func)) {
                    hir.declarations.push_back(std::move(hir_decl));
                }
                func->name = original_name;
            } else if (auto* st = inner_decl->as<ast::StructDecl>()) {
                std::string original_name = st->name;
                st->name = full_namespace + "::" + original_name;
                if (auto hir_decl = lower_struct(*st)) {
                    hir.declarations.push_back(std::move(hir_decl));
                }
                st->name = original_name;
            } else {
                if (auto hir_decl = lower_decl(*inner_decl)) {
                    hir.declarations.push_back(std::move(hir_decl));
                }
            }
        }
    }

   private:
    // 構造体定義のキャッシュ
    std::unordered_map<std::string, const ast::StructDecl*> struct_defs_;

    // 関数定義のキャッシュ（デフォルト引数を取得するため）
    std::unordered_map<std::string, const ast::FunctionDecl*> func_defs_;

    // enum値のキャッシュ (EnumName::MemberName -> value)
    std::unordered_map<std::string, int64_t> enum_values_;

    // デフォルトコンストラクタを持つ型の集合
    std::unordered_set<std::string> types_with_default_ctor_;

    // 構造体のdefaultメンバ名を取得（なければ空文字列）
    std::string get_default_member_name(const std::string& struct_name) const {
        auto it = struct_defs_.find(struct_name);
        if (it == struct_defs_.end())
            return "";
        for (const auto& field : it->second->fields) {
            if (field.is_default) {
                return field.name;
            }
        }
        return "";
    }

    // 宣言の変換
    HirDeclPtr lower_decl(ast::Decl& decl) {
        if (auto* func = decl.as<ast::FunctionDecl>()) {
            return lower_function(*func);
        } else if (auto* st = decl.as<ast::StructDecl>()) {
            return lower_struct(*st);
        } else if (auto* iface = decl.as<ast::InterfaceDecl>()) {
            return lower_interface(*iface);
        } else if (auto* impl = decl.as<ast::ImplDecl>()) {
            return lower_impl(*impl);
        } else if (auto* imp = decl.as<ast::ImportDecl>()) {
            return lower_import(*imp);
        } else if (auto* en = decl.as<ast::EnumDecl>()) {
            return lower_enum(*en);
        } else if (auto* td = decl.as<ast::TypedefDecl>()) {
            return lower_typedef(*td);
        } else if (auto* gv = decl.as<ast::GlobalVarDecl>()) {
            return lower_global_var(*gv);
        } else if (auto* mod = decl.as<ast::ModuleDecl>()) {
            return lower_module(*mod);
        } else if (auto* extern_block = decl.as<ast::ExternBlockDecl>()) {
            return lower_extern_block(*extern_block);
        }
        return nullptr;
    }

    // extern "C" ブロック
    HirDeclPtr lower_extern_block(ast::ExternBlockDecl& extern_block) {
        // extern "C"ブロック内の関数を個別に処理
        // HIRレベルではexternブロックを展開して個別の関数宣言として扱う
        auto hir_extern = std::make_unique<HirExternBlock>();
        hir_extern->language = extern_block.language;
        for (const auto& func : extern_block.declarations) {
            auto hir_func = std::make_unique<HirFunction>();
            hir_func->name = func->name;
            hir_func->return_type = func->return_type;
            hir_func->is_extern = true;
            for (const auto& p : func->params) {
                hir_func->params.push_back({p.name, p.type});
            }
            hir_extern->functions.push_back(std::move(hir_func));
        }
        return std::make_unique<HirDecl>(std::move(hir_extern));
    }

    // 関数
    HirDeclPtr lower_function(ast::FunctionDecl& func) {
        debug::hir::log(debug::hir::Id::FunctionNode, "function " + func.name, debug::Level::Debug);
        debug::hir::log(debug::hir::Id::FunctionName, func.name, debug::Level::Trace);

        auto hir_func = std::make_unique<HirFunction>();
        hir_func->name = func.name;
        hir_func->return_type = func.return_type;
        hir_func->is_export = func.visibility == ast::Visibility::Export;

        // ジェネリックパラメータを処理
        for (const auto& param_name : func.generic_params) {
            HirGenericParam param;
            param.name = param_name;
            // TODO: 型制約の解析（T: Ord のような構文）
            hir_func->generic_params.push_back(param);
        }

        debug::hir::log(debug::hir::Id::FunctionReturn,
                        func.return_type ? type_to_string(*func.return_type) : "void",
                        debug::Level::Trace);

        debug::hir::log(debug::hir::Id::FunctionParams,
                        "count=" + std::to_string(func.params.size()), debug::Level::Trace);
        for (const auto& param : func.params) {
            hir_func->params.push_back({param.name, param.type});
            debug::hir::dump_symbol(param.name, func.name,
                                    param.type ? type_to_string(*param.type) : "auto");
        }

        debug::hir::log(debug::hir::Id::FunctionBody,
                        "statements=" + std::to_string(func.body.size()), debug::Level::Trace);
        for (auto& stmt : func.body) {
            if (auto hir_stmt = lower_stmt(*stmt)) {
                hir_func->body.push_back(std::move(hir_stmt));
            }
        }

        return std::make_unique<HirDecl>(std::move(hir_func));
    }

    // 構造体
    HirDeclPtr lower_struct(ast::StructDecl& st) {
        debug::hir::log(debug::hir::Id::StructNode, "struct " + st.name, debug::Level::Debug);

        auto hir_st = std::make_unique<HirStruct>();
        hir_st->name = st.name;
        hir_st->is_export = st.visibility == ast::Visibility::Export;
        hir_st->auto_impls = st.auto_impls;  // with で指定されたinterface

        // ジェネリックパラメータを処理
        for (const auto& param_name : st.generic_params) {
            HirGenericParam param;
            param.name = param_name;
            // TODO: 型制約の解析（T: Ord のような構文）
            // 現在のASTでは制約情報を持っていないので、空のまま
            hir_st->generic_params.push_back(param);
        }

        for (const auto& field : st.fields) {
            hir_st->fields.push_back({field.name, field.type});
            debug::hir::log(
                debug::hir::Id::StructField,
                field.name + " : " + (field.type ? type_to_string(*field.type) : "auto"),
                debug::Level::Trace);
        }

        return std::make_unique<HirDecl>(std::move(hir_st));
    }

    // ASTのOperatorKindをHIRに変換
    HirOperatorKind convert_operator_kind(ast::OperatorKind kind) {
        switch (kind) {
            case ast::OperatorKind::Eq:
                return HirOperatorKind::Eq;
            case ast::OperatorKind::Ne:
                return HirOperatorKind::Ne;
            case ast::OperatorKind::Lt:
                return HirOperatorKind::Lt;
            case ast::OperatorKind::Gt:
                return HirOperatorKind::Gt;
            case ast::OperatorKind::Le:
                return HirOperatorKind::Le;
            case ast::OperatorKind::Ge:
                return HirOperatorKind::Ge;
            case ast::OperatorKind::Add:
                return HirOperatorKind::Add;
            case ast::OperatorKind::Sub:
                return HirOperatorKind::Sub;
            case ast::OperatorKind::Mul:
                return HirOperatorKind::Mul;
            case ast::OperatorKind::Div:
                return HirOperatorKind::Div;
            case ast::OperatorKind::Mod:
                return HirOperatorKind::Mod;
            case ast::OperatorKind::BitAnd:
                return HirOperatorKind::BitAnd;
            case ast::OperatorKind::BitOr:
                return HirOperatorKind::BitOr;
            case ast::OperatorKind::BitXor:
                return HirOperatorKind::BitXor;
            case ast::OperatorKind::Shl:
                return HirOperatorKind::Shl;
            case ast::OperatorKind::Shr:
                return HirOperatorKind::Shr;
            case ast::OperatorKind::Neg:
                return HirOperatorKind::Neg;
            case ast::OperatorKind::Not:
                return HirOperatorKind::Not;
            case ast::OperatorKind::BitNot:
                return HirOperatorKind::BitNot;
            default:
                return HirOperatorKind::Eq;  // fallback
        }
    }

    // インターフェース
    HirDeclPtr lower_interface(ast::InterfaceDecl& iface) {
        debug::hir::log(debug::hir::Id::NodeCreate, "interface " + iface.name, debug::Level::Trace);

        auto hir_iface = std::make_unique<HirInterface>();
        hir_iface->name = iface.name;
        hir_iface->is_export = iface.visibility == ast::Visibility::Export;

        // ジェネリックパラメータを処理
        for (const auto& param_name : iface.generic_params) {
            HirGenericParam param;
            param.name = param_name;
            hir_iface->generic_params.push_back(param);
        }

        // 通常のメソッドシグネチャ
        for (const auto& method : iface.methods) {
            HirMethodSig sig;
            sig.name = method.name;
            sig.return_type = method.return_type;
            for (const auto& param : method.params) {
                sig.params.push_back({param.name, param.type});
            }
            hir_iface->methods.push_back(std::move(sig));
        }

        // 演算子シグネチャ
        for (const auto& op : iface.operators) {
            HirOperatorSig sig;
            sig.op = convert_operator_kind(op.op);
            sig.return_type = op.return_type;
            for (const auto& param : op.params) {
                sig.params.push_back({param.name, param.type});
            }
            hir_iface->operators.push_back(std::move(sig));
        }

        return std::make_unique<HirDecl>(std::move(hir_iface));
    }

    // impl
    HirDeclPtr lower_impl(ast::ImplDecl& impl) {
        debug::hir::log(debug::hir::Id::NodeCreate, "impl " + impl.interface_name,
                        debug::Level::Trace);

        auto hir_impl = std::make_unique<HirImpl>();
        hir_impl->interface_name = impl.interface_name;
        hir_impl->target_type = impl.target_type ? type_to_string(*impl.target_type) : "";
        hir_impl->is_ctor_impl = impl.is_ctor_impl;

        // ジェネリックパラメータを処理
        for (const auto& param_name : impl.generic_params) {
            HirGenericParam param;
            param.name = param_name;
            hir_impl->generic_params.push_back(param);
        }

        // where句を処理
        for (const auto& clause : impl.where_clauses) {
            HirWhereClause hir_clause;
            hir_clause.type_param = clause.type_param;
            hir_clause.constraint_type = clause.constraint_type;
            hir_impl->where_clauses.push_back(hir_clause);
        }

        // コンストラクタ専用implの場合
        if (impl.is_ctor_impl) {
            // コンストラクタを関数としてlower
            for (auto& ctor : impl.constructors) {
                auto hir_func = std::make_unique<HirFunction>();
                std::string mangled_name = hir_impl->target_type + "__ctor";
                // 引数がある場合は常に引数数をサフィックスとして追加
                if (!ctor->params.empty()) {
                    mangled_name += "_" + std::to_string(ctor->params.size());
                }
                hir_func->name = mangled_name;
                hir_func->return_type = ast::make_void();
                hir_func->is_constructor = true;

                // selfパラメータを追加（ポインタ）
                hir_func->params.push_back({"self", impl.target_type});
                for (const auto& param : ctor->params) {
                    hir_func->params.push_back({param.name, param.type});
                }

                for (auto& stmt : ctor->body) {
                    if (auto hir_stmt = lower_stmt(*stmt)) {
                        hir_func->body.push_back(std::move(hir_stmt));
                    }
                }

                hir_impl->methods.push_back(std::move(hir_func));
            }

            // デストラクタをlower
            if (impl.destructor) {
                auto hir_func = std::make_unique<HirFunction>();
                hir_func->name = hir_impl->target_type + "__dtor";
                hir_func->return_type = ast::make_void();
                hir_func->is_destructor = true;

                // selfパラメータを追加（ポインタ）
                hir_func->params.push_back({"self", impl.target_type});

                for (auto& stmt : impl.destructor->body) {
                    if (auto hir_stmt = lower_stmt(*stmt)) {
                        hir_func->body.push_back(std::move(hir_stmt));
                    }
                }

                hir_impl->methods.push_back(std::move(hir_func));
            }

            return std::make_unique<HirDecl>(std::move(hir_impl));
        }

        // メソッド実装の場合
        for (auto& method : impl.methods) {
            auto hir_func = std::make_unique<HirFunction>();
            hir_func->name = method->name;
            hir_func->return_type = method->return_type;

            // implのジェネリックパラメータをメソッドに伝播
            for (const auto& gp : hir_impl->generic_params) {
                hir_func->generic_params.push_back(gp);
            }

            // selfパラメータを最初に追加（構造体型への参照）
            if (impl.target_type) {
                hir_func->params.push_back({"self", impl.target_type});
            }

            for (const auto& param : method->params) {
                hir_func->params.push_back({param.name, param.type});
            }

            for (auto& stmt : method->body) {
                if (auto hir_stmt = lower_stmt(*stmt)) {
                    hir_func->body.push_back(std::move(hir_stmt));
                }
            }

            hir_impl->methods.push_back(std::move(hir_func));
        }

        // 演算子実装
        for (auto& op : impl.operators) {
            auto hir_op = std::make_unique<HirOperatorImpl>();
            hir_op->op = convert_operator_kind(op->op);
            hir_op->return_type = op->return_type;

            for (const auto& param : op->params) {
                hir_op->params.push_back({param.name, param.type});
            }

            for (auto& stmt : op->body) {
                if (auto hir_stmt = lower_stmt(*stmt)) {
                    hir_op->body.push_back(std::move(hir_stmt));
                }
            }

            hir_impl->operators.push_back(std::move(hir_op));
        }

        return std::make_unique<HirDecl>(std::move(hir_impl));
    }

    // インポート
    HirDeclPtr lower_import(ast::ImportDecl& imp) {
        debug::hir::log(debug::hir::Id::NodeCreate, "import " + imp.path.to_string(),
                        debug::Level::Trace);

        auto hir_imp = std::make_unique<HirImport>();
        // ModulePath を vector<string> に変換
        hir_imp->path = imp.path.segments;
        // TODO: 選択的インポートのサポート
        hir_imp->alias = "";  // 一時的に空文字列

        return std::make_unique<HirDecl>(std::move(hir_imp));
    }

    // Enum
    HirDeclPtr lower_enum(ast::EnumDecl& en) {
        debug::hir::log(debug::hir::Id::NodeCreate, "enum " + en.name, debug::Level::Debug);

        auto hir_enum = std::make_unique<HirEnum>();
        hir_enum->name = en.name;
        hir_enum->is_export = en.visibility == ast::Visibility::Export;

        for (const auto& member : en.members) {
            HirEnumMember hir_member;
            hir_member.name = member.name;
            hir_member.value = member.value.value_or(0);  // ASTで既に値は計算済み
            hir_enum->members.push_back(std::move(hir_member));
        }

        return std::make_unique<HirDecl>(std::move(hir_enum));
    }

    // Typedef
    HirDeclPtr lower_typedef(ast::TypedefDecl& td) {
        debug::hir::log(debug::hir::Id::NodeCreate, "typedef " + td.name, debug::Level::Debug);

        auto hir_typedef = std::make_unique<HirTypedef>();
        hir_typedef->name = td.name;
        hir_typedef->type = td.type;

        return std::make_unique<HirDecl>(std::move(hir_typedef));
    }

    // グローバル変数/定数
    HirDeclPtr lower_global_var(ast::GlobalVarDecl& gv) {
        debug::hir::log(debug::hir::Id::NodeCreate,
                        std::string(gv.is_const ? "const " : "var ") + gv.name,
                        debug::Level::Debug);

        auto hir_global = std::make_unique<HirGlobalVar>();
        hir_global->name = gv.name;
        hir_global->type = gv.type;
        hir_global->is_const = gv.is_const;
        hir_global->is_export = (gv.visibility == ast::Visibility::Export);

        // 初期化式を変換
        if (gv.init_expr) {
            hir_global->init = lower_expr(*gv.init_expr);
        }

        return std::make_unique<HirDecl>(std::move(hir_global));
    }

    // Module/Namespace (namespace内の宣言を名前空間プレフィックス付きでフラット化)
    HirDeclPtr lower_module(ast::ModuleDecl& mod) {
        // namespace名を取得（最初のセグメントのみ使用）
        std::string namespace_name = mod.path.segments.empty() ? "" : mod.path.segments[0];

        debug::hir::log(debug::hir::Id::NodeCreate, "namespace " + namespace_name,
                        debug::Level::Debug);

        // namespace内の各宣言を処理
        // 暫定実装: 宣言をフラット化せず、nullptrを返す
        // 実際の処理はlower()関数で行う
        return nullptr;
    }

    // 文の変換
    HirStmtPtr lower_stmt(ast::Stmt& stmt) {
        if (auto* let = stmt.as<ast::LetStmt>()) {
            return lower_let(*let);
        } else if (auto* ret = stmt.as<ast::ReturnStmt>()) {
            return lower_return(*ret);
        } else if (auto* if_stmt = stmt.as<ast::IfStmt>()) {
            return lower_if(*if_stmt);
        } else if (auto* while_stmt = stmt.as<ast::WhileStmt>()) {
            return lower_while(*while_stmt);
        } else if (auto* for_stmt = stmt.as<ast::ForStmt>()) {
            return lower_for(*for_stmt);
        } else if (auto* for_in = stmt.as<ast::ForInStmt>()) {
            return lower_for_in(*for_in);
        } else if (auto* switch_stmt = stmt.as<ast::SwitchStmt>()) {
            return lower_switch(*switch_stmt);
        } else if (auto* expr_stmt = stmt.as<ast::ExprStmt>()) {
            return lower_expr_stmt(*expr_stmt);
        } else if (auto* block_stmt = stmt.as<ast::BlockStmt>()) {
            return lower_block(*block_stmt);
        } else if (stmt.as<ast::BreakStmt>()) {
            return std::make_unique<HirStmt>(std::make_unique<HirBreak>());
        } else if (stmt.as<ast::ContinueStmt>()) {
            return std::make_unique<HirStmt>(std::make_unique<HirContinue>());
        } else if (auto* defer_stmt = stmt.as<ast::DeferStmt>()) {
            return lower_defer(*defer_stmt);
        }
        return nullptr;
    }

    // defer文
    HirStmtPtr lower_defer(ast::DeferStmt& defer) {
        auto hir_defer = std::make_unique<HirDefer>();
        if (defer.body) {
            hir_defer->body = lower_stmt(*defer.body);
        }
        return std::make_unique<HirStmt>(std::move(hir_defer));
    }

    // ブロック文
    HirStmtPtr lower_block(ast::BlockStmt& block) {
        auto hir_block = std::make_unique<HirBlock>();
        for (auto& s : block.stmts) {
            if (auto hs = lower_stmt(*s)) {
                hir_block->stmts.push_back(std::move(hs));
            }
        }
        return std::make_unique<HirStmt>(std::move(hir_block));
    }

    // let文
    HirStmtPtr lower_let(ast::LetStmt& let) {
        debug::hir::log(debug::hir::Id::LetLower, "let " + let.name, debug::Level::Debug);
        debug::hir::log(debug::hir::Id::LetName, let.name, debug::Level::Trace);

        if (let.is_const) {
            debug::hir::log(debug::hir::Id::LetConst, "const variable: " + let.name,
                            debug::Level::Trace);
        }
        if (let.is_static) {
            debug::hir::log(debug::hir::Id::LetLower, "static variable: " + let.name,
                            debug::Level::Debug);
        }

        auto hir_let = std::make_unique<HirLet>();
        hir_let->name = let.name;
        hir_let->type = let.type;
        hir_let->is_const = let.is_const;
        hir_let->is_static = let.is_static;

        if (let.type) {
            debug::hir::log(debug::hir::Id::LetType, type_to_string(*let.type),
                            debug::Level::Trace);
        }

        if (let.init) {
            debug::hir::log(debug::hir::Id::LetInit, "initializer present", debug::Level::Trace);

            // 暗黙的構造体リテラルに型を伝播
            if (let.type && let.type->kind == ast::TypeKind::Struct) {
                if (auto* struct_lit = let.init->as<ast::StructLiteralExpr>()) {
                    if (struct_lit->type_name.empty()) {
                        struct_lit->type_name = let.type->name;
                        debug::hir::log(
                            debug::hir::Id::LetInit,
                            "Propagated type to implicit struct literal: " + let.type->name,
                            debug::Level::Debug);
                    }
                }
            }
            // 配列リテラルへの型伝播
            if (let.type && let.type->kind == ast::TypeKind::Array && let.type->element_type) {
                if (auto* array_lit = let.init->as<ast::ArrayLiteralExpr>()) {
                    // 配列要素が構造体の場合、各要素に型を伝播
                    if (let.type->element_type->kind == ast::TypeKind::Struct) {
                        for (auto& elem : array_lit->elements) {
                            if (auto* struct_lit = elem->as<ast::StructLiteralExpr>()) {
                                if (struct_lit->type_name.empty()) {
                                    struct_lit->type_name = let.type->element_type->name;
                                }
                            }
                        }
                    }
                }
            }

            // initが構造体のコンストラクタ呼び出し（Type name = Type(args)形式）かチェック
            bool is_constructor_init = false;
            if (auto* call = let.init->as<ast::CallExpr>()) {
                if (auto* ident = call->callee->as<ast::IdentExpr>()) {
                    // 呼び出し先が変数の型名と同じ場合、コンストラクタ呼び出しとみなす
                    if (let.type && ident->name == let.type->name) {
                        is_constructor_init = true;
                        // コンストラクタ引数として扱う
                        std::vector<ast::ExprPtr> ctor_args_temp;
                        for (auto& arg : call->args) {
                            ctor_args_temp.push_back(std::move(arg));
                        }
                        // ctor_argsとして設定（あとでコンストラクタ呼び出しとして処理される）
                        let.ctor_args = std::move(ctor_args_temp);
                        let.has_ctor_call = true;
                        debug::hir::log(debug::hir::Id::LetInit,
                                        "Detected constructor init: " + ident->name + " with " +
                                            std::to_string(let.ctor_args.size()) + " args",
                                        debug::Level::Debug);
                    }
                }
            }

            if (!is_constructor_init) {
                // defaultメンバからの暗黙的な取得をチェック
                // 左辺がプリミティブで右辺が構造体の場合、defaultメンバを取得
                auto init_type = let.init->type;
                if (let.type && let.type->kind != ast::TypeKind::Struct && init_type &&
                    init_type->kind == ast::TypeKind::Struct) {
                    std::string default_member = get_default_member_name(init_type->name);
                    if (!default_member.empty()) {
                        debug::hir::log(debug::hir::Id::LetInit,
                                        "Converting to default member access: " + default_member,
                                        debug::Level::Debug);
                        // int x = w → int x = w.value に変換
                        auto member = std::make_unique<HirMember>();
                        member->object = lower_expr(*let.init);
                        member->member = default_member;
                        hir_let->init = std::make_unique<HirExpr>(std::move(member), let.type);
                    } else {
                        hir_let->init = lower_expr(*let.init);
                    }
                } else {
                    hir_let->init = lower_expr(*let.init);
                }
            }
        }

        // コンストラクタ呼び出しがある場合、または構造体型でデフォルトコンストラクタを持つ場合
        bool should_call_ctor = let.has_ctor_call;

        // 明示的なコンストラクタ呼び出しがなく、初期化子もない場合
        // デフォルトコンストラクタを持つ構造体型なら暗黙的に呼び出す
        if (!should_call_ctor && !let.init && let.type) {
            std::string type_name = type_to_string(*let.type);
            if (types_with_default_ctor_.count(type_name)) {
                should_call_ctor = true;
                debug::hir::log(debug::hir::Id::LetInit,
                                "Implicit default constructor call for: " + type_name,
                                debug::Level::Debug);
            }
        }

        if (should_call_ctor && let.type) {
            std::string type_name = type_to_string(*let.type);
            std::string ctor_name = type_name + "__ctor";
            if (!let.ctor_args.empty()) {
                ctor_name += "_" + std::to_string(let.ctor_args.size());
            }

            debug::hir::log(debug::hir::Id::LetInit, "Adding constructor call: " + ctor_name,
                            debug::Level::Debug);

            // コンストラクタ呼び出しを生成
            auto ctor_call = std::make_unique<HirCall>();
            ctor_call->func_name = ctor_name;

            // thisポインタ（変数への参照）を第一引数として追加
            auto this_ref = std::make_unique<HirVarRef>();
            this_ref->name = let.name;
            ctor_call->args.push_back(std::make_unique<HirExpr>(std::move(this_ref), let.type));

            // コンストラクタ引数を追加
            for (auto& arg : let.ctor_args) {
                ctor_call->args.push_back(lower_expr(*arg));
            }

            hir_let->ctor_call = std::make_unique<HirExpr>(std::move(ctor_call), ast::make_void());
        }

        return std::make_unique<HirStmt>(std::move(hir_let));
    }

    // return文
    HirStmtPtr lower_return(ast::ReturnStmt& ret) {
        auto hir_ret = std::make_unique<HirReturn>();
        if (ret.value) {
            hir_ret->value = lower_expr(*ret.value);
        }
        return std::make_unique<HirStmt>(std::move(hir_ret));
    }

    // if文
    HirStmtPtr lower_if(ast::IfStmt& if_stmt) {
        auto hir_if = std::make_unique<HirIf>();
        hir_if->cond = lower_expr(*if_stmt.condition);

        for (auto& s : if_stmt.then_block) {
            if (auto hs = lower_stmt(*s)) {
                hir_if->then_block.push_back(std::move(hs));
            }
        }

        for (auto& s : if_stmt.else_block) {
            if (auto hs = lower_stmt(*s)) {
                hir_if->else_block.push_back(std::move(hs));
            }
        }

        return std::make_unique<HirStmt>(std::move(hir_if));
    }

    // while文 → HirWhileを直接生成（制御フロー構造を保持）
    HirStmtPtr lower_while(ast::WhileStmt& while_stmt) {
        auto hir_while = std::make_unique<HirWhile>();
        hir_while->cond = lower_expr(*while_stmt.condition);

        for (auto& s : while_stmt.body) {
            if (auto hs = lower_stmt(*s)) {
                hir_while->body.push_back(std::move(hs));
            }
        }

        return std::make_unique<HirStmt>(std::move(hir_while));
    }

    // for文 → HirForを直接生成（制御フロー構造を保持）
    HirStmtPtr lower_for(ast::ForStmt& for_stmt) {
        auto hir_for = std::make_unique<HirFor>();

        // init（nullable）
        if (for_stmt.init) {
            hir_for->init = lower_stmt(*for_stmt.init);
        }

        // cond（nullable - nullptrの場合は無限ループ）
        if (for_stmt.condition) {
            hir_for->cond = lower_expr(*for_stmt.condition);
        }

        // update（nullable）
        if (for_stmt.update) {
            hir_for->update = lower_expr(*for_stmt.update);
        }

        // body
        for (auto& s : for_stmt.body) {
            if (auto hs = lower_stmt(*s)) {
                hir_for->body.push_back(std::move(hs));
            }
        }

        return std::make_unique<HirStmt>(std::move(hir_for));
    }

    // for-in文 → 通常のforループに変換
    // for (T item in arr) { ... }
    // => for (int __i = 0; __i < arr.size(); __i = __i + 1) { T item = arr[__i]; ... }
    HirStmtPtr lower_for_in(ast::ForInStmt& for_in) {
        debug::hir::log(debug::hir::Id::LoopLower, "Lowering for-in statement",
                        debug::Level::Debug);

        auto hir_for = std::make_unique<HirFor>();

        // イテラブルの型からサイズを取得
        auto iterable_type = for_in.iterable->type;
        uint32_t size = 0;
        if (iterable_type && iterable_type->kind == ast::TypeKind::Array &&
            iterable_type->array_size.has_value()) {
            size = iterable_type->array_size.value();
        }

        // インデックス変数名を生成
        std::string idx_name = "__for_in_idx_" + for_in.var_name;

        // init: int __i = 0;
        auto init_let = std::make_unique<HirLet>();
        init_let->name = idx_name;
        init_let->type = ast::make_int();
        auto zero_lit = std::make_unique<HirLiteral>();
        zero_lit->value = int64_t{0};
        init_let->init = std::make_unique<HirExpr>(std::move(zero_lit), ast::make_int());
        hir_for->init = std::make_unique<HirStmt>(std::move(init_let));

        // cond: __i < size
        auto idx_ref = std::make_unique<HirVarRef>();
        idx_ref->name = idx_name;
        auto size_lit = std::make_unique<HirLiteral>();
        size_lit->value = static_cast<int64_t>(size);
        auto cond_binary = std::make_unique<HirBinary>();
        cond_binary->op = HirBinaryOp::Lt;
        cond_binary->lhs = std::make_unique<HirExpr>(std::move(idx_ref), ast::make_int());
        cond_binary->rhs = std::make_unique<HirExpr>(std::move(size_lit), ast::make_int());
        hir_for->cond = std::make_unique<HirExpr>(std::move(cond_binary), ast::make_bool());

        // update: __i = __i + 1
        // ASTの代入式を作成してlower_exprで変換
        auto ast_idx_ref_left = std::make_unique<ast::IdentExpr>(idx_name);
        auto ast_idx_ref_right = std::make_unique<ast::IdentExpr>(idx_name);
        auto ast_one = std::make_unique<ast::LiteralExpr>(int64_t{1});
        auto ast_add = std::make_unique<ast::BinaryExpr>(
            ast::BinaryOp::Add, std::make_unique<ast::Expr>(std::move(ast_idx_ref_right)),
            std::make_unique<ast::Expr>(std::move(ast_one)));
        auto ast_assign = std::make_unique<ast::BinaryExpr>(
            ast::BinaryOp::Assign, std::make_unique<ast::Expr>(std::move(ast_idx_ref_left)),
            std::make_unique<ast::Expr>(std::move(ast_add)));
        ast::Expr update_expr(std::move(ast_assign));
        update_expr.type = ast::make_int();
        hir_for->update = lower_expr(update_expr);

        // ループ本体の先頭にループ変数の初期化を追加
        // T item = arr[__i];
        auto elem_let = std::make_unique<HirLet>();
        elem_let->name = for_in.var_name;
        elem_let->type = for_in.var_type;

        // arr[__i] を構築
        auto arr_expr = lower_expr(*for_in.iterable);
        auto idx_ref3 = std::make_unique<HirVarRef>();
        idx_ref3->name = idx_name;
        auto index_expr = std::make_unique<HirIndex>();
        index_expr->object = std::move(arr_expr);
        index_expr->index = std::make_unique<HirExpr>(std::move(idx_ref3), ast::make_int());
        elem_let->init = std::make_unique<HirExpr>(std::move(index_expr), for_in.var_type);

        hir_for->body.push_back(std::make_unique<HirStmt>(std::move(elem_let)));

        // 残りのボディを追加
        for (auto& s : for_in.body) {
            if (auto hs = lower_stmt(*s)) {
                hir_for->body.push_back(std::move(hs));
            }
        }

        return std::make_unique<HirStmt>(std::move(hir_for));
    }

    // switch文
    HirStmtPtr lower_switch(ast::SwitchStmt& switch_stmt) {
        auto hir_switch = std::make_unique<HirSwitch>();
        hir_switch->expr = lower_expr(*switch_stmt.expr);

        for (auto& case_ : switch_stmt.cases) {
            HirSwitchCase hir_case;

            // patternがnullptrならelseケース
            if (case_.pattern) {
                hir_case.pattern = lower_pattern(*case_.pattern);

                // 後方互換性のため、単一値パターンの場合はvalueも設定
                // NOTE: value フィールドは後方互換性のため残しているが、
                // 新しい実装ではpatternフィールドを直接参照すること
            }

            // ケース内の文をlower（独立スコープとして扱う）
            // 各caseブロックは独自のスコープを持つ
            for (auto& stmt : case_.stmts) {
                if (auto hir_stmt = lower_stmt(*stmt)) {
                    hir_case.stmts.push_back(std::move(hir_stmt));
                }
            }

            hir_switch->cases.push_back(std::move(hir_case));
        }

        return std::make_unique<HirStmt>(std::move(hir_switch));
    }

    // ASTパターンをHIRパターンに変換
    std::unique_ptr<HirSwitchPattern> lower_pattern(ast::Pattern& pattern) {
        auto hir_pattern = std::make_unique<HirSwitchPattern>();

        switch (pattern.kind) {
            case ast::PatternKind::Value:
                hir_pattern->kind = HirSwitchPattern::SingleValue;
                hir_pattern->value = lower_expr(*pattern.value);
                break;

            case ast::PatternKind::Range:
                hir_pattern->kind = HirSwitchPattern::Range;
                hir_pattern->range_start = lower_expr(*pattern.range_start);
                hir_pattern->range_end = lower_expr(*pattern.range_end);
                break;

            case ast::PatternKind::Or:
                hir_pattern->kind = HirSwitchPattern::Or;
                for (auto& sub_pattern : pattern.or_patterns) {
                    hir_pattern->or_patterns.push_back(lower_pattern(*sub_pattern));
                }
                break;
        }

        return hir_pattern;
    }

    // 式文
    HirStmtPtr lower_expr_stmt(ast::ExprStmt& expr_stmt) {
        if (!expr_stmt.expr)
            return nullptr;

        auto hir = std::make_unique<HirExprStmt>();
        hir->expr = lower_expr(*expr_stmt.expr);
        return std::make_unique<HirStmt>(std::move(hir));
    }

    // 式の変換
    HirExprPtr lower_expr(ast::Expr& expr) {
        debug::hir::log(debug::hir::Id::ExprLower, "", debug::Level::Trace);
        TypePtr type = expr.type ? expr.type : make_error();

        if (type && type->kind != ast::TypeKind::Error) {
            debug::hir::log(debug::hir::Id::ExprType, type_to_string(*type), debug::Level::Trace);
        }

        if (auto* lit = expr.as<ast::LiteralExpr>()) {
            return lower_literal(*lit, type);
        } else if (auto* ident = expr.as<ast::IdentExpr>()) {
            debug::hir::log(debug::hir::Id::IdentifierLower, ident->name, debug::Level::Debug);

            // enum値アクセスかチェック（EnumName::MemberName形式）
            auto it = enum_values_.find(ident->name);
            if (it != enum_values_.end()) {
                // enum値を整数リテラルに変換
                debug::hir::log(debug::hir::Id::IdentifierRef,
                                "enum value: " + ident->name + " = " + std::to_string(it->second),
                                debug::Level::Debug);
                auto lit = std::make_unique<HirLiteral>();
                lit->value = it->second;
                return std::make_unique<HirExpr>(std::move(lit), ast::make_int());
            }

            debug::hir::log(debug::hir::Id::IdentifierRef, "variable: " + ident->name,
                            debug::Level::Trace);
            auto var_ref = std::make_unique<HirVarRef>();
            var_ref->name = ident->name;
            // 関数名への参照かチェック（関数ポインタ用）
            if (func_defs_.find(ident->name) != func_defs_.end()) {
                var_ref->is_function_ref = true;
                debug::hir::log(debug::hir::Id::IdentifierRef, "function reference: " + ident->name,
                                debug::Level::Debug);
            }
            return std::make_unique<HirExpr>(std::move(var_ref), type);
        } else if (auto* binary = expr.as<ast::BinaryExpr>()) {
            return lower_binary(*binary, type);
        } else if (auto* unary = expr.as<ast::UnaryExpr>()) {
            return lower_unary(*unary, type);
        } else if (auto* call = expr.as<ast::CallExpr>()) {
            return lower_call(*call, type);
        } else if (auto* idx = expr.as<ast::IndexExpr>()) {
            return lower_index(*idx, type);
        } else if (auto* slice = expr.as<ast::SliceExpr>()) {
            return lower_slice(*slice, type);
        } else if (auto* mem = expr.as<ast::MemberExpr>()) {
            return lower_member(*mem, type);
        } else if (auto* tern = expr.as<ast::TernaryExpr>()) {
            return lower_ternary(*tern, type);
        } else if (auto* match_expr = expr.as<ast::MatchExpr>()) {
            return lower_match(*match_expr, type);
        } else if (auto* struct_lit = expr.as<ast::StructLiteralExpr>()) {
            return lower_struct_literal(*struct_lit, type);
        } else if (auto* array_lit = expr.as<ast::ArrayLiteralExpr>()) {
            return lower_array_literal(*array_lit, type);
        }

        // フォールバック: nullリテラル
        debug::hir::log(debug::hir::Id::Warning, "Unknown expression type, using null literal",
                        debug::Level::Warn);
        auto lit = std::make_unique<HirLiteral>();
        return std::make_unique<HirExpr>(std::move(lit), type);
    }

    // リテラル
    HirExprPtr lower_literal(ast::LiteralExpr& lit, TypePtr type) {
        debug::hir::log(debug::hir::Id::LiteralLower, "", debug::Level::Trace);

        // リテラルの種類に応じてログ
        std::visit(
            [](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, int64_t>) {
                    debug::hir::log(debug::hir::Id::IntLiteral, std::to_string(arg),
                                    debug::Level::Trace);
                } else if constexpr (std::is_same_v<T, double>) {
                    debug::hir::log(debug::hir::Id::FloatLiteral, std::to_string(arg),
                                    debug::Level::Trace);
                } else if constexpr (std::is_same_v<T, std::string>) {
                    debug::hir::log(debug::hir::Id::StringLiteral, "\"" + arg + "\"",
                                    debug::Level::Trace);
                } else if constexpr (std::is_same_v<T, bool>) {
                    debug::hir::log(debug::hir::Id::BoolLiteral, arg ? "true" : "false",
                                    debug::Level::Trace);
                } else if constexpr (std::is_same_v<T, char>) {
                    debug::hir::log(debug::hir::Id::CharLiteral, std::string(1, arg),
                                    debug::Level::Trace);
                } else if constexpr (std::is_same_v<T, std::monostate>) {
                    debug::hir::log(debug::hir::Id::NullLiteral, "null", debug::Level::Trace);
                }
            },
            lit.value);

        auto hir_lit = std::make_unique<HirLiteral>();
        hir_lit->value = lit.value;
        return std::make_unique<HirExpr>(std::move(hir_lit), type);
    }

    // 二項演算
    HirExprPtr lower_binary(ast::BinaryExpr& binary, TypePtr type) {
        debug::hir::log(debug::hir::Id::BinaryExprLower, "", debug::Level::Debug);

        // 複合代入演算子を脱糖: x += y → x = x + y
        if (is_compound_assign(binary.op)) {
            debug::hir::log(debug::hir::Id::DesugarPass, "Compound assignment",
                            debug::Level::Trace);
            auto base_op = get_base_op(binary.op);

            // x op= y → x = x op y
            auto inner = std::make_unique<HirBinary>();
            inner->op = base_op;
            debug::hir::log(debug::hir::Id::BinaryLhs, "Evaluating left for inner op",
                            debug::Level::Trace);
            inner->lhs = lower_expr(*binary.left);
            debug::hir::log(debug::hir::Id::BinaryRhs, "Evaluating right for inner op",
                            debug::Level::Trace);
            inner->rhs = lower_expr(*binary.right);

            auto outer = std::make_unique<HirBinary>();
            outer->op = HirBinaryOp::Assign;
            debug::hir::log(debug::hir::Id::BinaryLhs, "Re-evaluating left for assignment",
                            debug::Level::Trace);
            outer->lhs = lower_expr(*binary.left);  // 左辺を再度評価
            outer->rhs = std::make_unique<HirExpr>(std::move(inner), type);

            return std::make_unique<HirExpr>(std::move(outer), type);
        }

        // 代入演算子の場合
        if (binary.op == ast::BinaryOp::Assign) {
            debug::hir::log(debug::hir::Id::AssignLower, "Assignment detected",
                            debug::Level::Debug);

            auto lhs_type = binary.left->type;
            auto rhs_type = binary.right->type;

            // 暗黙的構造体リテラルに左辺の型を伝播
            if (lhs_type && lhs_type->kind == ast::TypeKind::Struct) {
                if (auto* struct_lit = binary.right->as<ast::StructLiteralExpr>()) {
                    if (struct_lit->type_name.empty()) {
                        struct_lit->type_name = lhs_type->name;
                        debug::hir::log(
                            debug::hir::Id::AssignLower,
                            "Propagated type to implicit struct literal in assignment: " +
                                lhs_type->name,
                            debug::Level::Debug);
                    }
                }
            }

            // 配列リテラルへの型伝播
            if (lhs_type && lhs_type->kind == ast::TypeKind::Array && lhs_type->element_type) {
                if (auto* array_lit = binary.right->as<ast::ArrayLiteralExpr>()) {
                    // 配列要素が構造体の場合、各要素に型を伝播
                    if (lhs_type->element_type->kind == ast::TypeKind::Struct) {
                        for (auto& elem : array_lit->elements) {
                            if (auto* struct_lit = elem->as<ast::StructLiteralExpr>()) {
                                if (struct_lit->type_name.empty()) {
                                    struct_lit->type_name = lhs_type->element_type->name;
                                }
                            }
                        }
                    }
                }
            }

            // defaultメンバへの暗黙的な代入をチェック
            // 左辺が構造体で右辺がプリミティブの場合、defaultメンバへの代入に変換
            if (lhs_type && lhs_type->kind == ast::TypeKind::Struct && rhs_type &&
                rhs_type->kind != ast::TypeKind::Struct) {
                std::string default_member = get_default_member_name(lhs_type->name);
                if (!default_member.empty()) {
                    debug::hir::log(debug::hir::Id::AssignLower,
                                    "Converting to default member assignment: " + default_member,
                                    debug::Level::Debug);
                    // w = 20 → w.value = 20 に変換
                    auto hir = std::make_unique<HirBinary>();
                    hir->op = HirBinaryOp::Assign;

                    // 左辺をメンバアクセスに変換
                    auto member = std::make_unique<HirMember>();
                    member->object = lower_expr(*binary.left);
                    member->member = default_member;
                    hir->lhs = std::make_unique<HirExpr>(std::move(member), rhs_type);

                    hir->rhs = lower_expr(*binary.right);
                    return std::make_unique<HirExpr>(std::move(hir), type);
                }
            }
        }

        auto hir = std::make_unique<HirBinary>();
        hir->op = convert_binary_op(binary.op);
        debug::hir::log(debug::hir::Id::BinaryOp, hir_binary_op_to_string(hir->op),
                        debug::Level::Trace);

        debug::hir::log(debug::hir::Id::BinaryLhs, "Evaluating left operand", debug::Level::Trace);
        hir->lhs = lower_expr(*binary.left);
        debug::hir::log(debug::hir::Id::BinaryRhs, "Evaluating right operand", debug::Level::Trace);
        hir->rhs = lower_expr(*binary.right);
        return std::make_unique<HirExpr>(std::move(hir), type);
    }

    // 複合代入演算子かどうか判定
    bool is_compound_assign(ast::BinaryOp op) {
        switch (op) {
            case ast::BinaryOp::AddAssign:
            case ast::BinaryOp::SubAssign:
            case ast::BinaryOp::MulAssign:
            case ast::BinaryOp::DivAssign:
            case ast::BinaryOp::ModAssign:
            case ast::BinaryOp::BitAndAssign:
            case ast::BinaryOp::BitOrAssign:
            case ast::BinaryOp::BitXorAssign:
            case ast::BinaryOp::ShlAssign:
            case ast::BinaryOp::ShrAssign:
                return true;
            default:
                return false;
        }
    }

    // 複合代入の基本演算子を取得
    HirBinaryOp get_base_op(ast::BinaryOp op) {
        switch (op) {
            case ast::BinaryOp::AddAssign:
                return HirBinaryOp::Add;
            case ast::BinaryOp::SubAssign:
                return HirBinaryOp::Sub;
            case ast::BinaryOp::MulAssign:
                return HirBinaryOp::Mul;
            case ast::BinaryOp::DivAssign:
                return HirBinaryOp::Div;
            case ast::BinaryOp::ModAssign:
                return HirBinaryOp::Mod;
            case ast::BinaryOp::BitAndAssign:
                return HirBinaryOp::BitAnd;
            case ast::BinaryOp::BitOrAssign:
                return HirBinaryOp::BitOr;
            case ast::BinaryOp::BitXorAssign:
                return HirBinaryOp::BitXor;
            case ast::BinaryOp::ShlAssign:
                return HirBinaryOp::Shl;
            case ast::BinaryOp::ShrAssign:
                return HirBinaryOp::Shr;
            default:
                return HirBinaryOp::Add;
        }
    }

    HirBinaryOp convert_binary_op(ast::BinaryOp op) {
        switch (op) {
            case ast::BinaryOp::Add:
                return HirBinaryOp::Add;
            case ast::BinaryOp::Sub:
                return HirBinaryOp::Sub;
            case ast::BinaryOp::Mul:
                return HirBinaryOp::Mul;
            case ast::BinaryOp::Div:
                return HirBinaryOp::Div;
            case ast::BinaryOp::Mod:
                return HirBinaryOp::Mod;
            case ast::BinaryOp::BitAnd:
                return HirBinaryOp::BitAnd;
            case ast::BinaryOp::BitOr:
                return HirBinaryOp::BitOr;
            case ast::BinaryOp::BitXor:
                return HirBinaryOp::BitXor;
            case ast::BinaryOp::Shl:
                return HirBinaryOp::Shl;
            case ast::BinaryOp::Shr:
                return HirBinaryOp::Shr;
            case ast::BinaryOp::And:
                return HirBinaryOp::And;
            case ast::BinaryOp::Or:
                return HirBinaryOp::Or;
            case ast::BinaryOp::Eq:
                return HirBinaryOp::Eq;
            case ast::BinaryOp::Ne:
                return HirBinaryOp::Ne;
            case ast::BinaryOp::Lt:
                return HirBinaryOp::Lt;
            case ast::BinaryOp::Gt:
                return HirBinaryOp::Gt;
            case ast::BinaryOp::Le:
                return HirBinaryOp::Le;
            case ast::BinaryOp::Ge:
                return HirBinaryOp::Ge;
            case ast::BinaryOp::Assign:
                return HirBinaryOp::Assign;
            default:
                return HirBinaryOp::Add;
        }
    }

    // 単項演算
    HirExprPtr lower_unary(ast::UnaryExpr& unary, TypePtr type) {
        debug::hir::log(debug::hir::Id::UnaryExprLower, "", debug::Level::Debug);
        auto hir = std::make_unique<HirUnary>();
        hir->op = convert_unary_op(unary.op);
        debug::hir::log(debug::hir::Id::UnaryOp, hir_unary_op_to_string(hir->op),
                        debug::Level::Trace);

        debug::hir::log(debug::hir::Id::UnaryOperand, "Evaluating operand", debug::Level::Trace);
        hir->operand = lower_expr(*unary.operand);
        return std::make_unique<HirExpr>(std::move(hir), type);
    }

    std::string hir_binary_op_to_string(HirBinaryOp op) {
        switch (op) {
            case HirBinaryOp::Add:
                return "Add";
            case HirBinaryOp::Sub:
                return "Sub";
            case HirBinaryOp::Mul:
                return "Mul";
            case HirBinaryOp::Div:
                return "Div";
            case HirBinaryOp::Mod:
                return "Mod";
            case HirBinaryOp::BitAnd:
                return "BitAnd";
            case HirBinaryOp::BitOr:
                return "BitOr";
            case HirBinaryOp::BitXor:
                return "BitXor";
            case HirBinaryOp::Shl:
                return "Shl";
            case HirBinaryOp::Shr:
                return "Shr";
            case HirBinaryOp::And:
                return "And";
            case HirBinaryOp::Or:
                return "Or";
            case HirBinaryOp::Eq:
                return "Eq";
            case HirBinaryOp::Ne:
                return "Ne";
            case HirBinaryOp::Lt:
                return "Lt";
            case HirBinaryOp::Gt:
                return "Gt";
            case HirBinaryOp::Le:
                return "Le";
            case HirBinaryOp::Ge:
                return "Ge";
            case HirBinaryOp::Assign:
                return "Assign";
            default:
                return "Unknown";
        }
    }

    std::string hir_unary_op_to_string(HirUnaryOp op) {
        switch (op) {
            case HirUnaryOp::Neg:
                return "Neg";
            case HirUnaryOp::Not:
                return "Not";
            case HirUnaryOp::BitNot:
                return "BitNot";
            case HirUnaryOp::Deref:
                return "Deref";
            case HirUnaryOp::AddrOf:
                return "AddrOf";
            case HirUnaryOp::PreInc:
                return "PreInc";
            case HirUnaryOp::PreDec:
                return "PreDec";
            case HirUnaryOp::PostInc:
                return "PostInc";
            case HirUnaryOp::PostDec:
                return "PostDec";
            default:
                return "Unknown";
        }
    }

    HirUnaryOp convert_unary_op(ast::UnaryOp op) {
        switch (op) {
            case ast::UnaryOp::Neg:
                return HirUnaryOp::Neg;
            case ast::UnaryOp::Not:
                return HirUnaryOp::Not;
            case ast::UnaryOp::BitNot:
                return HirUnaryOp::BitNot;
            case ast::UnaryOp::Deref:
                return HirUnaryOp::Deref;
            case ast::UnaryOp::AddrOf:
                return HirUnaryOp::AddrOf;
            case ast::UnaryOp::PreInc:
                return HirUnaryOp::PreInc;
            case ast::UnaryOp::PreDec:
                return HirUnaryOp::PreDec;
            case ast::UnaryOp::PostInc:
                return HirUnaryOp::PostInc;
            case ast::UnaryOp::PostDec:
                return HirUnaryOp::PostDec;
            default:
                return HirUnaryOp::Neg;
        }
    }

    // 関数呼び出し
    HirExprPtr lower_call(ast::CallExpr& call, TypePtr type) {
        debug::hir::log(debug::hir::Id::CallExprLower, "", debug::Level::Debug);
        auto hir = std::make_unique<HirCall>();

        // 呼び出し先の名前を取得
        std::string func_name;
        if (auto* ident = call.callee->as<ast::IdentExpr>()) {
            func_name = ident->name;
            hir->func_name = func_name;
            debug::hir::log(debug::hir::Id::CallTarget, "function: " + func_name,
                            debug::Level::Trace);

            // 組み込み関数リスト
            static const std::set<std::string> builtin_funcs = {"println", "print", "printf",
                                                                "sprintf", "exit",  "panic"};

            // 関数ポインタからの呼び出しかチェック
            // - func_defs_にある場合は通常の関数呼び出し
            // - 組み込み関数の場合は通常の関数呼び出し
            // - 名前空間付き関数呼び出し（::を含む）は直接呼び出し
            // - それ以外は関数ポインタ変数
            bool is_builtin = builtin_funcs.find(func_name) != builtin_funcs.end();
            bool is_defined = func_defs_.find(func_name) != func_defs_.end();
            bool is_namespaced = func_name.find("::") != std::string::npos;

            if (!is_builtin && !is_defined && !is_namespaced) {
                hir->is_indirect = true;
                debug::hir::log(debug::hir::Id::CallTarget,
                                "indirect call via variable: " + func_name, debug::Level::Debug);
            }
        } else {
            hir->func_name = "<indirect>";
            hir->is_indirect = true;
            debug::hir::log(debug::hir::Id::CallTarget, "indirect call", debug::Level::Trace);
        }

        // 明示的に渡された引数をlower
        debug::hir::log(debug::hir::Id::CallArgs, "count=" + std::to_string(call.args.size()),
                        debug::Level::Trace);
        for (size_t i = 0; i < call.args.size(); i++) {
            debug::hir::log(debug::hir::Id::CallArgEval, "arg[" + std::to_string(i) + "]",
                            debug::Level::Trace);
            hir->args.push_back(lower_expr(*call.args[i]));
        }

        // デフォルト引数を適用
        if (!func_name.empty() && !hir->is_indirect) {
            auto func_it = func_defs_.find(func_name);
            if (func_it != func_defs_.end()) {
                const auto* func_def = func_it->second;
                // 不足している引数にデフォルト値を追加
                for (size_t i = call.args.size(); i < func_def->params.size(); ++i) {
                    const auto& param = func_def->params[i];
                    if (param.default_value) {
                        debug::hir::log(debug::hir::Id::CallArgEval,
                                        "default arg[" + std::to_string(i) + "] for " + param.name,
                                        debug::Level::Trace);
                        hir->args.push_back(lower_expr(*param.default_value));
                    }
                }
            }
        }

        return std::make_unique<HirExpr>(std::move(hir), type);
    }

    // 配列アクセス
    HirExprPtr lower_index(ast::IndexExpr& idx, TypePtr type) {
        debug::hir::log(debug::hir::Id::IndexLower, "", debug::Level::Debug);

        // オブジェクトの型を確認
        auto obj_hir = lower_expr(*idx.object);
        TypePtr obj_type = obj_hir->type;

        // 文字列インデックスの場合: __builtin_string_charAt に変換
        if (obj_type && obj_type->kind == ast::TypeKind::String) {
            debug::hir::log(debug::hir::Id::IndexLower, "String index access", debug::Level::Debug);
            auto hir = std::make_unique<HirCall>();
            hir->func_name = "__builtin_string_charAt";
            hir->args.push_back(std::move(obj_hir));
            hir->args.push_back(lower_expr(*idx.index));
            return std::make_unique<HirExpr>(std::move(hir), ast::make_char());
        }

        // 通常の配列/ポインタインデックス
        auto hir = std::make_unique<HirIndex>();
        debug::hir::log(debug::hir::Id::IndexBase, "Evaluating base", debug::Level::Trace);
        hir->object = std::move(obj_hir);
        debug::hir::log(debug::hir::Id::IndexValue, "Evaluating index", debug::Level::Trace);
        hir->index = lower_expr(*idx.index);
        return std::make_unique<HirExpr>(std::move(hir), type);
    }

    // スライス式: arr[start:end:step]
    // 文字列スライスの場合: __builtin_string_substring に変換
    // 配列スライスの場合: __builtin_array_slice に変換
    HirExprPtr lower_slice(ast::SliceExpr& slice, TypePtr type) {
        debug::hir::log(debug::hir::Id::IndexLower, "Slice expression", debug::Level::Debug);

        auto obj_hir = lower_expr(*slice.object);
        TypePtr obj_type = obj_hir->type;

        // 文字列スライス
        if (obj_type && obj_type->kind == ast::TypeKind::String) {
            auto hir = std::make_unique<HirCall>();
            hir->func_name = "__builtin_string_substring";
            hir->args.push_back(std::move(obj_hir));

            // start (デフォルト: 0)
            if (slice.start) {
                hir->args.push_back(lower_expr(*slice.start));
            } else {
                auto zero = std::make_unique<HirLiteral>();
                zero->value = int64_t{0};
                hir->args.push_back(std::make_unique<HirExpr>(std::move(zero), ast::make_int()));
            }

            // end (デフォルト: -1 = 最後まで)
            if (slice.end) {
                hir->args.push_back(lower_expr(*slice.end));
            } else {
                auto neg_one = std::make_unique<HirLiteral>();
                neg_one->value = int64_t{-1};
                hir->args.push_back(std::make_unique<HirExpr>(std::move(neg_one), ast::make_int()));
            }

            // step は文字列スライスでは現時点では無視
            if (slice.step) {
                debug::hir::log(debug::hir::Id::Warning, "String slice step not yet supported",
                                debug::Level::Warn);
            }

            return std::make_unique<HirExpr>(std::move(hir), ast::make_string());
        }

        // 配列スライス
        if (obj_type && obj_type->kind == ast::TypeKind::Array) {
            debug::hir::log(debug::hir::Id::IndexLower, "Array slice", debug::Level::Debug);
            auto hir = std::make_unique<HirCall>();
            hir->func_name = "__builtin_array_slice";

            // 配列へのポインタ
            hir->args.push_back(std::move(obj_hir));

            // 要素サイズ（型に応じて計算）
            int64_t elem_size = 8;  // デフォルト: 64ビット
            if (obj_type->element_type) {
                switch (obj_type->element_type->kind) {
                    case ast::TypeKind::Tiny:
                    case ast::TypeKind::UTiny:
                    case ast::TypeKind::Char:
                    case ast::TypeKind::Bool:
                        elem_size = 1;
                        break;
                    case ast::TypeKind::Short:
                    case ast::TypeKind::UShort:
                        elem_size = 2;
                        break;
                    case ast::TypeKind::Int:
                    case ast::TypeKind::UInt:
                    case ast::TypeKind::Float:
                        elem_size = 4;
                        break;
                    case ast::TypeKind::Long:
                    case ast::TypeKind::ULong:
                    case ast::TypeKind::Double:
                    case ast::TypeKind::Pointer:
                        elem_size = 8;
                        break;
                    default:
                        elem_size = 8;
                        break;
                }
            }
            auto elem_size_lit = std::make_unique<HirLiteral>();
            elem_size_lit->value = elem_size;
            hir->args.push_back(
                std::make_unique<HirExpr>(std::move(elem_size_lit), ast::make_int()));

            // 配列長
            int64_t arr_len = obj_type->array_size.value_or(0);
            auto arr_len_lit = std::make_unique<HirLiteral>();
            arr_len_lit->value = arr_len;
            hir->args.push_back(std::make_unique<HirExpr>(std::move(arr_len_lit), ast::make_int()));

            // start (デフォルト: 0)
            if (slice.start) {
                hir->args.push_back(lower_expr(*slice.start));
            } else {
                auto zero = std::make_unique<HirLiteral>();
                zero->value = int64_t{0};
                hir->args.push_back(std::make_unique<HirExpr>(std::move(zero), ast::make_int()));
            }

            // end (デフォルト: -1 = 最後まで)
            if (slice.end) {
                hir->args.push_back(lower_expr(*slice.end));
            } else {
                auto neg_one = std::make_unique<HirLiteral>();
                neg_one->value = int64_t{-1};
                hir->args.push_back(std::make_unique<HirExpr>(std::move(neg_one), ast::make_int()));
            }

            // step は配列スライスでも現時点では無視
            if (slice.step) {
                debug::hir::log(debug::hir::Id::Warning, "Array slice step not yet supported",
                                debug::Level::Warn);
            }

            // 戻り値の型: 動的サイズの配列（ポインタ）
            return std::make_unique<HirExpr>(std::move(hir), type);
        }

        // その他の型: 未サポート
        debug::hir::log(debug::hir::Id::Warning, "Slice on unsupported type", debug::Level::Warn);

        // フォールバック: 空のリテラル
        auto lit = std::make_unique<HirLiteral>();
        return std::make_unique<HirExpr>(std::move(lit), type);
    }

    // メンバアクセス / メソッド呼び出し
    HirExprPtr lower_member(ast::MemberExpr& mem, TypePtr type) {
        // メソッド呼び出しの場合
        if (mem.is_method_call) {
            debug::hir::log(
                debug::hir::Id::MethodCallLower,
                "method: " + mem.member + " with " + std::to_string(mem.args.size()) + " args",
                debug::Level::Debug);

            // オブジェクトの型を取得
            auto obj_hir = lower_expr(*mem.object);
            std::string type_name;

            // HIR式から型を取得
            TypePtr obj_type = nullptr;
            if (obj_hir->type) {
                obj_type = obj_hir->type;
                type_name = ast::type_to_string(*obj_hir->type);
            } else if (mem.object->type) {
                obj_type = mem.object->type;
                type_name = ast::type_to_string(*mem.object->type);
            }

            // 配列のビルトインメソッド処理
            if (obj_type && obj_type->kind == ast::TypeKind::Array) {
                if (mem.member == "size" || mem.member == "len" || mem.member == "length") {
                    // 配列サイズはコンパイル時定数として返す
                    auto lit = std::make_unique<HirLiteral>();
                    if (obj_type->array_size.has_value()) {
                        lit->value = static_cast<int64_t>(obj_type->array_size.value());
                    } else {
                        lit->value = int64_t{0};  // 動的配列の場合（将来対応）
                    }
                    debug::hir::log(debug::hir::Id::MethodCallLower,
                                    "Array builtin size() = " +
                                        std::to_string(obj_type->array_size.value_or(0)),
                                    debug::Level::Debug);
                    return std::make_unique<HirExpr>(std::move(lit), ast::make_uint());
                }

                // forEach: 各要素に対して関数を実行
                if (mem.member == "forEach") {
                    auto hir = std::make_unique<HirCall>();
                    hir->func_name = "__builtin_array_forEach";
                    hir->args.push_back(std::move(obj_hir));
                    // 配列サイズ
                    auto size_lit = std::make_unique<HirLiteral>();
                    size_lit->value = static_cast<int64_t>(obj_type->array_size.value_or(0));
                    hir->args.push_back(
                        std::make_unique<HirExpr>(std::move(size_lit), ast::make_int()));
                    // コールバック関数
                    for (auto& arg : mem.args) {
                        hir->args.push_back(lower_expr(*arg));
                    }
                    debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin forEach()",
                                    debug::Level::Debug);
                    return std::make_unique<HirExpr>(std::move(hir), ast::make_void());
                }

                // reduce: 要素を畳み込み
                if (mem.member == "reduce") {
                    auto hir = std::make_unique<HirCall>();
                    hir->func_name = "__builtin_array_reduce";
                    hir->args.push_back(std::move(obj_hir));
                    // 配列サイズ
                    auto size_lit = std::make_unique<HirLiteral>();
                    size_lit->value = static_cast<int64_t>(obj_type->array_size.value_or(0));
                    hir->args.push_back(
                        std::make_unique<HirExpr>(std::move(size_lit), ast::make_int()));
                    // コールバック関数と初期値
                    for (auto& arg : mem.args) {
                        hir->args.push_back(lower_expr(*arg));
                    }
                    debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin reduce()",
                                    debug::Level::Debug);
                    // 戻り値は初期値と同じ型（int型と仮定）
                    return std::make_unique<HirExpr>(std::move(hir), ast::make_int());
                }

                // some: いずれかの要素が条件を満たすか
                if (mem.member == "some") {
                    auto hir = std::make_unique<HirCall>();
                    hir->func_name = "__builtin_array_some_i32";
                    // 配列のアドレスを取得
                    auto addr_op = std::make_unique<HirUnary>();
                    addr_op->op = HirUnaryOp::AddrOf;
                    addr_op->operand = std::move(obj_hir);
                    auto ptr_type = ast::make_pointer(obj_type->element_type);
                    hir->args.push_back(std::make_unique<HirExpr>(std::move(addr_op), ptr_type));
                    auto size_lit = std::make_unique<HirLiteral>();
                    size_lit->value = static_cast<int64_t>(obj_type->array_size.value_or(0));
                    hir->args.push_back(
                        std::make_unique<HirExpr>(std::move(size_lit), ast::make_int()));
                    for (auto& arg : mem.args) {
                        hir->args.push_back(lower_expr(*arg));
                    }
                    debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin some()",
                                    debug::Level::Debug);
                    return std::make_unique<HirExpr>(std::move(hir), ast::make_bool());
                }

                // every: すべての要素が条件を満たすか
                if (mem.member == "every") {
                    auto hir = std::make_unique<HirCall>();
                    hir->func_name = "__builtin_array_every_i32";
                    // 配列のアドレスを取得
                    auto addr_op = std::make_unique<HirUnary>();
                    addr_op->op = HirUnaryOp::AddrOf;
                    addr_op->operand = std::move(obj_hir);
                    auto ptr_type = ast::make_pointer(obj_type->element_type);
                    hir->args.push_back(std::make_unique<HirExpr>(std::move(addr_op), ptr_type));
                    auto size_lit = std::make_unique<HirLiteral>();
                    size_lit->value = static_cast<int64_t>(obj_type->array_size.value_or(0));
                    hir->args.push_back(
                        std::make_unique<HirExpr>(std::move(size_lit), ast::make_int()));
                    for (auto& arg : mem.args) {
                        hir->args.push_back(lower_expr(*arg));
                    }
                    debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin every()",
                                    debug::Level::Debug);
                    return std::make_unique<HirExpr>(std::move(hir), ast::make_bool());
                }

                // findIndex: 条件を満たす最初の要素のインデックス (-1 if not found)
                if (mem.member == "findIndex") {
                    auto hir = std::make_unique<HirCall>();
                    hir->func_name = "__builtin_array_findIndex_i32";
                    // 配列のアドレスを取得
                    auto addr_op = std::make_unique<HirUnary>();
                    addr_op->op = HirUnaryOp::AddrOf;
                    addr_op->operand = std::move(obj_hir);
                    auto ptr_type = ast::make_pointer(obj_type->element_type);
                    hir->args.push_back(std::make_unique<HirExpr>(std::move(addr_op), ptr_type));
                    auto size_lit = std::make_unique<HirLiteral>();
                    size_lit->value = static_cast<int64_t>(obj_type->array_size.value_or(0));
                    hir->args.push_back(
                        std::make_unique<HirExpr>(std::move(size_lit), ast::make_int()));
                    for (auto& arg : mem.args) {
                        hir->args.push_back(lower_expr(*arg));
                    }
                    debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin findIndex()",
                                    debug::Level::Debug);
                    return std::make_unique<HirExpr>(std::move(hir), ast::make_int());
                }

                // indexOf: 要素の位置を検索
                if (mem.member == "indexOf") {
                    auto hir = std::make_unique<HirCall>();
                    hir->func_name = "__builtin_array_indexOf_i32";  // int型配列用
                    // 配列のアドレスを取得
                    auto addr_op = std::make_unique<HirUnary>();
                    addr_op->op = HirUnaryOp::AddrOf;
                    addr_op->operand = std::move(obj_hir);
                    auto ptr_type = ast::make_pointer(obj_type->element_type);
                    hir->args.push_back(std::make_unique<HirExpr>(std::move(addr_op), ptr_type));
                    // 配列サイズ
                    auto size_lit = std::make_unique<HirLiteral>();
                    size_lit->value = static_cast<int64_t>(obj_type->array_size.value_or(0));
                    hir->args.push_back(
                        std::make_unique<HirExpr>(std::move(size_lit), ast::make_int()));
                    for (auto& arg : mem.args) {
                        hir->args.push_back(lower_expr(*arg));
                    }
                    debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin indexOf()",
                                    debug::Level::Debug);
                    return std::make_unique<HirExpr>(std::move(hir), ast::make_int());
                }

                // includes/contains: 要素が含まれているか
                if (mem.member == "includes" || mem.member == "contains") {
                    auto hir = std::make_unique<HirCall>();
                    hir->func_name = "__builtin_array_includes_i32";  // int型配列用
                    // 配列のアドレスを取得
                    auto addr_op = std::make_unique<HirUnary>();
                    addr_op->op = HirUnaryOp::AddrOf;
                    addr_op->operand = std::move(obj_hir);
                    auto ptr_type = ast::make_pointer(obj_type->element_type);
                    hir->args.push_back(std::make_unique<HirExpr>(std::move(addr_op), ptr_type));
                    // 配列サイズ
                    auto size_lit = std::make_unique<HirLiteral>();
                    size_lit->value = static_cast<int64_t>(obj_type->array_size.value_or(0));
                    hir->args.push_back(
                        std::make_unique<HirExpr>(std::move(size_lit), ast::make_int()));
                    for (auto& arg : mem.args) {
                        hir->args.push_back(lower_expr(*arg));
                    }
                    debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin includes()",
                                    debug::Level::Debug);
                    return std::make_unique<HirExpr>(std::move(hir), ast::make_bool());
                }
            }

            // 文字列のビルトインメソッド処理
            if (obj_type && obj_type->kind == ast::TypeKind::String) {
                if (mem.member == "len" || mem.member == "size" || mem.member == "length") {
                    // 文字列の長さを取得するビルトイン関数呼び出し
                    auto hir = std::make_unique<HirCall>();
                    hir->func_name = "__builtin_string_len";
                    hir->args.push_back(std::move(obj_hir));
                    debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin len()",
                                    debug::Level::Debug);
                    return std::make_unique<HirExpr>(std::move(hir), ast::make_uint());
                }
                if (mem.member == "charAt" || mem.member == "at") {
                    // 指定位置の文字を取得
                    auto hir = std::make_unique<HirCall>();
                    hir->func_name = "__builtin_string_charAt";
                    hir->args.push_back(std::move(obj_hir));
                    for (auto& arg : mem.args) {
                        hir->args.push_back(lower_expr(*arg));
                    }
                    debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin charAt()",
                                    debug::Level::Debug);
                    return std::make_unique<HirExpr>(std::move(hir), ast::make_char());
                }
                if (mem.member == "substring" || mem.member == "slice") {
                    // 部分文字列を取得
                    auto hir = std::make_unique<HirCall>();
                    hir->func_name = "__builtin_string_substring";
                    hir->args.push_back(std::move(obj_hir));
                    for (auto& arg : mem.args) {
                        hir->args.push_back(lower_expr(*arg));
                    }
                    debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin substring()",
                                    debug::Level::Debug);
                    return std::make_unique<HirExpr>(std::move(hir), ast::make_string());
                }
                if (mem.member == "indexOf") {
                    // 部分文字列の位置を検索
                    auto hir = std::make_unique<HirCall>();
                    hir->func_name = "__builtin_string_indexOf";
                    hir->args.push_back(std::move(obj_hir));
                    for (auto& arg : mem.args) {
                        hir->args.push_back(lower_expr(*arg));
                    }
                    debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin indexOf()",
                                    debug::Level::Debug);
                    return std::make_unique<HirExpr>(std::move(hir), ast::make_int());
                }
                if (mem.member == "toUpperCase") {
                    auto hir = std::make_unique<HirCall>();
                    hir->func_name = "__builtin_string_toUpperCase";
                    hir->args.push_back(std::move(obj_hir));
                    debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin toUpperCase()",
                                    debug::Level::Debug);
                    return std::make_unique<HirExpr>(std::move(hir), ast::make_string());
                }
                if (mem.member == "toLowerCase") {
                    auto hir = std::make_unique<HirCall>();
                    hir->func_name = "__builtin_string_toLowerCase";
                    hir->args.push_back(std::move(obj_hir));
                    debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin toLowerCase()",
                                    debug::Level::Debug);
                    return std::make_unique<HirExpr>(std::move(hir), ast::make_string());
                }
                if (mem.member == "trim") {
                    auto hir = std::make_unique<HirCall>();
                    hir->func_name = "__builtin_string_trim";
                    hir->args.push_back(std::move(obj_hir));
                    debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin trim()",
                                    debug::Level::Debug);
                    return std::make_unique<HirExpr>(std::move(hir), ast::make_string());
                }
                if (mem.member == "startsWith") {
                    auto hir = std::make_unique<HirCall>();
                    hir->func_name = "__builtin_string_startsWith";
                    hir->args.push_back(std::move(obj_hir));
                    for (auto& arg : mem.args) {
                        hir->args.push_back(lower_expr(*arg));
                    }
                    debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin startsWith()",
                                    debug::Level::Debug);
                    return std::make_unique<HirExpr>(std::move(hir), ast::make_bool());
                }
                if (mem.member == "endsWith") {
                    auto hir = std::make_unique<HirCall>();
                    hir->func_name = "__builtin_string_endsWith";
                    hir->args.push_back(std::move(obj_hir));
                    for (auto& arg : mem.args) {
                        hir->args.push_back(lower_expr(*arg));
                    }
                    debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin endsWith()",
                                    debug::Level::Debug);
                    return std::make_unique<HirExpr>(std::move(hir), ast::make_bool());
                }
                if (mem.member == "includes" || mem.member == "contains") {
                    auto hir = std::make_unique<HirCall>();
                    hir->func_name = "__builtin_string_includes";
                    hir->args.push_back(std::move(obj_hir));
                    for (auto& arg : mem.args) {
                        hir->args.push_back(lower_expr(*arg));
                    }
                    debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin includes()",
                                    debug::Level::Debug);
                    return std::make_unique<HirExpr>(std::move(hir), ast::make_bool());
                }
                if (mem.member == "repeat") {
                    auto hir = std::make_unique<HirCall>();
                    hir->func_name = "__builtin_string_repeat";
                    hir->args.push_back(std::move(obj_hir));
                    for (auto& arg : mem.args) {
                        hir->args.push_back(lower_expr(*arg));
                    }
                    debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin repeat()",
                                    debug::Level::Debug);
                    return std::make_unique<HirExpr>(std::move(hir), ast::make_string());
                }
                if (mem.member == "replace") {
                    auto hir = std::make_unique<HirCall>();
                    hir->func_name = "__builtin_string_replace";
                    hir->args.push_back(std::move(obj_hir));
                    for (auto& arg : mem.args) {
                        hir->args.push_back(lower_expr(*arg));
                    }
                    debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin replace()",
                                    debug::Level::Debug);
                    return std::make_unique<HirExpr>(std::move(hir), ast::make_string());
                }
            }

            // 名前空間を除去した型名を取得（メソッド呼び出し用）
            std::string method_type_name = type_name;
            size_t last_colon = type_name.rfind("::");
            if (last_colon != std::string::npos) {
                method_type_name = type_name.substr(last_colon + 2);
            }

            auto hir = std::make_unique<HirCall>();
            hir->func_name = method_type_name + "__" + mem.member;

            // selfを第一引数として追加
            hir->args.push_back(std::move(obj_hir));

            // 残りの引数を追加
            for (auto& arg : mem.args) {
                hir->args.push_back(lower_expr(*arg));
            }

            return std::make_unique<HirExpr>(std::move(hir), type);
        }

        // 通常のフィールドアクセス
        debug::hir::log(debug::hir::Id::FieldAccessLower, "", debug::Level::Debug);
        auto hir = std::make_unique<HirMember>();
        hir->object = lower_expr(*mem.object);
        hir->member = mem.member;
        debug::hir::log(debug::hir::Id::FieldName, "field: " + mem.member, debug::Level::Trace);
        return std::make_unique<HirExpr>(std::move(hir), type);
    }

    // 三項演算子
    HirExprPtr lower_ternary(ast::TernaryExpr& tern, TypePtr type) {
        auto hir = std::make_unique<HirTernary>();
        hir->condition = lower_expr(*tern.condition);
        hir->then_expr = lower_expr(*tern.then_expr);
        hir->else_expr = lower_expr(*tern.else_expr);
        return std::make_unique<HirExpr>(std::move(hir), type);
    }

    // match式 - ネストされた三項演算子に変換
    // match (x) { 1 => a, 2 => b, _ => c }
    // => (x == 1) ? a : ((x == 2) ? b : c)
    HirExprPtr lower_match(ast::MatchExpr& match, TypePtr type) {
        debug::hir::log(debug::hir::Id::LiteralLower, "Lowering match expression",
                        debug::Level::Debug);

        // scrutinee（マッチ対象）を一度評価して変数に格納
        auto scrutinee = lower_expr(*match.scrutinee);
        auto scrutinee_type = scrutinee->type;

        // アームがない場合、デフォルト値を返す
        if (match.arms.empty()) {
            auto lit = std::make_unique<HirLiteral>();
            lit->value = int64_t{0};
            return std::make_unique<HirExpr>(std::move(lit), type);
        }

        // ネストされた三項演算子を構築（逆順で）
        HirExprPtr result = nullptr;
        for (int i = static_cast<int>(match.arms.size()) - 1; i >= 0; i--) {
            auto& arm = match.arms[i];
            auto body = lower_expr(*arm.body);

            if (arm.pattern->kind == ast::MatchPatternKind::Wildcard) {
                // ワイルドカードは最後のデフォルトとして使用
                if (result == nullptr) {
                    result = std::move(body);
                } else {
                    // ワイルドカードの後に他のパターンがある場合はエラー
                    debug::hir::log(debug::hir::Id::Warning, "Wildcard pattern should be last",
                                    debug::Level::Warn);
                }
            } else {
                // 条件を構築: scrutinee == pattern_value
                auto cond = build_match_condition(scrutinee, scrutinee_type, arm);

                // ガード条件がある場合、条件に追加
                if (arm.guard) {
                    HirExprPtr guard_cond;

                    // 変数束縛パターンの場合、ガード式内の変数をscrutineeで置換
                    if (arm.pattern->kind == ast::MatchPatternKind::Variable &&
                        !arm.pattern->var_name.empty()) {
                        guard_cond = lower_guard_with_binding(*arm.guard, arm.pattern->var_name,
                                                              scrutinee, scrutinee_type);
                    } else {
                        guard_cond = lower_expr(*arm.guard);
                    }

                    auto combined = std::make_unique<HirBinary>();
                    combined->op = HirBinaryOp::And;
                    combined->lhs = std::move(cond);
                    combined->rhs = std::move(guard_cond);
                    cond = std::make_unique<HirExpr>(
                        std::move(combined), std::make_shared<ast::Type>(ast::TypeKind::Bool));
                }

                // 三項演算子を構築
                auto ternary = std::make_unique<HirTernary>();
                ternary->condition = std::move(cond);
                ternary->then_expr = std::move(body);

                if (result) {
                    ternary->else_expr = std::move(result);
                } else {
                    // 型に応じたデフォルト値
                    ternary->else_expr = make_default_value(type);
                }

                result = std::make_unique<HirExpr>(std::move(ternary), type);
            }
        }

        if (!result) {
            result = make_default_value(type);
        }

        return result;
    }

    // 型に応じたデフォルト値を生成
    HirExprPtr make_default_value(TypePtr type) {
        auto lit = std::make_unique<HirLiteral>();

        if (type && type->kind == ast::TypeKind::String) {
            lit->value = std::string("");
        } else if (type && type->kind == ast::TypeKind::Bool) {
            lit->value = false;
        } else if (type &&
                   (type->kind == ast::TypeKind::Float || type->kind == ast::TypeKind::Double)) {
            lit->value = 0.0;
        } else if (type && type->kind == ast::TypeKind::Char) {
            lit->value = '\0';
        } else {
            lit->value = int64_t{0};
        }

        return std::make_unique<HirExpr>(std::move(lit), type);
    }

    // matchパターンの条件式を構築
    HirExprPtr build_match_condition(const HirExprPtr& scrutinee, TypePtr /*scrutinee_type*/,
                                     const ast::MatchArm& arm) {
        // scrutineeをコピー（複製）
        HirExprPtr scrutinee_copy = clone_hir_expr(scrutinee);

        // パターンの種類に応じて条件を構築
        switch (arm.pattern->kind) {
            case ast::MatchPatternKind::Literal:
            case ast::MatchPatternKind::EnumVariant: {
                // scrutinee == pattern_value
                auto pattern_value = lower_expr(*arm.pattern->value);
                auto cond = std::make_unique<HirBinary>();
                cond->op = HirBinaryOp::Eq;
                cond->lhs = std::move(scrutinee_copy);
                cond->rhs = std::move(pattern_value);
                return std::make_unique<HirExpr>(std::move(cond),
                                                 std::make_shared<ast::Type>(ast::TypeKind::Bool));
            }

            case ast::MatchPatternKind::Variable:
                // 変数束縛は常にtrue（全てにマッチ）
                // TODO: 変数への束縛をサポート
                {
                    auto lit = std::make_unique<HirLiteral>();
                    lit->value = true;
                    return std::make_unique<HirExpr>(
                        std::move(lit), std::make_shared<ast::Type>(ast::TypeKind::Bool));
                }

            case ast::MatchPatternKind::Wildcard:
                // ワイルドカードは常にtrue
                {
                    auto lit = std::make_unique<HirLiteral>();
                    lit->value = true;
                    return std::make_unique<HirExpr>(
                        std::move(lit), std::make_shared<ast::Type>(ast::TypeKind::Bool));
                }
        }

        // フォールバック
        auto lit = std::make_unique<HirLiteral>();
        lit->value = false;
        return std::make_unique<HirExpr>(std::move(lit),
                                         std::make_shared<ast::Type>(ast::TypeKind::Bool));
    }

    // HIR式の簡易クローン
    HirExprPtr clone_hir_expr(const HirExprPtr& expr) {
        if (!expr)
            return nullptr;

        // VarRefの場合、同じ変数参照を作成
        if (auto* var = std::get_if<std::unique_ptr<HirVarRef>>(&expr->kind)) {
            auto clone = std::make_unique<HirVarRef>();
            clone->name = (*var)->name;
            return std::make_unique<HirExpr>(std::move(clone), expr->type, expr->span);
        }

        // Literalの場合、コピー
        if (auto* lit = std::get_if<std::unique_ptr<HirLiteral>>(&expr->kind)) {
            auto clone = std::make_unique<HirLiteral>();
            clone->value = (*lit)->value;
            return std::make_unique<HirExpr>(std::move(clone), expr->type, expr->span);
        }

        // Memberアクセスの場合、再帰的にクローン
        if (auto* member = std::get_if<std::unique_ptr<HirMember>>(&expr->kind)) {
            auto clone = std::make_unique<HirMember>();
            clone->object = clone_hir_expr((*member)->object);
            clone->member = (*member)->member;
            return std::make_unique<HirExpr>(std::move(clone), expr->type, expr->span);
        }

        // Binaryの場合、再帰的にクローン
        if (auto* binary = std::get_if<std::unique_ptr<HirBinary>>(&expr->kind)) {
            auto clone = std::make_unique<HirBinary>();
            clone->op = (*binary)->op;
            clone->lhs = clone_hir_expr((*binary)->lhs);
            clone->rhs = clone_hir_expr((*binary)->rhs);
            return std::make_unique<HirExpr>(std::move(clone), expr->type, expr->span);
        }

        // Unaryの場合、再帰的にクローン
        if (auto* unary = std::get_if<std::unique_ptr<HirUnary>>(&expr->kind)) {
            auto clone = std::make_unique<HirUnary>();
            clone->op = (*unary)->op;
            clone->operand = clone_hir_expr((*unary)->operand);
            return std::make_unique<HirExpr>(std::move(clone), expr->type, expr->span);
        }

        // Indexの場合、再帰的にクローン
        if (auto* index = std::get_if<std::unique_ptr<HirIndex>>(&expr->kind)) {
            auto clone = std::make_unique<HirIndex>();
            clone->object = clone_hir_expr((*index)->object);
            clone->index = clone_hir_expr((*index)->index);
            return std::make_unique<HirExpr>(std::move(clone), expr->type, expr->span);
        }

        // その他の式は変数参照に置き換え（一時変数を使う必要があるが、簡易実装）
        // TODO: より正確なクローン実装
        debug::hir::log(debug::hir::Id::Warning, "Complex expression cloning not fully supported",
                        debug::Level::Warn);

        // 一時的なリテラル（0）を返す
        auto clone = std::make_unique<HirLiteral>();
        clone->value = int64_t{0};
        return std::make_unique<HirExpr>(std::move(clone), expr->type, expr->span);
    }

    // ガード式内の変数束縛をscrutineeで置換してlower
    // 例: n if n > 0 の場合、ガード式 "n > 0" 内の "n" を scrutinee で置換
    HirExprPtr lower_guard_with_binding(ast::Expr& guard, const std::string& var_name,
                                        const HirExprPtr& scrutinee, TypePtr scrutinee_type) {
        // ガード式がIdentExprで、変数名が束縛変数と一致する場合
        if (auto* ident = guard.as<ast::IdentExpr>()) {
            if (ident->name == var_name) {
                // scrutineeのクローンを返す
                return clone_hir_expr(scrutinee);
            }
        }

        // BinaryExprの場合、左右両方を再帰的に処理
        if (auto* binary = guard.as<ast::BinaryExpr>()) {
            auto left =
                lower_guard_with_binding(*binary->left, var_name, scrutinee, scrutinee_type);
            auto right =
                lower_guard_with_binding(*binary->right, var_name, scrutinee, scrutinee_type);

            auto hir = std::make_unique<HirBinary>();
            hir->op = convert_binary_op(binary->op);
            hir->lhs = std::move(left);
            hir->rhs = std::move(right);

            // 結果の型を決定
            TypePtr result_type;
            if (is_comparison_op(binary->op)) {
                result_type = std::make_shared<ast::Type>(ast::TypeKind::Bool);
            } else {
                result_type = left ? left->type : scrutinee_type;
            }

            return std::make_unique<HirExpr>(std::move(hir), result_type);
        }

        // UnaryExprの場合
        if (auto* unary = guard.as<ast::UnaryExpr>()) {
            auto operand =
                lower_guard_with_binding(*unary->operand, var_name, scrutinee, scrutinee_type);

            auto hir = std::make_unique<HirUnary>();
            hir->op = convert_unary_op(unary->op);
            hir->operand = std::move(operand);

            TypePtr result_type;
            if (unary->op == ast::UnaryOp::Not) {
                result_type = std::make_shared<ast::Type>(ast::TypeKind::Bool);
            } else {
                result_type = hir->operand ? hir->operand->type : scrutinee_type;
            }

            return std::make_unique<HirExpr>(std::move(hir), result_type);
        }

        // その他の式はそのままlower
        return lower_expr(guard);
    }

    // 構造体リテラル
    HirExprPtr lower_struct_literal(ast::StructLiteralExpr& lit, TypePtr expected_type) {
        std::string type_name = lit.type_name;

        // 暗黙的構造体リテラル（型名が空）の場合、expected_typeから推論
        if (type_name.empty() && expected_type) {
            if (expected_type->kind == ast::TypeKind::Struct && !expected_type->name.empty()) {
                type_name = expected_type->name;
                debug::hir::log(debug::hir::Id::LiteralLower,
                                "Inferred struct type from context: " + type_name,
                                debug::Level::Debug);
            }
        }

        debug::hir::log(debug::hir::Id::LiteralLower, "Lowering struct literal: " + type_name,
                        debug::Level::Debug);

        auto hir_lit = std::make_unique<HirStructLiteral>();
        hir_lit->type_name = type_name;

        // 構造体の型を取得
        TypePtr struct_type = std::make_shared<ast::Type>(ast::TypeKind::Struct);
        struct_type->name = type_name;

        // 構造体定義を取得（フィールドの型推論に使用）
        const ast::StructDecl* struct_def = nullptr;
        if (!type_name.empty()) {
            auto struct_it = struct_defs_.find(type_name);
            if (struct_it != struct_defs_.end()) {
                struct_def = struct_it->second;
            }
        }

        // フィールドをlower
        for (auto& field : lit.fields) {
            HirStructLiteralField hir_field;
            hir_field.name = field.name;

            // フィールドの値をチェックして、暗黙的構造体リテラルならば型を推論
            if (struct_def) {
                for (auto& def_field : struct_def->fields) {
                    if (def_field.name == field.name) {
                        // ネストした暗黙的構造体リテラルに型を伝播
                        if (auto* nested_lit = field.value->as<ast::StructLiteralExpr>()) {
                            if (nested_lit->type_name.empty() && def_field.type &&
                                def_field.type->kind == ast::TypeKind::Struct) {
                                nested_lit->type_name = def_field.type->name;
                                debug::hir::log(
                                    debug::hir::Id::LiteralLower,
                                    "Propagated type to nested struct: " + def_field.type->name,
                                    debug::Level::Debug);
                            }
                        }
                        // ネストした配列リテラルの型推論
                        // TODO: 配列リテラル内の構造体も処理
                        break;
                    }
                }
            }

            hir_field.value = lower_expr(*field.value);
            hir_lit->fields.push_back(std::move(hir_field));
        }

        return std::make_unique<HirExpr>(std::move(hir_lit), struct_type);
    }

    // 配列リテラル
    HirExprPtr lower_array_literal(ast::ArrayLiteralExpr& lit, TypePtr expected_type) {
        debug::hir::log(
            debug::hir::Id::LiteralLower,
            "Lowering array literal with " + std::to_string(lit.elements.size()) + " elements",
            debug::Level::Debug);

        auto hir_lit = std::make_unique<HirArrayLiteral>();

        // 要素の期待される型を取得
        TypePtr expected_elem_type = nullptr;
        if (expected_type && expected_type->kind == ast::TypeKind::Array &&
            expected_type->element_type) {
            expected_elem_type = expected_type->element_type;
            debug::hir::log(debug::hir::Id::LiteralLower,
                            "Using expected element type: " + expected_elem_type->name,
                            debug::Level::Debug);
        }

        // 要素の型を推論
        TypePtr elem_type = expected_elem_type;
        for (auto& elem : lit.elements) {
            // 配列内の暗黙的構造体リテラルに型を伝播
            if (expected_elem_type && expected_elem_type->kind == ast::TypeKind::Struct) {
                if (auto* nested_lit = elem->as<ast::StructLiteralExpr>()) {
                    if (nested_lit->type_name.empty()) {
                        nested_lit->type_name = expected_elem_type->name;
                        debug::hir::log(
                            debug::hir::Id::LiteralLower,
                            "Propagated type to array element struct: " + expected_elem_type->name,
                            debug::Level::Debug);
                    }
                }
            }

            auto lowered_elem = lower_expr(*elem);
            if (!elem_type) {
                elem_type = lowered_elem->type;
            }
            hir_lit->elements.push_back(std::move(lowered_elem));
        }

        if (!elem_type) {
            elem_type = hir::make_int();  // デフォルト
        }

        // 配列型を作成
        TypePtr array_type = hir::make_array(elem_type, lit.elements.size());

        return std::make_unique<HirExpr>(std::move(hir_lit), array_type);
    }

    // 比較演算子かどうか
    bool is_comparison_op(ast::BinaryOp op) {
        return op == ast::BinaryOp::Eq || op == ast::BinaryOp::Ne || op == ast::BinaryOp::Lt ||
               op == ast::BinaryOp::Gt || op == ast::BinaryOp::Le || op == ast::BinaryOp::Ge;
    }
};

}  // namespace cm::hir
