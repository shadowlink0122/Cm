#pragma once

#include "../common/debug/hir.hpp"
#include "../frontend/ast/decl.hpp"
#include "hir_nodes.hpp"

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

        for (auto& decl : program.declarations) {
            if (auto hir_decl = lower_decl(*decl)) {
                hir.declarations.push_back(std::move(hir_decl));
            }
        }

        debug::hir::log(debug::hir::Id::LowerEnd,
                        std::to_string(hir.declarations.size()) + " declarations");
        return hir;
    }

   private:
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
        }
        return nullptr;
    }

    // 関数
    HirDeclPtr lower_function(ast::FunctionDecl& func) {
        debug::hir::log(debug::hir::Id::FunctionNode, "function " + func.name, debug::Level::Debug);
        debug::hir::log(debug::hir::Id::FunctionName, func.name, debug::Level::Trace);

        auto hir_func = std::make_unique<HirFunction>();
        hir_func->name = func.name;
        hir_func->return_type = func.return_type;
        hir_func->is_export = func.visibility == ast::Visibility::Export;

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

        for (const auto& field : st.fields) {
            hir_st->fields.push_back({field.name, field.type});
            debug::hir::log(
                debug::hir::Id::StructField,
                field.name + " : " + (field.type ? type_to_string(*field.type) : "auto"),
                debug::Level::Trace);
        }

        return std::make_unique<HirDecl>(std::move(hir_st));
    }

    // インターフェース
    HirDeclPtr lower_interface(ast::InterfaceDecl& iface) {
        debug::hir::log(debug::hir::Id::NodeCreate, "interface " + iface.name, debug::Level::Trace);

        auto hir_iface = std::make_unique<HirInterface>();
        hir_iface->name = iface.name;
        hir_iface->is_export = iface.visibility == ast::Visibility::Export;

        for (const auto& method : iface.methods) {
            HirMethodSig sig;
            sig.name = method.name;
            sig.return_type = method.return_type;
            for (const auto& param : method.params) {
                sig.params.push_back({param.name, param.type});
            }
            hir_iface->methods.push_back(std::move(sig));
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

        for (auto& method : impl.methods) {
            auto hir_func = std::make_unique<HirFunction>();
            hir_func->name = method->name;
            hir_func->return_type = method->return_type;

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
        }
        return nullptr;
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

        auto hir_let = std::make_unique<HirLet>();
        hir_let->name = let.name;
        hir_let->type = let.type;
        hir_let->is_const = let.is_const;

        if (let.type) {
            debug::hir::log(debug::hir::Id::LetType, type_to_string(*let.type),
                            debug::Level::Trace);
        }

        if (let.init) {
            debug::hir::log(debug::hir::Id::LetInit, "initializer present", debug::Level::Trace);
            hir_let->init = lower_expr(*let.init);
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
            debug::hir::log(debug::hir::Id::IdentifierRef, "variable: " + ident->name,
                            debug::Level::Trace);
            auto var_ref = std::make_unique<HirVarRef>();
            var_ref->name = ident->name;
            return std::make_unique<HirExpr>(std::move(var_ref), type);
        } else if (auto* binary = expr.as<ast::BinaryExpr>()) {
            return lower_binary(*binary, type);
        } else if (auto* unary = expr.as<ast::UnaryExpr>()) {
            return lower_unary(*unary, type);
        } else if (auto* call = expr.as<ast::CallExpr>()) {
            return lower_call(*call, type);
        } else if (auto* idx = expr.as<ast::IndexExpr>()) {
            return lower_index(*idx, type);
        } else if (auto* mem = expr.as<ast::MemberExpr>()) {
            return lower_member(*mem, type);
        } else if (auto* tern = expr.as<ast::TernaryExpr>()) {
            return lower_ternary(*tern, type);
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
        if (auto* ident = call.callee->as<ast::IdentExpr>()) {
            hir->func_name = ident->name;
            debug::hir::log(debug::hir::Id::CallTarget, "function: " + ident->name,
                            debug::Level::Trace);
        } else {
            hir->func_name = "<indirect>";
            debug::hir::log(debug::hir::Id::CallTarget, "indirect call", debug::Level::Trace);
        }

        debug::hir::log(debug::hir::Id::CallArgs, "count=" + std::to_string(call.args.size()),
                        debug::Level::Trace);
        for (size_t i = 0; i < call.args.size(); i++) {
            debug::hir::log(debug::hir::Id::CallArgEval, "arg[" + std::to_string(i) + "]",
                            debug::Level::Trace);
            hir->args.push_back(lower_expr(*call.args[i]));
        }

        return std::make_unique<HirExpr>(std::move(hir), type);
    }

    // 配列アクセス
    HirExprPtr lower_index(ast::IndexExpr& idx, TypePtr type) {
        debug::hir::log(debug::hir::Id::IndexLower, "", debug::Level::Debug);
        auto hir = std::make_unique<HirIndex>();

        debug::hir::log(debug::hir::Id::IndexBase, "Evaluating base", debug::Level::Trace);
        hir->object = lower_expr(*idx.object);
        debug::hir::log(debug::hir::Id::IndexValue, "Evaluating index", debug::Level::Trace);
        hir->index = lower_expr(*idx.index);
        return std::make_unique<HirExpr>(std::move(hir), type);
    }

    // メンバアクセス
    HirExprPtr lower_member(ast::MemberExpr& mem, TypePtr type) {
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
};

}  // namespace cm::hir
