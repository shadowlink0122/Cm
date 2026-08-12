#include "type_audit.hpp"

#include <cstdio>
#include <functional>

namespace cm::hir {

namespace {

// HirExprKindのvariant indexからノード種別名を返す
const char* kind_name(const HirExprKind& kind) {
    static const char* names[] = {"Literal",       "VarRef",       "Binary", "Unary",
                                  "Call",          "Index",        "Member", "Ternary",
                                  "StructLiteral", "ArrayLiteral", "Lambda", "Cast",
                                  "EnumConstruct", "EnumPayload"};
    const size_t idx = kind.index();
    return idx < sizeof(names) / sizeof(names[0]) ? names[idx] : "Unknown";
}

struct Walker {
    TypeAuditResult& result;
    std::string current_function;

    void note_violation(const HirExpr& e, bool is_null) {
        if (is_null) {
            ++result.null_types;
        } else {
            ++result.error_types;
        }
        const std::string kind = kind_name(e.kind);
        ++result.violations_by_kind[kind];
        if (result.samples.size() < 50) {
            std::string detail;
            if (auto* vr = std::get_if<std::unique_ptr<HirVarRef>>(&e.kind); vr && *vr) {
                detail = " name=" + (*vr)->name;
            } else if (auto* lit = std::get_if<std::unique_ptr<HirLiteral>>(&e.kind); lit && *lit) {
                if (auto* sv = std::get_if<std::string>(&(*lit)->value)) {
                    detail = " str=\"" + sv->substr(0, 24) + "\"";
                } else if (auto* iv = std::get_if<int64_t>(&(*lit)->value)) {
                    detail = " int=" + std::to_string(*iv);
                }
            } else if (auto* call = std::get_if<std::unique_ptr<HirCall>>(&e.kind); call && *call) {
                detail = " func=" + (*call)->func_name;
            } else if (auto* mem = std::get_if<std::unique_ptr<HirMember>>(&e.kind); mem && *mem) {
                detail = " member=" + (*mem)->member;
            }
            result.samples.push_back(current_function + " kind=" + kind + detail +
                                     (is_null ? " (null)" : " (error)") +
                                     " offset=" + std::to_string(e.span.start));
        }
    }

    void walk_expr(const HirExpr* e) {
        if (!e) {
            return;
        }
        ++result.total_exprs;
        if (!e->type) {
            note_violation(*e, true);
        } else if (e->type->is_error()) {
            note_violation(*e, false);
        }
        std::visit([this](const auto& node) { walk_expr_kind(node); }, e->kind);
    }

    void walk_expr_kind(const std::unique_ptr<HirLiteral>& n) {
        if (!n) {
            return;
        }
        for (const auto& [content, pe] : n->interp_parts) {
            walk_expr(pe.get());
        }
    }
    void walk_expr_kind(const std::unique_ptr<HirVarRef>&) {}
    void walk_expr_kind(const std::unique_ptr<HirBinary>& n) {
        if (!n) {
            return;
        }
        walk_expr(n->lhs.get());
        walk_expr(n->rhs.get());
    }
    void walk_expr_kind(const std::unique_ptr<HirUnary>& n) {
        if (!n) {
            return;
        }
        walk_expr(n->operand.get());
    }
    void walk_expr_kind(const std::unique_ptr<HirCall>& n) {
        if (!n) {
            return;
        }
        for (const auto& a : n->args) {
            walk_expr(a.get());
        }
        for (const auto& a : n->captured_args) {
            walk_expr(a.get());
        }
        walk_expr(n->indirect_callee.get());
    }
    void walk_expr_kind(const std::unique_ptr<HirIndex>& n) {
        if (!n) {
            return;
        }
        walk_expr(n->object.get());
        walk_expr(n->index.get());
        for (const auto& i : n->indices) {
            walk_expr(i.get());
        }
    }
    void walk_expr_kind(const std::unique_ptr<HirMember>& n) {
        if (!n) {
            return;
        }
        walk_expr(n->object.get());
    }
    void walk_expr_kind(const std::unique_ptr<HirTernary>& n) {
        if (!n) {
            return;
        }
        walk_expr(n->condition.get());
        walk_expr(n->then_expr.get());
        walk_expr(n->else_expr.get());
    }
    void walk_expr_kind(const std::unique_ptr<HirStructLiteral>& n) {
        if (!n) {
            return;
        }
        for (const auto& f : n->fields) {
            walk_expr(f.value.get());
        }
    }
    void walk_expr_kind(const std::unique_ptr<HirArrayLiteral>& n) {
        if (!n) {
            return;
        }
        for (const auto& e : n->elements) {
            walk_expr(e.get());
        }
    }
    void walk_expr_kind(const std::unique_ptr<HirLambda>& n) {
        if (!n) {
            return;
        }
        for (const auto& s : n->body) {
            walk_stmt(s.get());
        }
    }
    void walk_expr_kind(const std::unique_ptr<HirCast>& n) {
        if (!n) {
            return;
        }
        walk_expr(n->operand.get());
    }
    void walk_expr_kind(const std::unique_ptr<HirEnumConstruct>& n) {
        if (!n) {
            return;
        }
        walk_expr(n->payload.get());
    }
    void walk_expr_kind(const std::unique_ptr<HirEnumPayload>& n) {
        if (!n) {
            return;
        }
        walk_expr(n->scrutinee.get());
    }

    void walk_pattern(const HirSwitchPattern* p) {
        if (!p) {
            return;
        }
        walk_expr(p->value.get());
        walk_expr(p->range_start.get());
        walk_expr(p->range_end.get());
        for (const auto& op : p->or_patterns) {
            walk_pattern(op.get());
        }
    }

    void walk_stmt(const HirStmt* s) {
        if (!s) {
            return;
        }
        std::visit([this](const auto& node) { walk_stmt_kind(node); }, s->kind);
    }

    void walk_stmt_kind(const std::unique_ptr<HirLet>& n) {
        if (!n) {
            return;
        }
        walk_expr(n->init.get());
        walk_expr(n->ctor_call.get());
    }
    void walk_stmt_kind(const std::unique_ptr<HirAssign>& n) {
        if (!n) {
            return;
        }
        walk_expr(n->target.get());
        walk_expr(n->value.get());
    }
    void walk_stmt_kind(const std::unique_ptr<HirReturn>& n) {
        if (!n) {
            return;
        }
        walk_expr(n->value.get());
    }
    void walk_stmt_kind(const std::unique_ptr<HirIf>& n) {
        if (!n) {
            return;
        }
        walk_expr(n->cond.get());
        for (const auto& s : n->then_block) {
            walk_stmt(s.get());
        }
        for (const auto& s : n->else_block) {
            walk_stmt(s.get());
        }
    }
    void walk_stmt_kind(const std::unique_ptr<HirLoop>& n) {
        if (!n) {
            return;
        }
        for (const auto& s : n->body) {
            walk_stmt(s.get());
        }
    }
    void walk_stmt_kind(const std::unique_ptr<HirWhile>& n) {
        if (!n) {
            return;
        }
        walk_expr(n->cond.get());
        for (const auto& s : n->body) {
            walk_stmt(s.get());
        }
    }
    void walk_stmt_kind(const std::unique_ptr<HirFor>& n) {
        if (!n) {
            return;
        }
        walk_stmt(n->init.get());
        walk_expr(n->cond.get());
        walk_expr(n->update.get());
        for (const auto& s : n->body) {
            walk_stmt(s.get());
        }
    }
    void walk_stmt_kind(const std::unique_ptr<HirBreak>&) {}
    void walk_stmt_kind(const std::unique_ptr<HirContinue>&) {}
    void walk_stmt_kind(const std::unique_ptr<HirDefer>& n) {
        if (!n) {
            return;
        }
        walk_stmt(n->body.get());
    }
    void walk_stmt_kind(const std::unique_ptr<HirExprStmt>& n) {
        if (!n) {
            return;
        }
        walk_expr(n->expr.get());
    }
    void walk_stmt_kind(const std::unique_ptr<HirBlock>& n) {
        if (!n) {
            return;
        }
        for (const auto& s : n->stmts) {
            walk_stmt(s.get());
        }
    }
    void walk_stmt_kind(const std::unique_ptr<HirSwitch>& n) {
        if (!n) {
            return;
        }
        walk_expr(n->expr.get());
        for (const auto& c : n->cases) {
            walk_pattern(c.pattern.get());
            walk_expr(c.value.get());
            for (const auto& s : c.stmts) {
                walk_stmt(s.get());
            }
        }
    }
    void walk_stmt_kind(const std::unique_ptr<HirAsm>&) {}
    void walk_stmt_kind(const std::unique_ptr<HirMustBlock>& n) {
        if (!n) {
            return;
        }
        for (const auto& s : n->body) {
            walk_stmt(s.get());
        }
    }

    void walk_function(const HirFunction* f) {
        if (!f) {
            return;
        }
        current_function = f->name;
        for (const auto& s : f->body) {
            walk_stmt(s.get());
        }
    }
};

}  // namespace

TypeAuditResult audit_types(const HirProgram& program) {
    TypeAuditResult result;
    Walker w{result, ""};
    for (const auto& decl : program.declarations) {
        if (!decl) {
            continue;
        }
        if (auto* fn = std::get_if<std::unique_ptr<HirFunction>>(&decl->kind)) {
            w.walk_function(fn->get());
        } else if (auto* impl = std::get_if<std::unique_ptr<HirImpl>>(&decl->kind)) {
            if (*impl) {
                for (const auto& m : (*impl)->methods) {
                    w.walk_function(m.get());
                }
                for (const auto& op : (*impl)->operators) {
                    if (op) {
                        w.current_function = (*impl)->target_type + "::operator";
                        for (const auto& s : op->body) {
                            w.walk_stmt(s.get());
                        }
                    }
                }
            }
        } else if (auto* gv = std::get_if<std::unique_ptr<HirGlobalVar>>(&decl->kind)) {
            if (*gv) {
                w.current_function = "<global:" + (*gv)->name + ">";
                w.walk_expr((*gv)->init.get());
            }
        } else if (auto* ib = std::get_if<std::unique_ptr<HirInitialBlock>>(&decl->kind)) {
            if (*ib) {
                w.current_function = "<initial>";
                for (const auto& s : (*ib)->body) {
                    w.walk_stmt(s.get());
                }
            }
        }
    }
    return result;
}

void report_type_audit(const TypeAuditResult& result, const std::string& source_name,
                       bool verbose) {
    std::fprintf(stderr, "[HIR-TYPE-AUDIT] %s: exprs=%zu null=%zu error=%zu %s\n",
                 source_name.c_str(), result.total_exprs, result.null_types, result.error_types,
                 result.ok() ? "OK" : "VIOLATIONS");
    for (const auto& [kind, count] : result.violations_by_kind) {
        std::fprintf(stderr, "[HIR-TYPE-AUDIT]   %s: %zu\n", kind.c_str(), count);
    }
    if (verbose) {
        for (const auto& s : result.samples) {
            std::fprintf(stderr, "[HIR-TYPE-AUDIT]   sample: %s\n", s.c_str());
        }
    }
}

}  // namespace cm::hir
