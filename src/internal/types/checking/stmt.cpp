// ============================================================
// TypeChecker 実装 - 文のチェック
// ============================================================

#include "internal/base/i18n.hpp"
#include "internal/types/type_checker.hpp"

#include <functional>
#include <optional>
#include <string>

namespace cm {

// モノモーフ化されたenum型名からベース名を抽出
// 例: "Option__ulong" → "Option", "Result__int__string" → "Result"
static std::string get_enum_base_name(const std::string& name) {
    auto pos = name.find("__");
    if (pos != std::string::npos) {
        return name.substr(0, pos);
    }
    return name;
}

// 文列が「その先へフォールスルーしない」ことを構造的に判定する（H6）。
// for_function=trueは関数からの脱出（return/exit/無限ループ）のみを数え、
// falseは分岐からの脱出（break/continueを含む）も数える
bool cm_stmts_terminate(const std::vector<ast::StmtPtr>& stmts, bool for_function);

namespace {

// ループ本体に（ネストしたループへ降りずに）breakが含まれるか
bool contains_loop_break(const std::vector<ast::StmtPtr>& stmts) {
    for (const auto& s : stmts) {
        if (!s)
            continue;
        if (s->as<ast::BreakStmt>())
            return true;
        if (auto* ifs = s->as<ast::IfStmt>()) {
            if (contains_loop_break(ifs->then_block) || contains_loop_break(ifs->else_block))
                return true;
        } else if (auto* blk = s->as<ast::BlockStmt>()) {
            if (contains_loop_break(blk->stmts))
                return true;
        } else if (auto* sw = s->as<ast::SwitchStmt>()) {
            for (const auto& c : sw->cases) {
                if (contains_loop_break(c.stmts))
                    return true;
            }
        }
        // While/For/ForIn配下のbreakはそのループを抜けるだけなので降りない
    }
    return false;
}

// 単一文の終端判定（cm_stmts_terminateの下請け）
bool stmt_terminates(const ast::StmtPtr& s, bool for_function) {
    if (!s)
        return false;
    if (s->as<ast::ReturnStmt>())
        return true;
    if (!for_function && (s->as<ast::BreakStmt>() || s->as<ast::ContinueStmt>()))
        return true;
    if (auto* es = s->as<ast::ExprStmt>()) {
        // exit(code) はプロセスを終了する
        if (es->expr) {
            if (auto* call = es->expr->as<ast::CallExpr>()) {
                if (call->callee) {
                    if (auto* id = call->callee->as<ast::IdentExpr>()) {
                        if (id->name == "exit")
                            return true;
                    }
                }
            }
        }
        return false;
    }
    if (auto* ifs = s->as<ast::IfStmt>()) {
        return !ifs->else_block.empty() && cm_stmts_terminate(ifs->then_block, for_function) &&
               cm_stmts_terminate(ifs->else_block, for_function);
    }
    if (auto* blk = s->as<ast::BlockStmt>()) {
        return cm_stmts_terminate(blk->stmts, for_function);
    }
    if (auto* sw = s->as<ast::SwitchStmt>()) {
        // else/defaultケースを持ち、全ケースが終端する場合のみ
        bool has_default = false;
        for (const auto& c : sw->cases) {
            if (!c.pattern)
                has_default = true;
            if (!cm_stmts_terminate(c.stmts, for_function))
                return false;
        }
        return has_default;
    }
    if (auto* ws = s->as<ast::WhileStmt>()) {
        // while(true)でbreakを持たない無限ループは終端扱い（イベントループ等。H13と整合）
        if (ws->condition) {
            if (auto* lit = ws->condition->as<ast::LiteralExpr>()) {
                if (auto* b = std::get_if<bool>(&lit->value)) {
                    if (*b && !contains_loop_break(ws->body))
                        return true;
                }
            }
        }
        return false;
    }
    if (auto* mb = s->as<ast::MustBlockStmt>()) {
        return cm_stmts_terminate(mb->body, for_function);
    }
    return false;
}

}  // namespace

bool cm_stmts_terminate(const std::vector<ast::StmtPtr>& stmts, bool for_function) {
    for (const auto& s : stmts) {
        if (stmt_terminates(s, for_function))
            return true;  // 以降の文は到達不能
    }
    return false;
}

void TypeChecker::check_statement(ast::Stmt& stmt) {
    debug::tc::log(debug::tc::Id::CheckStmt, "", debug::Level::Trace);

    // エラー表示用に現在の文のSpanを保存
    current_span_ = stmt.span;

    if (auto* let = stmt.as<ast::LetStmt>()) {
        check_let(*let);
    } else if (auto* ret = stmt.as<ast::ReturnStmt>()) {
        check_return(*ret);
    } else if (auto* expr_stmt = stmt.as<ast::ExprStmt>()) {
        if (expr_stmt->expr) {
            auto expr_type = infer_type(*expr_stmt->expr);
            // must_use検査: Result型の値を使わずに捨てる文は警告する（Rustのunused_must_use相当。エラーの取りこぼしを静的に検出する）
            // matchは文として使われるため対象外
            if (expr_type && expr_type->kind == ast::TypeKind::Struct &&
                !expr_stmt->expr->as<ast::MatchExpr>()) {
                std::string base = expr_type->name;
                auto lt = base.find('<');
                if (lt != std::string::npos) {
                    base = base.substr(0, lt);
                }
                if (base == "Result") {
                    // 式ノードのSpanが未設定（start=0）の場合は直近の推論位置を使う
                    Span warn_span = expr_stmt->expr->span;
                    if (warn_span.start == 0) {
                        warn_span = current_span_;
                    }
                    warning(warn_span, i18n::msg(i18n::MsgId::TypeUnusedResultValueTheError));
                }
            }
        }
    } else if (auto* if_stmt = stmt.as<ast::IfStmt>()) {
        check_if(*if_stmt);
    } else if (auto* while_stmt = stmt.as<ast::WhileStmt>()) {
        check_while(*while_stmt);
    } else if (auto* for_stmt = stmt.as<ast::ForStmt>()) {
        check_for(*for_stmt);
    } else if (auto* for_in = stmt.as<ast::ForInStmt>()) {
        check_for_in(*for_in);
    } else if (auto* block = stmt.as<ast::BlockStmt>()) {
        scopes_.push();
        for (auto& s : block->stmts) {
            check_statement(*s);
        }
        scopes_.pop();
    } else if (auto* switch_stmt = stmt.as<ast::SwitchStmt>()) {
        // switch本体の検査（従来は未走査で、case内の型エラーや変数の使用・変更がLint追跡から漏れていた）
        if (switch_stmt->expr) {
            infer_type(*switch_stmt->expr);
        }
        // caseパターン値（単一値・範囲・ORパターン）にも型を注釈する（typed-hir-single-source 第2段）
        std::function<void(ast::Pattern&)> infer_pattern = [&](ast::Pattern& p) {
            if (p.value) {
                infer_type(*p.value);
            }
            if (p.range_start) {
                infer_type(*p.range_start);
            }
            if (p.range_end) {
                infer_type(*p.range_end);
            }
            for (auto& op : p.or_patterns) {
                if (op) {
                    infer_pattern(*op);
                }
            }
        };
        for (auto& c : switch_stmt->cases) {
            if (c.pattern) {
                infer_pattern(*c.pattern);
            }
            scopes_.push();
            for (auto& s : c.stmts) {
                check_statement(*s);
            }
            scopes_.pop();
        }
    } else if (auto* defer_stmt = stmt.as<ast::DeferStmt>()) {
        if (defer_stmt->body) {
            check_statement(*defer_stmt->body);
        }
    } else if (auto* must_block = stmt.as<ast::MustBlockStmt>()) {
        scopes_.push();
        for (auto& s : must_block->body) {
            check_statement(*s);
        }
        scopes_.pop();
    }
}

// M3段階3: 非constポインタ型の格納先へ、const基点の&式を束縛する場合に警告する
// （const int* p = &c は正当なため、要素constのポインタ格納先は対象外）
void TypeChecker::warn_addr_of_const_into_mutable_ptr(const ast::TypePtr& dest_type,
                                                      const ast::Expr* init) {
    if (!enable_lint_warnings_ || !dest_type || !init) {
        return;
    }
    auto resolved = resolve_typedef(dest_type);
    if (!resolved || resolved->kind != ast::TypeKind::Pointer) {
        return;
    }
    if (resolved->element_type && resolved->element_type->qualifiers.is_const) {
        return;
    }
    auto* unary = init->as<ast::UnaryExpr>();
    if (!unary || unary->op != ast::UnaryOp::AddrOf || !unary->operand) {
        return;
    }
    const ast::Expr* base = unary->operand.get();
    while (base) {
        if (auto* idx = base->as<ast::IndexExpr>()) {
            base = idx->object.get();
        } else if (auto* mem = base->as<ast::MemberExpr>()) {
            base = mem->object.get();
        } else {
            break;
        }
    }
    if (!base) {
        return;
    }
    auto* base_ident = base->as<ast::IdentExpr>();
    if (!base_ident) {
        return;
    }
    auto base_sym = scopes_.current().lookup(base_ident->name);
    if (base_sym && base_sym->is_const) {
        Span warn_span = unary->operand->span;
        if (warn_span.start == 0) {
            warn_span = current_span_;
        }
        warning(warn_span, i18n::msgf(i18n::MsgId::TypeAddrOfConst, base_ident->name));
    }
}

void TypeChecker::check_let(ast::LetStmt& let) {
    // エラー表示用に文のSpanを保存
    Span stmt_span = current_span_;

    // ジェネリック型引数の個数を検証する（H15。ローカル変数宣言はis_valid_typeを通らないためここで検査）
    if (let.type) {
        auto rt = resolve_typedef(let.type);
        const auto& ct = rt ? rt : let.type;
        auto sd_it = struct_defs_.find(ct->name);
        if (sd_it != struct_defs_.end() && sd_it->second && !ct->type_args.empty() &&
            ct->type_args.size() != sd_it->second->generic_params.size()) {
            error(current_span_, i18n::msgf(i18n::MsgId::TypeGenericArgumentCountMismatch, ct->name,
                                            std::to_string(sd_it->second->generic_params.size()),
                                            std::to_string(ct->type_args.size())));
        }
        // H15: ジェネリック型を型引数なしで宣言に使うのはエラー（Pair p; 等。推論の材料が無い）
        else if (sd_it != struct_defs_.end() && sd_it->second &&
                 !sd_it->second->generic_params.empty() && ct->type_args.empty() &&
                 ct->name.find("__") == std::string::npos) {
            error(current_span_, i18n::msgf(i18n::MsgId::TypeGenericTypeRequiresArguments, ct->name,
                                            std::to_string(sd_it->second->generic_params.size())));
        }
        // H15: 各型引数の存在を検証する（Pair<int, Nope> の Nope 等を検出）
        if (sd_it != struct_defs_.end() && sd_it->second) {
            for (const auto& arg : ct->type_args) {
                if (arg && !is_valid_type(arg)) {
                    error(current_span_, i18n::msgf(i18n::MsgId::TypeUnknownTypeArgument,
                                                    ast::type_to_string(*arg), ct->name));
                }
            }
        }
    }

    // const変数の値を評価（配列サイズ等で使用）
    std::optional<int64_t> const_int_value = std::nullopt;
    if (let.is_const && let.init) {
        const_int_value = evaluate_const_expr(*let.init);
        if (const_int_value) {
            debug::tc::log(
                debug::tc::Id::TypeInfer,
                "Evaluated const: " + let.name + " = " + std::to_string(*const_int_value),
                debug::Level::Debug);
        }
    }

    ast::TypePtr init_type;
    if (let.init) {
        if (auto* array_lit = let.init->as<ast::ArrayLiteralExpr>()) {
            if (let.type && let.type->kind == ast::TypeKind::Array) {
                // 宣言型を採用しつつ、要素（ネストした無名リテラル含む）へ要素型を期待型として伝播する
                init_type = let.type;
                let.init->type = let.type;
                for (auto& elem : array_lit->elements) {
                    infer_type_expecting(*elem, let.type->element_type);
                }
            } else {
                init_type = infer_type(*let.init);
            }
        } else {
            // 宣言型があれば期待型として渡す（無名構造体リテラルの型名補完はinfer_type_expectingへ一元化）
            init_type = infer_type_expecting(*let.init, let.type);
        }
    }

    // キャプチャ付きクロージャ変数の追跡（V5〜V7の診断用。infer後はlambda.capturesが確定している）
    if (let.init) {
        if (const auto* lam = let.init->as<ast::LambdaExpr>()) {
            if (!lam->captures.empty()) {
                closure_vars_.insert(let.name);
            } else {
                closure_vars_.erase(let.name);
            }
        } else if (is_capturing_closure_expr(*let.init)) {
            // クロージャ変数のコピーもクロージャとして追跡する
            closure_vars_.insert(let.name);
        } else {
            closure_vars_.erase(let.name);
        }
    }

    if (let.has_ctor_call && let.type) {
        std::string type_name = ast::type_to_string(*let.type);
        std::string ctor_name = type_name + "__ctor";
        if (!let.ctor_args.empty()) {
            ctor_name += "_" + std::to_string(let.ctor_args.size());
        }

        for (auto& arg : let.ctor_args) {
            infer_type(*arg);
        }

        debug::tc::log(debug::tc::Id::Resolved, "Constructor call: " + ctor_name,
                       debug::Level::Debug);
    }

    // auto型（Inferred）の場合は初期化式から型を推論
    if (let.type && let.type->kind == ast::TypeKind::Inferred) {
        if (init_type) {
            let.type = init_type;
            scopes_.current().define(let.name, init_type, let.is_const, let.is_static, stmt_span,
                                     const_int_value);
            debug::tc::log(debug::tc::Id::TypeInfer,
                           "auto " + let.name + " : " + ast::type_to_string(*init_type),
                           debug::Level::Trace);
        } else {
            error(stmt_span, i18n::msgf(i18n::MsgId::TcCannotInferTypeAutoVariable, let.name));
        }
    } else if (let.type) {
        auto resolved_type = resolve_typedef(let.type);
        // 配列型のsize_param_nameを解決（const強化）
        resolve_array_size(resolved_type);

        // ポインタ型の場合、宣言時のconst情報を保持（借用システム Phase 2）
        // const int* p = &x の場合、let.type->element_type->qualifiers.is_const = true
        if (resolved_type->kind == ast::TypeKind::Pointer &&
            let.type->kind == ast::TypeKind::Pointer && let.type->element_type &&
            let.type->element_type->qualifiers.is_const) {
            if (resolved_type->element_type) {
                resolved_type->element_type->qualifiers.is_const = true;
            }
        }
        // 借用システム Phase 2: const変数がポインタ型の場合、element_typeにconstを適用
        // Cmでは "const int* p" は "const (int*) p" としてパースされるため、ポインタのelement_typeにconst修飾を伝播する
        if (let.is_const && resolved_type->kind == ast::TypeKind::Pointer &&
            resolved_type->element_type) {
            resolved_type->element_type->qualifiers.is_const = true;
        }
        if (init_type && !types_compatible(resolved_type, init_type)) {
            // ジェネリクスenum variant型推論: Option<ulong> x = Option::None のようなケース
            // init_typeがintだが、init式がenum variant識別子の場合、宣言型に強制する
            bool is_enum_variant_coercion = false;
            if (let.init) {
                if (auto* ident = let.init->as<ast::IdentExpr>()) {
                    // Option::None, Result::Err 等のenum variant名かチェック
                    auto sep = ident->name.find("::");
                    if (sep != std::string::npos) {
                        std::string enum_name = ident->name.substr(0, sep);
                        // 宣言型の名前がenum名と一致するか
                        // モノモーフ化された型名（Option__ulong等）にも対応
                        if ((resolved_type->name == enum_name ||
                             get_enum_base_name(resolved_type->name) == enum_name) &&
                            enum_values_.count(ident->name) > 0) {
                            is_enum_variant_coercion = true;
                            // init式の型を宣言型に強制
                            let.init->type = resolved_type;
                        }
                    }
                }
            }
            // ジェネリック構造体リテラルの期待型推論: Pair<int, string> p = Pair{...} / {...}
            // リテラルの裸名（Pair）は特殊化を持たないため、宣言型が同基底の特殊化なら宣言型を採用する
            bool is_generic_literal_coercion = false;
            if (let.init && !resolved_type->type_args.empty()) {
                if (auto* slit = let.init->as<ast::StructLiteralExpr>()) {
                    if (slit->type_name.empty() || slit->type_name == resolved_type->name) {
                        is_generic_literal_coercion = true;
                        slit->type_name = resolved_type->name;
                        let.init->type = resolved_type;
                    }
                }
            }
            if (!is_enum_variant_coercion && !is_generic_literal_coercion) {
                error(stmt_span, i18n::msgf(i18n::MsgId::TcTypeMismatchVariableDeclarationExpected,
                                            let.name, ast::type_to_string(*resolved_type),
                                            ast::type_to_string(*init_type)));
            }
        }
        // リテラル型チェック（typedef HttpMethod = "GET" | "POST" など）
        if (let.init) {
            check_literal_assignment(resolved_type, let.init.get(), stmt_span);
        }
        let.type = resolved_type;
        scopes_.current().define(let.name, resolved_type, let.is_const, let.is_static, stmt_span,
                                 const_int_value);
    } else if (init_type) {
        let.type = init_type;
        scopes_.current().define(let.name, init_type, let.is_const, let.is_static, stmt_span,
                                 const_int_value);
        debug::tc::log(debug::tc::Id::TypeInfer, let.name + " : " + ast::type_to_string(*init_type),
                       debug::Level::Trace);
    } else {
        error(stmt_span, i18n::msgf(i18n::MsgId::TcCannotInferType, let.name));
    }

    // 非const変数を追跡（const推奨警告用）。
    // ポインタ型は const T* がpointee-constを意味し、束縛のconst化を表せないため対象外
    {
        auto tracked_type = resolve_typedef(let.type ? let.type : init_type);
        bool is_pointer = tracked_type && tracked_type->kind == ast::TypeKind::Pointer;
        // 初期化子（またはコンストラクタ呼び出し）のない宣言はconst化できないため対象外
        bool can_be_const = let.init != nullptr || let.has_ctor_call;
        // _ 始まりの名前（意図的な未使用・コンパイラ生成の一時変数）は対象外
        bool is_internal_name = !let.name.empty() && let.name[0] == '_';
        // 補間プレースホルダを含む文字列リテラル初期化子は対象外（const stringは補間が実行されない既知の非互換があるため）
        bool has_interp_literal = false;
        if (let.init) {
            if (auto* lit = let.init->as<ast::LiteralExpr>()) {
                if (lit->is_string()) {
                    const auto& sval = std::get<std::string>(lit->value);
                    for (size_t bi = 0; bi + 1 < sval.size(); ++bi) {
                        if (sval[bi] == '{' && sval[bi + 1] != '{') {
                            has_interp_literal = true;
                            break;
                        }
                    }
                }
            }
        }
        if (!let.is_const && !is_pointer && can_be_const && !is_internal_name &&
            !has_interp_literal) {
            non_const_variable_spans_[let.name] = stmt_span;
        }
    }

    // 初期化式がある場合は初期化済みとしてマーク
    if (let.init) {
        mark_variable_initialized(let.name);
        // M3段階3: int* q = &const_値 の束縛を警告（const int*は対象外）
        warn_addr_of_const_into_mutable_ptr(let.type, let.init.get());
    }
    // コンストラクタ呼び出し宣言はデストラクタ等の副作用のために存在し得るため、初期化済みかつ使用済みとして扱う（RAIIパターンのW001誤検出防止）
    if (let.has_ctor_call) {
        mark_variable_initialized(let.name);
        scopes_.current().mark_used(let.name);
    }
    if (!let.init && let.type) {
        // 構造体型は既定構築、固定長配列はH4のゼロ初期化、スライスは暗黙の空スライス構築が
        // ランタイム保証されるため、未初期化使用の警告対象はスカラに限定する
        // （utiny[4096] buf; の書き込みバッファや int[] s; return s; が偽陽性になっていた）
        auto resolved = resolve_typedef(let.type);
        if (resolved &&
            (resolved->kind == ast::TypeKind::Struct || resolved->kind == ast::TypeKind::Array)) {
            mark_variable_initialized(let.name);
        }
    }

    // 命名規則チェックは check_naming_conventions（L001 --strict）へ一本化
}

void TypeChecker::check_return(ast::ReturnStmt& ret) {
    Span stmt_span = current_span_;
    if (!current_return_type_)
        return;

    if (ret.value) {
        // 戻り値型を期待型として渡す（return {..}; / return [..]; の無名リテラルを許容）
        auto val_type = infer_type_expecting(*ret.value, current_return_type_);
        if (!types_compatible(current_return_type_, val_type)) {
            // ジェネリクスenum variant型推論: return Option::None のようなケース
            bool is_enum_variant_coercion = false;
            if (auto* ident = ret.value->as<ast::IdentExpr>()) {
                auto sep = ident->name.find("::");
                if (sep != std::string::npos) {
                    std::string enum_name = ident->name.substr(0, sep);
                    // モノモーフ化された型名（Option__ulong等）にも対応
                    if (current_return_type_ &&
                        (current_return_type_->name == enum_name ||
                         get_enum_base_name(current_return_type_->name) == enum_name) &&
                        enum_values_.count(ident->name) > 0) {
                        is_enum_variant_coercion = true;
                        ret.value->type = current_return_type_;
                    }
                }
            }
            if (!is_enum_variant_coercion) {
                error(stmt_span,
                      i18n::msgf(i18n::MsgId::TcReturnTypeMismatchExpected,
                                 ast::type_to_string(*current_return_type_),
                                 (val_type ? ast::type_to_string(*val_type) : "unknown")));
            }
        }

        // ライフタイムチェック: ローカル変数への参照を返すことを禁止
        // return &x の場合、xがローカル変数ならダングリングポインタになる
        // ただし、static変数はプログラム全体のライフタイムを持つので許可
        if (val_type && val_type->kind == ast::TypeKind::Pointer) {
            if (auto* unary = ret.value->as<ast::UnaryExpr>()) {
                if (unary->op == ast::UnaryOp::AddrOf) {
                    if (auto* ident = unary->operand->as<ast::IdentExpr>()) {
                        // 変数のスコープレベルを確認
                        int var_level = scopes_.current().get_scope_level(ident->name);
                        // static変数かどうか確認
                        auto sym = scopes_.current().lookup(ident->name);
                        bool is_static = sym && sym->is_static;
                        // レベル1以上はローカル変数（0=グローバル）、ただしstaticは除外
                        if (var_level >= 1 && !is_static) {
                            error(stmt_span,
                                  i18n::msgf(i18n::MsgId::TcCannotReturnReferenceLocalVariable,
                                             ident->name));
                        }
                    }
                }
            }
        }
    } else if (current_return_type_->kind != ast::TypeKind::Void) {
        error(stmt_span, i18n::msgf(i18n::MsgId::TcMissingReturnValueExpected,
                                    ast::type_to_string(*current_return_type_)));
    }
}

void TypeChecker::check_if(ast::IfStmt& if_stmt) {
    Span stmt_span = current_span_;
    auto cond_type = infer_type(*if_stmt.condition);
    if (cond_type && cond_type->kind != ast::TypeKind::Bool) {
        error(stmt_span,
              i18n::msgf(i18n::MsgId::TcIfConditionMustBool, ast::type_to_string(*cond_type)));
    }

    // H6: 確定代入のfork/join。分岐内の初期化は「生き残る全経路で初期化」された場合のみ
    // 合流後へ伝える（従来はフラット集合で、片側分岐だけの初期化が合流後も初期化済みと誤認された）
    auto before_init = initialized_variables_;

    scopes_.push();
    for (auto& s : if_stmt.then_block) {
        check_statement(*s);
    }
    scopes_.pop();
    auto then_init = initialized_variables_;
    bool then_terminates = cm_stmts_terminate(if_stmt.then_block, false);
    initialized_variables_ = before_init;

    bool else_terminates = false;
    auto else_init = before_init;
    if (!if_stmt.else_block.empty()) {
        scopes_.push();
        for (auto& s : if_stmt.else_block) {
            check_statement(*s);
        }
        scopes_.pop();
        else_init = initialized_variables_;
        else_terminates = cm_stmts_terminate(if_stmt.else_block, false);
        initialized_variables_ = before_init;
    }

    // 合流: return等で終端した分岐は合流に参加しない
    if (then_terminates && else_terminates) {
        initialized_variables_ = before_init;
    } else if (then_terminates) {
        initialized_variables_ = else_init;
    } else if (else_terminates) {
        initialized_variables_ = then_init;
    } else {
        initialized_variables_.clear();
        for (const auto& name : then_init) {
            if (else_init.count(name) > 0) {
                initialized_variables_.insert(name);
            }
        }
    }
}

void TypeChecker::check_while(ast::WhileStmt& while_stmt) {
    Span stmt_span = current_span_;
    auto cond_type = infer_type(*while_stmt.condition);
    if (cond_type && cond_type->kind != ast::TypeKind::Bool) {
        error(stmt_span,
              i18n::msgf(i18n::MsgId::TcWhileConditionMustBool, ast::type_to_string(*cond_type)));
    }

    scopes_.push();
    for (auto& s : while_stmt.body) {
        check_statement(*s);
    }
    scopes_.pop();
}

void TypeChecker::check_for(ast::ForStmt& for_stmt) {
    Span stmt_span = current_span_;
    scopes_.push();

    if (for_stmt.init) {
        check_statement(*for_stmt.init);
    }
    if (for_stmt.condition) {
        auto cond_type = infer_type(*for_stmt.condition);
        if (cond_type && cond_type->kind != ast::TypeKind::Bool) {
            error(stmt_span,
                  i18n::msgf(i18n::MsgId::TcConditionMustBool, ast::type_to_string(*cond_type)));
        }
    }
    if (for_stmt.update) {
        infer_type(*for_stmt.update);
    }

    for (auto& s : for_stmt.body) {
        check_statement(*s);
    }

    scopes_.pop();
}

void TypeChecker::check_for_in(ast::ForInStmt& for_in) {
    Span stmt_span = current_span_;
    scopes_.push();

    auto iterable_type = infer_type(*for_in.iterable);
    if (!iterable_type) {
        error(stmt_span, i18n::msg(i18n::MsgId::TcCannotInferTypeIterableExpression));
        scopes_.pop();
        return;
    }

    ast::TypePtr element_type;

    // Struct型の場合: iter()メソッドがあるか確認
    if (iterable_type->kind == ast::TypeKind::Struct) {
        std::string type_name = ast::type_to_string(*iterable_type);

        // iter()メソッドを検索
        auto it = type_methods_.find(type_name);
        if (it != type_methods_.end()) {
            auto method_it = it->second.find("iter");
            if (method_it != it->second.end()) {
                // iter()メソッドが存在 → イテレータベースで展開
                for_in.use_iterator = true;

                // イテレータの戻り値型を取得
                if (method_it->second.return_type) {
                    for_in.iterator_type_name = ast::type_to_string(*method_it->second.return_type);

                    // イテレータのnext()メソッドから要素型を推定
                    auto iter_it = type_methods_.find(for_in.iterator_type_name);
                    if (iter_it != type_methods_.end()) {
                        auto next_it = iter_it->second.find("next");
                        if (next_it != iter_it->second.end() && next_it->second.return_type) {
                            element_type = next_it->second.return_type;
                        }
                    }
                }

                debug::tc::log(debug::tc::Id::TypeInfer,
                               "for-in: using iterator pattern for " + type_name +
                                   " (iterator: " + for_in.iterator_type_name + ")",
                               debug::Level::Debug);
            }
        }

        // iter()メソッドがない場合はエラー
        if (!for_in.use_iterator) {
            error(stmt_span, i18n::msgf(i18n::MsgId::TcRequiresIterableTypeArrayType,
                                        ast::type_to_string(*iterable_type)));
            scopes_.pop();
            return;
        }
    } else if (iterable_type->kind == ast::TypeKind::Array) {
        // 配列型: 従来のインデックスベース展開
        element_type = iterable_type->element_type;
    } else {
        error(stmt_span, i18n::msgf(i18n::MsgId::TcRequiresIterableTypeArray,
                                    ast::type_to_string(*iterable_type)));
        scopes_.pop();
        return;
    }

    if (for_in.var_type) {
        auto resolved_type = resolve_typedef(for_in.var_type);
        // auto（Inferred型）の場合は要素型を使用
        if (resolved_type->kind == ast::TypeKind::Inferred) {
            for_in.var_type = element_type;
        } else if (!types_compatible(resolved_type, element_type)) {
            error(stmt_span, i18n::msgf(i18n::MsgId::TcVariableTypeMismatchExpected,
                                        ast::type_to_string(*element_type),
                                        ast::type_to_string(*resolved_type)));
        } else {
            for_in.var_type = resolved_type;
        }
    } else {
        for_in.var_type = element_type;
    }

    scopes_.current().define(for_in.var_name, for_in.var_type, false);
    // ループ変数はイテレーションで毎回代入されるため初期化済みとして扱う
    mark_variable_initialized(for_in.var_name);

    for (auto& s : for_in.body) {
        check_statement(*s);
    }

    scopes_.pop();
}

}  // namespace cm
