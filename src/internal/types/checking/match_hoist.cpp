// ============================================================
// 式形式matchのscrutinee退避プリパス
//
// HIRは式形式matchを三項演算子チェーンへ脱糖し、scrutineeをアームごとにclone_hir_exprで複製する。関数呼び出しは複製すると
// 多重評価になるためクローン対象外で、従来はダミー値(0)へフォールバックし誤った結果を返していた。
// 本パスは型チェック前にASTを書き換え、呼び出しを含むscrutineeを文の直前の一時変数（auto推論）へ退避して単一評価を保証する。
//
// 同様に、組み込みResult/Optionメソッド（is_some/unwrap_or等）の脱糖もレシーバをclone_hir_exprで
// 複製するため、呼び出しを含むレシーバ（parse_int(s).unwrap_or(0)・map.get(k).is_none()等）を
// 同じ仕組みで退避する（退避しないとタグ比較とペイロード取得が別評価になり誤った値を返す）。
//
// 退避しない位置（評価回数・評価タイミングが変わるため）:
// - while/forの条件式・更新式（反復ごとに評価される）
// - &&/||の右辺、三項演算子の分岐、matchアームの式本体・ガード（短絡・条件評価）
// - defer本文の式、ラムダ本体からの持ち出し（遅延評価）
// ============================================================

#include "match_hoist.hpp"

#include "internal/syntax/ast/decl.hpp"
#include "internal/syntax/ast/expr.hpp"
#include "internal/syntax/ast/module.hpp"
#include "internal/syntax/ast/stmt.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cm {

namespace {

// 式の中に関数呼び出し（メソッド呼び出し・new・await含む）が含まれるか
bool contains_call(const ast::Expr& e) {
    if (e.as<ast::CallExpr>() || e.as<ast::NewExpr>() || e.as<ast::AwaitExpr>()) {
        return true;
    }
    if (const auto* mem = e.as<ast::MemberExpr>()) {
        if (mem->is_method_call) {
            return true;
        }
        return mem->object && contains_call(*mem->object);
    }
    if (const auto* bin = e.as<ast::BinaryExpr>()) {
        return (bin->left && contains_call(*bin->left)) ||
               (bin->right && contains_call(*bin->right));
    }
    if (const auto* un = e.as<ast::UnaryExpr>()) {
        return un->operand && contains_call(*un->operand);
    }
    if (const auto* idx = e.as<ast::IndexExpr>()) {
        return (idx->object && contains_call(*idx->object)) ||
               (idx->index && contains_call(*idx->index));
    }
    if (const auto* cast = e.as<ast::CastExpr>()) {
        return cast->operand && contains_call(*cast->operand);
    }
    if (const auto* tern = e.as<ast::TernaryExpr>()) {
        return (tern->condition && contains_call(*tern->condition)) ||
               (tern->then_expr && contains_call(*tern->then_expr)) ||
               (tern->else_expr && contains_call(*tern->else_expr));
    }
    if (const auto* mv = e.as<ast::MoveExpr>()) {
        return mv->operand && contains_call(*mv->operand);
    }
    if (const auto* m = e.as<ast::MatchExpr>()) {
        // ネストしたmatch自体も退避対象になりうるので呼び出し扱いにする
        (void)m;
        return true;
    }
    return false;
}

// 組み込みResult/Optionのメソッド名（HIRで脱糖されレシーバが複製されるもの）
bool is_builtin_sum_method(const std::string& name) {
    return name == "is_ok" || name == "is_err" || name == "is_some" || name == "is_none" ||
           name == "unwrap" || name == "unwrap_or" || name == "unwrap_err" || name == "expect";
}

class MatchHoister {
   public:
    void process_program(ast::Program& program) {
        for (auto& decl : program.declarations) {
            if (decl) {
                process_decl(*decl);
            }
        }
    }

   private:
    // 退避対象の式スロット（matchのscrutineeまたはenumメソッドのレシーバ）
    struct HoistSlot {
        ast::ExprPtr* slot;
        const char* prefix;
    };

    size_t counter_ = 0;

    void process_decl(ast::Decl& decl) {
        if (auto* func = decl.as<ast::FunctionDecl>()) {
            process_stmt_list(func->body);
        } else if (auto* impl = decl.as<ast::ImplDecl>()) {
            for (auto& method : impl->methods) {
                if (method) {
                    process_stmt_list(method->body);
                }
            }
            for (auto& ctor : impl->constructors) {
                if (ctor) {
                    process_stmt_list(ctor->body);
                }
            }
            if (impl->destructor) {
                process_stmt_list(impl->destructor->body);
            }
        } else if (auto* mod = decl.as<ast::ModuleDecl>()) {
            for (auto& inner : mod->declarations) {
                if (inner) {
                    process_decl(*inner);
                }
            }
        } else if (auto* init = decl.as<ast::InitialBlockDecl>()) {
            process_stmt_list(init->body);
        }
    }

    // 文リストを走査し、各文から収集したmatchのscrutinee退避letを直前に挿入する
    void process_stmt_list(std::vector<ast::StmtPtr>& stmts) {
        for (size_t i = 0; i < stmts.size(); ++i) {
            if (!stmts[i]) {
                continue;
            }
            std::vector<HoistSlot> hoists;
            collect_from_stmt(*stmts[i], hoists);
            for (auto& h : hoists) {
                std::string name = std::string(h.prefix) + std::to_string(counter_++);
                auto let = std::make_unique<ast::LetStmt>(name, nullptr, std::move(*h.slot));
                *h.slot = std::make_unique<ast::Expr>(std::make_unique<ast::IdentExpr>(name),
                                                      stmts[i]->span);
                stmts.insert(stmts.begin() + static_cast<std::ptrdiff_t>(i),
                             std::make_unique<ast::Stmt>(std::move(let), stmts[i]->span));
                ++i;  // 挿入した退避letの分だけ現在の文の位置を進める
            }
        }
    }

    // 1つの文から退避対象matchを収集し、内包する文リストを再帰処理する
    void collect_from_stmt(ast::Stmt& s, std::vector<HoistSlot>& out) {
        if (auto* let = s.as<ast::LetStmt>()) {
            if (let->init) {
                collect(*let->init, out);
            }
            for (auto& arg : let->ctor_args) {
                if (arg) {
                    collect(*arg, out);
                }
            }
        } else if (auto* es = s.as<ast::ExprStmt>()) {
            if (es->expr) {
                collect(*es->expr, out);
            }
        } else if (auto* ret = s.as<ast::ReturnStmt>()) {
            if (ret->value) {
                collect(*ret->value, out);
            }
        } else if (auto* iff = s.as<ast::IfStmt>()) {
            if (iff->condition) {
                collect(*iff->condition, out);
            }
            process_stmt_list(iff->then_block);
            process_stmt_list(iff->else_block);
        } else if (auto* wh = s.as<ast::WhileStmt>()) {
            // 条件式は反復ごとに評価されるため退避しない
            process_stmt_list(wh->body);
        } else if (auto* fo = s.as<ast::ForStmt>()) {
            // initは1回だけ実行されるのでfor文の直前へ退避できる。条件・更新式は反復評価のため対象外
            if (fo->init) {
                collect_from_stmt(*fo->init, out);
            }
            process_stmt_list(fo->body);
        } else if (auto* fi = s.as<ast::ForInStmt>()) {
            // イテレート対象はループ開始時に1回評価される
            if (fi->iterable) {
                collect(*fi->iterable, out);
            }
            process_stmt_list(fi->body);
        } else if (auto* blk = s.as<ast::BlockStmt>()) {
            process_stmt_list(blk->stmts);
        } else if (auto* sw = s.as<ast::SwitchStmt>()) {
            if (sw->expr) {
                collect(*sw->expr, out);
            }
            for (auto& c : sw->cases) {
                process_stmt_list(c.stmts);
            }
        } else if (auto* df = s.as<ast::DeferStmt>()) {
            // 遅延実行のため式の持ち出しはしないが、ブロック本文内の退避はブロック内で完結する
            if (df->body) {
                if (auto* body_blk = df->body->as<ast::BlockStmt>()) {
                    process_stmt_list(body_blk->stmts);
                }
            }
        } else if (auto* must = s.as<ast::MustBlockStmt>()) {
            process_stmt_list(must->body);
        }
    }

    // 文の実行時に無条件で1回評価される部分式から退避対象matchを収集する（短絡評価の右辺・条件分岐の枝には入らない）
    void collect(ast::Expr& e, std::vector<HoistSlot>& out) {
        if (auto* m = e.as<ast::MatchExpr>()) {
            // scrutinee内のネストしたmatchを先に退避する（挿入順=評価順を保つ）
            if (m->scrutinee) {
                collect(*m->scrutinee, out);
                if (contains_call(*m->scrutinee)) {
                    out.push_back({&m->scrutinee, "__match_scrutinee_expr_"});
                }
            }
            // アームの式本体・ガードは条件評価のため対象外。
            // ブロック形式アームの文リストはアーム内で完結するので再帰処理する
            for (auto& arm : m->arms) {
                if (arm.is_block_form) {
                    process_stmt_list(arm.block_body);
                }
            }
            return;
        }
        if (auto* bin = e.as<ast::BinaryExpr>()) {
            if (bin->left) {
                collect(*bin->left, out);
            }
            // &&/||の右辺は短絡評価のため退避しない
            if (bin->right && bin->op != ast::BinaryOp::And && bin->op != ast::BinaryOp::Or) {
                collect(*bin->right, out);
            }
            return;
        }
        if (auto* tern = e.as<ast::TernaryExpr>()) {
            // 分岐先は条件評価のため条件式のみ対象
            if (tern->condition) {
                collect(*tern->condition, out);
            }
            return;
        }
        if (auto* un = e.as<ast::UnaryExpr>()) {
            if (un->operand) {
                collect(*un->operand, out);
            }
            return;
        }
        if (auto* call = e.as<ast::CallExpr>()) {
            if (call->callee) {
                collect(*call->callee, out);
            }
            for (auto& arg : call->args) {
                if (arg) {
                    collect(*arg, out);
                }
            }
            return;
        }
        if (auto* mem = e.as<ast::MemberExpr>()) {
            if (mem->object) {
                collect(*mem->object, out);
                // 組み込みResult/Optionメソッドの呼び出しレシーバを退避する
                // （脱糖でレシーバが複製されるため、呼び出しを含むと多重評価で壊れる）
                if (mem->is_method_call && is_builtin_sum_method(mem->member) &&
                    contains_call(*mem->object)) {
                    out.push_back({&mem->object, "__enum_recv_expr_"});
                }
            }
            for (auto& arg : mem->args) {
                if (arg) {
                    collect(*arg, out);
                }
            }
            return;
        }
        if (auto* idx = e.as<ast::IndexExpr>()) {
            if (idx->object) {
                collect(*idx->object, out);
            }
            if (idx->index) {
                collect(*idx->index, out);
            }
            return;
        }
        if (auto* sl = e.as<ast::SliceExpr>()) {
            if (sl->object) {
                collect(*sl->object, out);
            }
            if (sl->start) {
                collect(*sl->start, out);
            }
            if (sl->end) {
                collect(*sl->end, out);
            }
            if (sl->step) {
                collect(*sl->step, out);
            }
            return;
        }
        if (auto* cast = e.as<ast::CastExpr>()) {
            if (cast->operand) {
                collect(*cast->operand, out);
            }
            return;
        }
        if (auto* mv = e.as<ast::MoveExpr>()) {
            if (mv->operand) {
                collect(*mv->operand, out);
            }
            return;
        }
        if (auto* aw = e.as<ast::AwaitExpr>()) {
            if (aw->operand) {
                collect(*aw->operand, out);
            }
            return;
        }
        if (auto* stl = e.as<ast::StructLiteralExpr>()) {
            for (auto& f : stl->fields) {
                if (f.value) {
                    collect(*f.value, out);
                }
            }
            return;
        }
        if (auto* arr = e.as<ast::ArrayLiteralExpr>()) {
            for (auto& el : arr->elements) {
                if (el) {
                    collect(*el, out);
                }
            }
            return;
        }
        if (auto* nw = e.as<ast::NewExpr>()) {
            for (auto& arg : nw->args) {
                if (arg) {
                    collect(*arg, out);
                }
            }
            return;
        }
        if (auto* lam = e.as<ast::LambdaExpr>()) {
            // ラムダ本体は遅延評価のため持ち出さないが、本体内の退避は本体内で完結する
            if (auto* body_stmts = std::get_if<std::vector<ast::StmtPtr>>(&lam->body)) {
                process_stmt_list(*body_stmts);
            }
            return;
        }
    }
};

}  // namespace

void hoist_match_call_scrutinees(ast::Program& program) {
    MatchHoister hoister;
    hoister.process_program(program);
}

}  // namespace cm
