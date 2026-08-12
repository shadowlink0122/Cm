#include "graph.hpp"

#include "internal/base/i18n.hpp"
#include "internal/base/source/location.hpp"
#include "internal/preprocessor/conditional.hpp"
#include "internal/preprocessor/import.hpp"
#include "internal/syntax/lexer/lexer.hpp"
#include "internal/syntax/parser/parser.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace cm::module_graph {

namespace {

// DFSの訪問状態（Visiting中の再訪問=循環）
enum class VisitState { Visiting, Done };

// import/再exportの1辺
struct Edge {
    std::string dep;                 // 依存先ファイル（正規化パス）
    std::vector<std::string> items;  // 選択項目（元名）
    bool wildcard = false;           // ::* / export * from
    bool module_form = false;        // モジュール全体import（namespace包み）
    std::string namespace_name;  // module_form時の名前（別名または末尾セグメント）
    bool module_aliased =
        false;  // as付きモジュールimport（namespace包みのみで平坦直接アクセスなし）
    bool is_reexport = false;  // export import / export {..} from（公開面へ転送）
    // エイリアス付き選択項目（元名→別名）。改名複製として出力する
    std::vector<std::pair<std::string, std::string>> aliased_items;
};

// 関数宣言の記録
struct FuncInfo {
    size_t span_start = 0;
    size_t span_end = 0;
    bool is_export = false;
    bool is_extern = false;  // extern宣言（ランタイムシンボルへの橋渡し。改名対象外）
    std::unordered_set<std::string> refs;  // 本文が参照する識別子（テキストスキャン）
};

struct FileInfo {
    std::string path;
    std::string source;  // 条件コンパイル+指示行空行化済み
    std::vector<Edge> edges;
    std::unordered_map<std::string, FuncInfo> functions;
    std::unordered_set<std::string> export_list;  // export { name, ... } の名前
    std::unordered_set<std::string> rest_refs;    // 関数本文以外（impl/global等）の参照
    std::unordered_set<std::string> included;     // 出力へ残す関数
    std::vector<std::string> namespace_wraps;  // namespace包み出力の名前（module形import由来）
    // FFI宣言ブロック（use libc {...}）: 包含済み関数が宣言名を参照する場合のみ出力へ残す
    std::vector<std::pair<std::pair<size_t, size_t>, std::vector<std::string>>> ffi_blocks;
    std::vector<std::pair<size_t, size_t>>
        impl_spans;  // impl定義（namespace包み時の平坦重複を回避）
    // エイリアス付き選択importの改名複製（元名→別名）
    std::vector<std::pair<std::string, std::string>> rename_copies;
    // 階層再export（export { io::{file, stream} }）: namespaceパスと対象モジュール名
    std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>> hier_exports;
    bool is_root = false;
};

// コメントと文字列リテラル本文を除去したテキストを返す（識別子スキャン用）。
// 文字列内は補間断片 {expr} のみ残す（補間が参照する識別子は包含判定に必要）
std::string strip_for_scan(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    const size_t n = text.size();
    while (i < n) {
        const char c = text[i];
        if (c == '/' && i + 1 < n && text[i + 1] == '/') {
            while (i < n && text[i] != '\n') {
                ++i;
            }
            continue;
        }
        if (c == '/' && i + 1 < n && text[i + 1] == '*') {
            i += 2;
            while (i + 1 < n && !(text[i] == '*' && text[i + 1] == '/')) {
                ++i;
            }
            i = (i + 1 < n) ? i + 2 : n;
            continue;
        }
        if (c == '`') {
            // raw文字列（バッククォート）: 補間 ${...} 内部のみ識別子走査対象として残す
            ++i;
            while (i < n && text[i] != '`') {
                if (text[i] == '\\' && i + 1 < n && text[i + 1] == '`') {
                    i += 2;
                    continue;
                }
                if (text[i] == '$' && i + 1 < n && text[i + 1] == '{') {
                    i += 2;
                    while (i < n && text[i] != '}' && text[i] != '`') {
                        out += text[i];
                        ++i;
                    }
                    out += ' ';
                    continue;
                }
                ++i;
            }
            if (i < n) {
                ++i;
            }
            out += ' ';
            continue;
        }
        if (c == '"') {
            ++i;
            bool in_interp = false;
            while (i < n && text[i] != '"') {
                if (text[i] == '\\' && i + 1 < n) {
                    i += 2;
                    continue;
                }
                if (text[i] == '{' || text[i] == '}') {
                    in_interp = (text[i] == '{');
                    out += ' ';
                    ++i;
                    continue;
                }
                out += in_interp ? text[i] : ' ';
                ++i;
            }
            if (i < n) {
                ++i;
            }
            continue;
        }
        if (c == '\'') {
            ++i;
            while (i < n && text[i] != '\'') {
                if (text[i] == '\\') {
                    ++i;
                }
                ++i;
            }
            if (i < n) {
                ++i;
            }
            continue;
        }
        out += c;
        ++i;
    }
    return out;
}

// 識別子のテキストスキャン（コメント・文字列除去済みテキストに適用する）
void collect_identifiers(const std::string& text, std::unordered_set<std::string>& out) {
    static const std::regex ident_re("[A-Za-z_][A-Za-z0-9_]*");
    const std::string cleaned = strip_for_scan(text);
    for (auto it = std::sregex_iterator(cleaned.begin(), cleaned.end(), ident_re);
         it != std::sregex_iterator(); ++it) {
        out.insert(it->str());
    }
}

// ==== 識別子のトークン精密改名（module-graph-ast-emission 第2段） ====
// プライベート改名・エイリアス改名複製で使用する。従来の\b正規表現置換は文字列リテラルの
// 内容やコメント内の同名語も書き換えて破壊していた（実測: 改名対象ヘルパー名を含む文字列
// "call hidden_calc ..." の内容が改名される）。strip_for_scanと同じ走査規則で、コメントは
// 改名対象外・文字列リテラルは補間プレースホルダ{...}内部の識別子のみ改名する
std::string rename_identifiers(const std::string& text,
                               const std::vector<std::pair<std::string, std::string>>& renames) {
    if (renames.empty()) {
        return text;
    }
    std::unordered_map<std::string, const std::string*> map;
    for (const auto& [from, to] : renames) {
        map.emplace(from, &to);
    }
    auto is_ident_start = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
    };
    auto is_ident_char = [&](char c) { return is_ident_start(c) || (c >= '0' && c <= '9'); };
    std::string out;
    out.reserve(text.size() + 64);
    size_t i = 0;
    const size_t n = text.size();
    bool in_string = false;
    bool in_interp = false;
    // 現在位置の識別子を（必要なら改名して）出力する。識別子でなければfalse
    auto emit_ident = [&]() -> bool {
        if (!is_ident_start(text[i])) {
            return false;
        }
        size_t j = i + 1;
        while (j < n && is_ident_char(text[j])) {
            ++j;
        }
        const std::string name = text.substr(i, j - i);
        auto it = map.find(name);
        out += (it != map.end()) ? *it->second : name;
        i = j;
        return true;
    };
    while (i < n) {
        const char c = text[i];
        if (!in_string) {
            if (c == '/' && i + 1 < n && text[i + 1] == '/') {
                while (i < n && text[i] != '\n') {
                    out += text[i];
                    ++i;
                }
                continue;
            }
            if (c == '/' && i + 1 < n && text[i + 1] == '*') {
                out += text[i];
                out += text[i + 1];
                i += 2;
                while (i + 1 < n && !(text[i] == '*' && text[i + 1] == '/')) {
                    out += text[i];
                    ++i;
                }
                if (i + 1 < n) {
                    out += text[i];
                    out += text[i + 1];
                    i += 2;
                } else if (i < n) {
                    out += text[i];
                    ++i;
                }
                continue;
            }
            if (c == '"') {
                in_string = true;
                in_interp = false;
                out += c;
                ++i;
                continue;
            }
            if (c == '`') {
                // raw文字列（バッククォート）: エスケープは\`のみ・複数行可。
                // 内容は改名対象外で、補間 ${...} 内部の識別子のみ改名する
                out += c;
                ++i;
                while (i < n && text[i] != '`') {
                    if (text[i] == '\\' && i + 1 < n && text[i + 1] == '`') {
                        out += text[i];
                        out += text[i + 1];
                        i += 2;
                        continue;
                    }
                    if (text[i] == '$' && i + 1 < n && text[i + 1] == '{') {
                        out += "${";
                        i += 2;
                        while (i < n && text[i] != '}' && text[i] != '`') {
                            if (emit_ident()) {
                                continue;
                            }
                            out += text[i];
                            ++i;
                        }
                        continue;
                    }
                    out += text[i];
                    ++i;
                }
                if (i < n) {
                    out += text[i];
                    ++i;
                }
                continue;
            }
            if (c == '\'') {
                // charリテラル: 内容（'"'・'{'等）が文字列/補間の状態機械を壊さないよう丸ごと通過させる
                out += c;
                ++i;
                while (i < n && text[i] != '\'') {
                    if (text[i] == '\\' && i + 1 < n) {
                        out += text[i];
                        ++i;
                    }
                    out += text[i];
                    ++i;
                }
                if (i < n) {
                    out += text[i];
                    ++i;
                }
                continue;
            }
            if (emit_ident()) {
                continue;
            }
            out += c;
            ++i;
            continue;
        }
        // 文字列リテラル内
        if (c == '\\' && i + 1 < n) {
            out += text[i];
            out += text[i + 1];
            i += 2;
            continue;
        }
        if (c == '"') {
            in_string = false;
            out += c;
            ++i;
            continue;
        }
        if (c == '{') {
            in_interp = true;
            out += c;
            ++i;
            continue;
        }
        if (c == '}') {
            in_interp = false;
            out += c;
            ++i;
            continue;
        }
        if (in_interp && emit_ident()) {
            continue;
        }
        out += c;
        ++i;
    }
    return out;
}

// ==== 包含判定のAST化（module-graph-ast-emission 第1段） ====
// 参照識別子をパース済みASTのwalkで収集する（従来の正規表現テキストスキャンcollect_identifiersを置換）。
// 収集方針は従来スキャンと同じ「出現した識別子は全て包含候補」（過剰包含は無害・過少包含はリンク欠落で即顕在化）。
// 修飾名A::Bはセグメント分割して両方を候補にし、文字列リテラルは補間プレースホルダ内部のみを識別子走査する
struct AstRefCollector {
    std::unordered_set<std::string>& out;

    explicit AstRefCollector(std::unordered_set<std::string>& o) : out(o) {}

    void add(const std::string& name) {
        size_t pos = 0;
        while (pos <= name.size()) {
            size_t sep = name.find("::", pos);
            std::string seg =
                (sep == std::string::npos) ? name.substr(pos) : name.substr(pos, sep - pos);
            if (!seg.empty()) {
                out.insert(seg);
            }
            if (sep == std::string::npos) {
                break;
            }
            pos = sep + 2;
        }
    }

    // 文字列リテラル: 補間プレースホルダ{...}の内部のみを識別子走査する（strip_for_scanの文字列規則と同一）
    void placeholders(const std::string& s) {
        size_t i = 0;
        while (i < s.size()) {
            if (s[i] == '{' && i + 1 < s.size() && s[i + 1] == '{') {
                i += 2;
                continue;
            }
            if (s[i] == '{') {
                size_t close = s.find('}', i + 1);
                if (close == std::string::npos) {
                    break;
                }
                collect_identifiers(s.substr(i + 1, close - i - 1), out);
                i = close + 1;
                continue;
            }
            ++i;
        }
    }

    void type(const ast::TypePtr& t) {
        if (!t) {
            return;
        }
        add(t->name);
        for (const auto& a : t->type_args) {
            type(a);
        }
        type(t->element_type);
        type(t->return_type);
        for (const auto& pt : t->param_types) {
            type(pt);
        }
        if (t->kind == ast::TypeKind::Union) {
            for (const auto& v : ast::union_variant_types(t)) {
                type(v);
            }
        }
    }

    void attrs(const std::vector<ast::AttributeNode>& list) {
        for (const auto& a : list) {
            add(a.name);
            for (const auto& arg : a.args) {
                add(arg);
            }
        }
    }

    void pattern(const ast::Pattern* p) {
        if (!p) {
            return;
        }
        expr(p->value.get());
        expr(p->range_start.get());
        expr(p->range_end.get());
        for (const auto& op : p->or_patterns) {
            pattern(op.get());
        }
    }

    void match_pattern(const ast::MatchPattern* p) {
        if (!p) {
            return;
        }
        expr(p->value.get());
        expr(p->range_start.get());
        expr(p->range_end.get());
        add(p->enum_variant);
        type(p->type_pattern);
        for (const auto& op : p->or_patterns) {
            match_pattern(op.get());
        }
    }

    void expr(const ast::Expr* e) {
        if (!e) {
            return;
        }
        if (const auto* lit = e->as<ast::LiteralExpr>()) {
            if (lit->is_string()) {
                placeholders(std::get<std::string>(lit->value));
            }
            return;
        }
        if (const auto* id = e->as<ast::IdentExpr>()) {
            add(id->name);
            return;
        }
        if (const auto* bin = e->as<ast::BinaryExpr>()) {
            expr(bin->left.get());
            expr(bin->right.get());
            return;
        }
        if (const auto* un = e->as<ast::UnaryExpr>()) {
            expr(un->operand.get());
            return;
        }
        if (const auto* call = e->as<ast::CallExpr>()) {
            expr(call->callee.get());
            for (const auto& a : call->args) {
                expr(a.get());
            }
            return;
        }
        if (const auto* idx = e->as<ast::IndexExpr>()) {
            expr(idx->object.get());
            expr(idx->index.get());
            return;
        }
        if (const auto* sl = e->as<ast::SliceExpr>()) {
            expr(sl->object.get());
            expr(sl->start.get());
            expr(sl->end.get());
            expr(sl->step.get());
            return;
        }
        if (const auto* mem = e->as<ast::MemberExpr>()) {
            expr(mem->object.get());
            add(mem->member);
            for (const auto& a : mem->args) {
                expr(a.get());
            }
            return;
        }
        if (const auto* tern = e->as<ast::TernaryExpr>()) {
            expr(tern->condition.get());
            expr(tern->then_expr.get());
            expr(tern->else_expr.get());
            return;
        }
        if (const auto* nw = e->as<ast::NewExpr>()) {
            type(nw->type);
            for (const auto& a : nw->args) {
                expr(a.get());
            }
            return;
        }
        if (const auto* sz = e->as<ast::SizeofExpr>()) {
            type(sz->target_type);
            expr(sz->target_expr.get());
            return;
        }
        if (const auto* to = e->as<ast::TypeofExpr>()) {
            expr(to->target_expr.get());
            return;
        }
        if (const auto* al = e->as<ast::AlignofExpr>()) {
            type(al->target_type);
            return;
        }
        if (const auto* tn = e->as<ast::TypenameOfExpr>()) {
            type(tn->target_type);
            expr(tn->target_expr.get());
            return;
        }
        if (const auto* slit = e->as<ast::StructLiteralExpr>()) {
            add(slit->type_name);
            for (const auto& f : slit->fields) {
                expr(f.value.get());
            }
            return;
        }
        if (const auto* alit = e->as<ast::ArrayLiteralExpr>()) {
            for (const auto& el : alit->elements) {
                expr(el.get());
            }
            return;
        }
        if (const auto* lam = e->as<ast::LambdaExpr>()) {
            for (const auto& pr : lam->params) {
                type(pr.type);
                expr(pr.default_value.get());
            }
            type(lam->return_type);
            if (lam->is_expr_body()) {
                expr(std::get<ast::ExprPtr>(lam->body).get());
            } else {
                for (const auto& s : std::get<std::vector<ast::StmtPtr>>(lam->body)) {
                    stmt(s.get());
                }
            }
            return;
        }
        if (const auto* m = e->as<ast::MatchExpr>()) {
            expr(m->scrutinee.get());
            for (const auto& arm : m->arms) {
                match_pattern(arm.pattern.get());
                expr(arm.guard.get());
                expr(arm.expr_body.get());
                for (const auto& s : arm.block_body) {
                    stmt(s.get());
                }
            }
            return;
        }
        if (const auto* c = e->as<ast::CastExpr>()) {
            expr(c->operand.get());
            type(c->target_type);
            return;
        }
        if (const auto* mv = e->as<ast::MoveExpr>()) {
            expr(mv->operand.get());
            return;
        }
        if (const auto* aw = e->as<ast::AwaitExpr>()) {
            expr(aw->operand.get());
            return;
        }
    }

    void stmt(const ast::Stmt* s) {
        if (!s) {
            return;
        }
        if (const auto* let = const_cast<ast::Stmt*>(s)->as<ast::LetStmt>()) {
            type(let->type);
            expr(let->init.get());
            for (const auto& a : let->ctor_args) {
                expr(a.get());
            }
            return;
        }
        if (const auto* es = const_cast<ast::Stmt*>(s)->as<ast::ExprStmt>()) {
            expr(es->expr.get());
            return;
        }
        if (const auto* ret = const_cast<ast::Stmt*>(s)->as<ast::ReturnStmt>()) {
            expr(ret->value.get());
            return;
        }
        if (const auto* ifs = const_cast<ast::Stmt*>(s)->as<ast::IfStmt>()) {
            expr(ifs->condition.get());
            for (const auto& st : ifs->then_block) {
                stmt(st.get());
            }
            for (const auto& st : ifs->else_block) {
                stmt(st.get());
            }
            return;
        }
        if (const auto* fs = const_cast<ast::Stmt*>(s)->as<ast::ForStmt>()) {
            stmt(fs->init.get());
            expr(fs->condition.get());
            expr(fs->update.get());
            for (const auto& st : fs->body) {
                stmt(st.get());
            }
            return;
        }
        if (const auto* fin = const_cast<ast::Stmt*>(s)->as<ast::ForInStmt>()) {
            expr(fin->iterable.get());
            for (const auto& st : fin->body) {
                stmt(st.get());
            }
            return;
        }
        if (const auto* ws = const_cast<ast::Stmt*>(s)->as<ast::WhileStmt>()) {
            expr(ws->condition.get());
            for (const auto& st : ws->body) {
                stmt(st.get());
            }
            return;
        }
        if (const auto* blk = const_cast<ast::Stmt*>(s)->as<ast::BlockStmt>()) {
            for (const auto& st : blk->stmts) {
                stmt(st.get());
            }
            return;
        }
        if (const auto* sw = const_cast<ast::Stmt*>(s)->as<ast::SwitchStmt>()) {
            expr(sw->expr.get());
            for (const auto& c : sw->cases) {
                pattern(c.pattern.get());
                for (const auto& st : c.stmts) {
                    stmt(st.get());
                }
            }
            return;
        }
        if (const auto* df = const_cast<ast::Stmt*>(s)->as<ast::DeferStmt>()) {
            stmt(df->body.get());
            return;
        }
        if (const auto* mb = const_cast<ast::Stmt*>(s)->as<ast::MustBlockStmt>()) {
            for (const auto& st : mb->body) {
                stmt(st.get());
            }
            return;
        }
    }

    void func(const ast::FunctionDecl& f) {
        for (const auto& pr : f.params) {
            type(pr.type);
            expr(pr.default_value.get());
        }
        type(f.return_type);
        for (const auto& gp : f.generic_params_v2) {
            for (const auto& c : gp.constraints) {
                add(c);
            }
            type(gp.const_type);
        }
        attrs(f.attributes);
        for (const auto& st : f.body) {
            stmt(st.get());
        }
    }

    // 関数以外の宣言（rest_refs用）。ImportDecl/ExportDecl(List系)/FFI use等の除外は呼び出し側で行う
    void decl(ast::Decl& d) {
        if (const auto* st = d.as<ast::StructDecl>()) {
            for (const auto& f : st->fields) {
                type(f.type);
                expr(f.default_value.get());
            }
            for (const auto& gp : st->generic_params_v2) {
                for (const auto& c : gp.constraints) {
                    add(c);
                }
                type(gp.const_type);
            }
            for (const auto& ai : st->auto_impls) {
                add(ai);
            }
            attrs(st->attributes);
            return;
        }
        if (const auto* gv = d.as<ast::GlobalVarDecl>()) {
            type(gv->type);
            expr(gv->init_expr.get());
            return;
        }
        if (const auto* td = d.as<ast::TypedefDecl>()) {
            type(td->type);
            return;
        }
        if (const auto* en = d.as<ast::EnumDecl>()) {
            for (const auto& m : en->members) {
                for (const auto& f : m.fields) {
                    type(f.second);
                }
            }
            return;
        }
        if (const auto* ifc = d.as<ast::InterfaceDecl>()) {
            for (const auto& m : ifc->methods) {
                for (const auto& pr : m.params) {
                    type(pr.type);
                }
                type(m.return_type);
            }
            for (const auto& op : ifc->operators) {
                for (const auto& pr : op.params) {
                    type(pr.type);
                }
                type(op.return_type);
            }
            return;
        }
        if (const auto* im = d.as<ast::ImplDecl>()) {
            add(im->interface_name);
            type(im->target_type);
            for (const auto& ta : im->interface_type_args) {
                type(ta);
            }
            for (const auto& m : im->methods) {
                func(*m);
            }
            for (const auto& c : im->constructors) {
                func(*c);
            }
            if (im->destructor) {
                func(*im->destructor);
            }
            for (const auto& op : im->operators) {
                for (const auto& pr : op->params) {
                    type(pr.type);
                    expr(pr.default_value.get());
                }
                type(op->return_type);
                for (const auto& st2 : op->body) {
                    stmt(st2.get());
                }
            }
            return;
        }
        if (const auto* mc = d.as<ast::MacroDecl>()) {
            type(mc->type);
            expr(mc->value.get());
            for (const auto& st2 : mc->body) {
                stmt(st2.get());
            }
            return;
        }
        if (const auto* fn = d.as<ast::FunctionDecl>()) {
            func(*fn);
            return;
        }
    }
};

// import行のディレクトリワイルドカード「/*」は字句解析でブロックコメント開始と衝突するため、
// パース前にセンチネルセグメントへ置換する（行数は保存され、graph側で"*"相当として解釈する）
constexpr const char* kDirWildcardSentinel = "__cm_dir_wildcard__";
void rewrite_dir_wildcards(std::string& source) {
    size_t pos = 0;
    while (pos < source.size()) {
        size_t eol = source.find('\n', pos);
        if (eol == std::string::npos) {
            eol = source.size();
        }
        size_t s = pos;
        while (s < eol && (source[s] == ' ' || source[s] == '\t')) {
            ++s;
        }
        const bool is_import =
            source.compare(s, 7, "import ") == 0 || source.compare(s, 14, "export import ") == 0;
        if (is_import) {
            size_t p = s;
            while (true) {
                const size_t hit = source.find("/*", p);
                if (hit == std::string::npos || hit >= eol) {
                    break;
                }
                source.replace(hit, 2, std::string("/") + kDirWildcardSentinel);
                eol = source.find('\n', pos);
                if (eol == std::string::npos) {
                    eol = source.size();
                }
                p = hit + 1;
            }
        }
        pos = eol + 1;
    }
}

struct Builder {
    const GraphParams& params;
    preprocessor::ImportPreprocessor resolver;  // resolve_module_pathの解決意味論を共有
    std::unordered_map<std::string, VisitState> state;
    std::unordered_map<std::string, FileInfo> files;
    std::vector<std::string> order;  // 依存順（依存先が先）
    std::string error;
    // R14: errorが位置情報付きの整形済み構文エラーならtrue
    bool error_has_location = false;

    explicit Builder(const GraphParams& p) : params(p), resolver(p.debug) {}

    // 条件付きコンパイルを適用する。構造違反（閉じ忘れ・過剰#end・#define）はerrorへ格納する（R6: 従来は全て無診断で、偽条件の閉じ忘れは後続コードを丸ごと飲み込んでいた）
    std::string apply_conditional(const std::string& source, const std::string& path) {
        preprocessor::ConditionalPreprocessor conditional;
        for (const auto& def : params.defines) {
            conditional.define(def);
        }
        if (params.test_mode) {
            conditional.define("TEST");
        }
        if (params.target == "baremetal-arm" || params.target == "bm" ||
            params.target == "baremetal-x86" || params.target == "bm-x86") {
            conditional.define("__NO_STD__");
            conditional.define("__BAREMETAL__");
        } else if (params.target == "uefi") {
            conditional.define("__NO_STD__");
            conditional.define("__BAREMETAL__");
            conditional.define("__UEFI__");
            conditional.define("__EFI__");
        }
        std::vector<preprocessor::ConditionalPreprocessor::Issue> issues;
        std::string processed = conditional.process(source, issues);
        if (!issues.empty() && error.empty()) {
            const auto& issue = issues.front();
            using IssueKind = preprocessor::ConditionalPreprocessor::IssueKind;
            switch (issue.kind) {
                case IssueKind::UnclosedConditional:
                    error = i18n::msgf(i18n::MsgId::PpUnclosedConditional, issue.detail, path,
                                       std::to_string(issue.line));
                    break;
                case IssueKind::UnmatchedDirective:
                    error = i18n::msgf(i18n::MsgId::PpUnmatchedDirective, issue.detail, path,
                                       std::to_string(issue.line));
                    break;
                case IssueKind::DefineNotSupported:
                    error = i18n::msgf(i18n::MsgId::PpDefineNotSupported, path,
                                       std::to_string(issue.line));
                    break;
            }
        }
        return processed;
    }

    // オフセット範囲を行単位で空行化する（行数を保存し座標を崩さない）
    static void blank_lines(std::string& source, size_t start, size_t end) {
        if (start >= source.size()) {
            return;
        }
        size_t line_start = source.rfind('\n', start);
        line_start = (line_start == std::string::npos) ? 0 : line_start + 1;
        size_t line_end = source.find('\n', end < source.size() ? end : source.size() - 1);
        line_end = (line_end == std::string::npos) ? source.size() : line_end;
        for (size_t i = line_start; i < line_end; ++i) {
            if (source[i] != '\n') {
                source[i] = ' ';
            }
        }
    }

    std::filesystem::path try_resolve(const std::string& spec, const std::string& current_file) {
        try {
            return resolver.resolve_module_path(spec, current_file);
        } catch (const std::exception&) {
            return {};
        }
    }

    static std::string canonical_of(const std::filesystem::path& p) {
        std::error_code ec;
        auto c = std::filesystem::weakly_canonical(p, ec).string();
        return c.empty() ? p.string() : c;
    }

    // 相対プレフィックスセグメントか（"./" または "../" の1回以上の繰り返し。import ../../x 等の多段親参照を含む）
    static bool is_relative_prefix(const std::string& seg) {
        if (seg == "./") {
            return true;
        }
        if (seg.empty() || seg.size() % 3 != 0) {
            return false;
        }
        for (size_t i = 0; i < seg.size(); i += 3) {
            if (seg.compare(i, 3, "../") != 0) {
                return false;
            }
        }
        return true;
    }

    // モジュールパスの指定子文字列（ソースの区切りを保存: '/'=パス連結・':'/'.'=::。
    // 相対プレフィックス ./ ../ は先頭セグメントとして連結する）
    static std::string spec_of(const std::vector<std::string>& segs, const std::vector<char>& seps,
                               size_t count) {
        std::string spec;
        size_t start = 0;
        if (!segs.empty() && is_relative_prefix(segs[0])) {
            spec = segs[0];
            start = 1;
        }
        for (size_t i = start; i < count && i < segs.size(); ++i) {
            if (i > start) {
                // seps[k]はsegments[start+k]とsegments[start+k+1]の間の区切り
                const size_t sep_index = i - start - 1;
                const char sep = (sep_index < seps.size()) ? seps[sep_index] : ':';
                spec += (sep == '/') ? "/" : "::";
            }
            spec += segs[i];
        }
        return spec;
    }

    // importをファイルと要求形へ解決する。末尾セグメントが項目の形（import std::io::println）は
    // 親モジュール+選択項目へ再解釈する（従来のimport解釈と同一）
    bool resolve_import_decl(const ast::ImportDecl& imp, const std::string& current_file,
                             std::vector<Edge>& out) {
        const auto& segs = imp.path.segments;

        // ディレクトリワイルドカード: import ./path/*; / import ./path/*::{mod1, mod2};
        // ディレクトリ配下の全モジュール（items指定時は名前でフィルタ）を使用箇所駆動で取り込む
        if (segs.back() == "*" || segs.back() == kDirWildcardSentinel) {
            const std::string dir_spec = spec_of(segs, imp.separators, segs.size() - 1);
            std::filesystem::path dir;
            auto entry = try_resolve(dir_spec, current_file);
            if (!entry.empty() && segs.size() >= 2 &&
                entry.parent_path().filename().string() == segs[segs.size() - 2]) {
                dir = entry.parent_path();
            }
            if (dir.empty()) {
                std::string rel = dir_spec;
                for (size_t pos = 0; (pos = rel.find("::", pos)) != std::string::npos;) {
                    rel.replace(pos, 2, "/");
                }
                auto cand = std::filesystem::path(current_file).parent_path() / rel;
                std::error_code ec;
                if (std::filesystem::is_directory(cand, ec)) {
                    dir = cand;
                }
            }
            std::error_code ec;
            if (dir.empty() || !std::filesystem::is_directory(dir, ec)) {
                error =
                    "cannot resolve import directory '" + dir_spec + "' in '" + current_file + "'";
                return false;
            }
            std::unordered_set<std::string> filter;
            for (const auto& item : imp.items) {
                filter.insert(item.name);
            }
            std::vector<std::filesystem::path> found;
            for (const auto& e : std::filesystem::recursive_directory_iterator(dir, ec)) {
                if (e.is_regular_file() && e.path().extension() == ".cm") {
                    if (filter.empty() || filter.count(e.path().stem().string())) {
                        found.push_back(e.path());
                    }
                }
            }
            std::sort(found.begin(), found.end());
            for (const auto& f : found) {
                // 各モジュールを非aliasのモジュールimportとして扱う（namespaceアクセス+平坦直接アクセス）
                Edge edge;
                edge.dep = canonical_of(f);
                edge.module_form = true;
                edge.namespace_name = f.stem().string();
                edge.is_reexport = imp.is_reexport;
                out.push_back(std::move(edge));
            }
            return true;
        }
        Edge edge;

        // 解決結果がセグメント名に合致するか（<seg>.cm / <seg>/mod.cm / <seg>ディレクトリのentry）。
        // リゾルバは末尾の未知セグメントを飲み込んで親モジュールを返すため、合致検査で
        // モジュールimportと項目import（親+選択）を判別する
        auto resolves_segment = [](const std::filesystem::path& p, const std::string& seg) {
            if (p.empty()) {
                return false;
            }
            const std::string stem = p.stem().string();
            if (stem == seg) {
                return true;
            }
            const std::string dir = p.parent_path().filename().string();
            return dir == seg;
        };

        // 最長プレフィックス解決: 先頭から最も長く解決できるモジュール列を特定する。
        // 単一セグメントは解決できれば合致不問（entry-point等でファイル名が異なる場合がある）
        const std::string full_spec = spec_of(segs, imp.separators, segs.size());
        std::filesystem::path path;
        size_t resolved_count = 0;
        const bool has_rel_prefix = !segs.empty() && is_relative_prefix(segs[0]);
        for (size_t k = segs.size(); k >= 1; --k) {
            // 相対プレフィックス単独（"./"のみ）は有効なモジュール指定子ではないため除外する
            if (has_rel_prefix && k == 1) {
                break;
            }
            auto cand = try_resolve(spec_of(segs, imp.separators, k), current_file);
            const bool lenient_single = (k == 1 || (has_rel_prefix && k == 2)) && !cand.empty();
            if (resolves_segment(cand, segs[k - 1]) || lenient_single) {
                path = cand;
                resolved_count = k;
                break;
            }
        }
        if (path.empty() && segs.size() > 1) {
            // プレフィックスが1つも合致しないがfull自体は解決する場合（entry-point等）はモジュールとして扱う
            auto cand = try_resolve(full_spec, current_file);
            if (!cand.empty()) {
                path = cand;
                resolved_count = segs.size();
            }
        }
        // 残りセグメントはモジュール横断（再exportされたサブモジュール）として基点ファイルからの相対で辿り、
        // 辿り切れなかった末尾セグメントを選択項目として要求する（従来のimport解釈と同一の平坦化）
        std::string fallback_item;
        while (!path.empty() && resolved_count < segs.size()) {
            auto next = try_resolve("./" + segs[resolved_count], path.string());
            if (resolves_segment(next, segs[resolved_count])) {
                path = next;
                ++resolved_count;
            } else {
                fallback_item = segs[resolved_count];
                break;
            }
        }
        if (std::getenv("CM_GRAPH_DEBUG")) {
            std::fprintf(stderr, "[RESOLVE] '%s' -> '%s' item='%s'\n", full_spec.c_str(),
                         path.string().c_str(), fallback_item.c_str());
        }
        if (path.empty()) {
            error =
                "cannot resolve import '" + imp.path.to_string() + "' in '" + current_file + "'";
            return false;
        }
        edge.dep = canonical_of(path);
        edge.is_reexport = imp.is_reexport;
        if (imp.is_wildcard) {
            edge.wildcard = true;
            out.push_back(std::move(edge));
            return true;
        }
        const bool module_alias =
            imp.items.size() == 1 && imp.items[0].name.empty() && imp.items[0].alias.has_value();
        if (!fallback_item.empty()) {
            if (module_alias) {
                // import path::mod as alias; はモジュール別名（項目でなくモジュール全体）
                edge.module_form = true;
                edge.module_aliased = true;
                edge.namespace_name = *imp.items[0].alias;
            } else {
                edge.items.push_back(fallback_item);
            }
            out.push_back(std::move(edge));
            return true;
        }
        if (module_alias) {
            // import path as alias; → モジュール全体をnamespace <alias>で包む
            edge.module_form = true;
            edge.module_aliased = true;
            edge.namespace_name = *imp.items[0].alias;
            out.push_back(std::move(edge));
            return true;
        }
        if (!imp.items.empty()) {
            for (const auto& item : imp.items) {
                if (item.alias.has_value() && !item.name.empty()) {
                    edge.aliased_items.push_back({item.name, *item.alias});
                } else {
                    edge.items.push_back(item.name);
                }
            }
            out.push_back(std::move(edge));
            return true;
        }
        // import path; → モジュール全体（namespace 末尾セグメント + 平坦直接アクセス）
        edge.module_form = true;
        edge.namespace_name = segs.back();
        out.push_back(std::move(edge));
        return true;
    }

    bool visit(const std::string& file_path, std::string raw_source, bool is_root) {
        const std::string canonical = canonical_of(file_path);
        auto it = state.find(canonical);
        if (it != state.end()) {
            if (it->second == VisitState::Visiting) {
                error = "circular import detected at '" + canonical + "'";
                return false;
            }
            return true;
        }
        state[canonical] = VisitState::Visiting;

        FileInfo info;
        info.path = canonical;
        info.is_root = is_root;
        info.source = apply_conditional(raw_source, canonical);
        if (!error.empty()) {
            return false;
        }
        rewrite_dir_wildcards(info.source);

        // ターゲットがSVの場合は依存ファイルもSVレキサーで解析する。
        // //! platform: 指定のないimport先モジュール（自動生成のROM定義等）にもSV方言（初期化子なしグローバル・posedge型等）を許可するため
        const bool sv_target =
            params.target == "sv" || params.target == "verilog" || params.target == "systemverilog";
        Lexer lexer(info.source, sv_target ? LexerPlatform::SV : LexerPlatform::Default);
        auto tokens = lexer.tokenize();
        Parser parser(std::move(tokens), lexer.is_sv());
        ast::Program program = parser.parse();
        if (parser.has_errors()) {
            // R14: 構文エラーへ行番号・該当行・キャレットを付与する。直接コンパイルしたルートファイルを「imported module」と誤表記しない
            if (!parser.diagnostics().empty()) {
                const auto& d = parser.diagnostics().front();
                SourceLocationManager slm(info.source, canonical);
                error = is_root ? "" : ("in imported module '" + canonical + "':\n");
                error += slm.format_error_location(d.span, d.message);
                error_has_location = true;
            } else {
                error = is_root ? ("syntax error in '" + canonical + "'")
                                : ("syntax error in imported module '" + canonical + "'");
            }
            return false;
        }

        // 宣言走査: import/export指示の収集と空行化・関数表の構築
        std::vector<std::pair<size_t, size_t>> func_spans;
        auto record_function = [&](const std::string& name, const Span& span, bool is_export,
                                   bool is_extern) {
            FuncInfo fi;
            fi.span_start = span.start;
            fi.span_end = span.end;
            fi.is_export = is_export;
            fi.is_extern = is_extern;
            info.functions[name] = std::move(fi);
            func_spans.push_back({span.start, span.end});
        };
        for (const auto& decl : program.declarations) {
            if (!decl) {
                continue;
            }
            if (auto* imp = decl->as<ast::ImportDecl>()) {
                std::vector<Edge> edges;
                if (!resolve_import_decl(*imp, canonical, edges)) {
                    return false;
                }
                for (auto& edge : edges) {
                    if (std::getenv("CM_GRAPH_DEBUG")) {
                        std::string items_text;
                        for (const auto& n : edge.items) {
                            items_text += n + ",";
                        }
                        std::fprintf(stderr,
                                     "[GRAPH] %s -> %s (ast_items=%zu items=[%s] wc=%d mod=%d "
                                     "reexp=%d)\n",
                                     canonical.c_str(), edge.dep.c_str(), imp->items.size(),
                                     items_text.c_str(), (int)edge.wildcard, (int)edge.module_form,
                                     (int)edge.is_reexport);
                    }
                    info.edges.push_back(std::move(edge));
                }
                blank_lines(info.source, decl->span.start, decl->span.end);
                continue;
            }
            if (auto* exp = decl->as<ast::ExportDecl>()) {
                if (exp->kind == ast::ExportDecl::List) {
                    // 階層再export項目（export { io::{file, stream} }）はnamespaceパスごとに集約する
                    std::unordered_map<std::string, size_t> hier_index;
                    for (const auto& item : exp->items) {
                        if (item.namespace_path.has_value()) {
                            const std::string key = item.namespace_path->to_string();
                            auto hit = hier_index.find(key);
                            if (hit == hier_index.end()) {
                                hier_index[key] = info.hier_exports.size();
                                info.hier_exports.push_back(
                                    {item.namespace_path->segments, {item.name}});
                            } else {
                                info.hier_exports[hit->second].second.push_back(item.name);
                            }
                        } else {
                            info.export_list.insert(item.name);
                        }
                    }
                    blank_lines(info.source, decl->span.start, decl->span.end);
                } else if (exp->kind == ast::ExportDecl::ReExport ||
                           exp->kind == ast::ExportDecl::WildcardReExport) {
                    // export {..} from m / export * from m は再export辺
                    std::string spec = exp->from_module ? exp->from_module->to_string() : "";
                    auto dep = try_resolve(spec, canonical);
                    if (dep.empty()) {
                        error = "cannot resolve re-export '" + spec + "' in '" + canonical + "'";
                        return false;
                    }
                    Edge edge;
                    edge.is_reexport = true;
                    edge.dep = canonical_of(dep);
                    if (exp->kind == ast::ExportDecl::WildcardReExport) {
                        edge.wildcard = true;
                    } else {
                        for (const auto& item : exp->items) {
                            edge.items.push_back(item.name);
                        }
                    }
                    info.edges.push_back(std::move(edge));
                    blank_lines(info.source, decl->span.start, decl->span.end);
                } else if (exp->declaration) {
                    // export fn 等の宣言付きexport（定義そのものなので残す）
                    if (auto* fn = exp->declaration->as<ast::FunctionDecl>()) {
                        record_function(fn->name, decl->span, true, fn->is_extern);
                    }
                }
                continue;
            }
            if (auto* fn = decl->as<ast::FunctionDecl>()) {
                record_function(fn->name, decl->span, fn->visibility == ast::Visibility::Export,
                                fn->is_extern);
                continue;
            }
            if (auto* use = decl->as<ast::UseDecl>()) {
                // FFI宣言ブロックは宣言名を参照する関数が包含された場合のみ残す（未使用FFIの名前衝突を防ぐ）
                if (use->kind == ast::UseDecl::FFIUse && !is_root) {
                    std::vector<std::string> names;
                    for (const auto& f : use->ffi_funcs) {
                        names.push_back(f.name);
                    }
                    info.ffi_blocks.push_back({{decl->span.start, decl->span.end}, names});
                }
                continue;
            }
            if (decl->as<ast::ImplDecl>()) {
                // implはnamespace包み複製と平坦出力の二重定義を避けるため位置を記録する
                info.impl_spans.push_back({decl->span.start, decl->span.end});
                continue;
            }
            if (decl->as<ast::ModuleDecl>() && !is_root) {
                blank_lines(info.source, decl->span.start, decl->span.end);
            }
        }

        // 参照集合: 各関数と、関数以外の宣言（impl・グローバル等）。既定はAST walk（module-graph-ast-emission 第1段）。
        // CM_GRAPH_TEXT_SCAN=1 で従来の正規表現テキストスキャンへフォールバックできる（挙動比較・切り分け用）
        if (std::getenv("CM_GRAPH_TEXT_SCAN")) {
            for (auto& [name, fi] : info.functions) {
                const size_t s = std::min(fi.span_start, info.source.size());
                const size_t e = std::min(fi.span_end, info.source.size());
                collect_identifiers(info.source.substr(s, e - s), fi.refs);
            }
            std::string rest = info.source;
            // FFI宣言ブロックは残余参照から除外する（宣言名そのものを使用参照と誤認しないため）
            for (const auto& [span, names] : info.ffi_blocks) {
                func_spans.push_back(span);
            }
            std::sort(func_spans.begin(), func_spans.end(),
                      [](auto& a, auto& b) { return a.first > b.first; });
            for (const auto& [s, e] : func_spans) {
                if (s < rest.size()) {
                    rest.erase(s, std::min(e, rest.size()) - s);
                }
            }
            collect_identifiers(rest, info.rest_refs);
        } else {
            for (const auto& decl : program.declarations) {
                if (!decl) {
                    continue;
                }
                // 関数（export宣言付き含む）は自関数のrefsへ、それ以外はrest_refsへ収集する
                if (const auto* fn = decl->as<ast::FunctionDecl>()) {
                    auto fit = info.functions.find(fn->name);
                    if (fit != info.functions.end()) {
                        AstRefCollector rc(fit->second.refs);
                        rc.func(*fn);
                    } else {
                        AstRefCollector rc(info.rest_refs);
                        rc.func(*fn);
                    }
                    continue;
                }
                if (const auto* exp = decl->as<ast::ExportDecl>()) {
                    // List/ReExportは空行化済み（辺として処理）。宣言付きexportのみ中身を収集する
                    if (exp->declaration) {
                        if (const auto* fn2 = exp->declaration->as<ast::FunctionDecl>()) {
                            auto fit = info.functions.find(fn2->name);
                            AstRefCollector rc(fit != info.functions.end() ? fit->second.refs
                                                                           : info.rest_refs);
                            rc.func(*fn2);
                        } else {
                            AstRefCollector rc(info.rest_refs);
                            rc.decl(*exp->declaration);
                        }
                    }
                    continue;
                }
                if (decl->as<ast::ImportDecl>()) {
                    continue;  // 辺として処理済み（テキストも空行化済み）
                }
                if (const auto* use = decl->as<ast::UseDecl>()) {
                    // FFI宣言ブロックの宣言名は使用参照と誤認しない（従来のffi_blocks除外と同じ）。
                    // 宣言のシグネチャ型のみ参照として収集する
                    for (const auto& f : use->ffi_funcs) {
                        AstRefCollector rc(info.rest_refs);
                        rc.type(f.return_type);
                        for (const auto& pr : f.params) {
                            rc.type(pr.second);
                        }
                    }
                    continue;
                }
                if (const auto* mod = decl->as<ast::ModuleDecl>()) {
                    // 非rootのmodule宣言は空行化済み（namespace包み経路が別処理）。rootのみ中身を収集する
                    if (is_root) {
                        std::function<void(const ast::ModuleDecl&)> walk_mod =
                            [&](const ast::ModuleDecl& md) {
                                for (const auto& inner : md.declarations) {
                                    if (!inner) {
                                        continue;
                                    }
                                    if (const auto* nested = inner->as<ast::ModuleDecl>()) {
                                        walk_mod(*nested);
                                        continue;
                                    }
                                    AstRefCollector rc(info.rest_refs);
                                    rc.decl(*inner);
                                }
                            };
                        walk_mod(*mod);
                    }
                    continue;
                }
                AstRefCollector rc(info.rest_refs);
                rc.decl(*decl);
            }
        }

        // 依存を先に処理
        for (const auto& edge : info.edges) {
            std::ifstream ifs(edge.dep);
            if (!ifs) {
                error = "cannot read imported module '" + edge.dep + "'";
                return false;
            }
            std::stringstream buffer;
            buffer << ifs.rdbuf();
            if (!visit(edge.dep, buffer.str(), false)) {
                return false;
            }
        }

        state[canonical] = VisitState::Done;
        files[canonical] = std::move(info);
        order.push_back(canonical);
        return true;
    }

    // 関数の包含（同一ファイル内参照のクロージャ展開）
    void include_function(FileInfo& info, const std::string& name) {
        if (!info.included.insert(name).second) {
            return;
        }
        auto it = info.functions.find(name);
        if (it == info.functions.end()) {
            return;
        }
        for (const auto& ref : it->second.refs) {
            if (ref != name && info.functions.count(ref) && !info.included.count(ref)) {
                include_function(info, ref);
            }
        }
    }

    // 要求nameをfileの定義・再export辺から解決して包含マークする
    void request_item(const std::string& file, const std::string& name,
                      std::unordered_set<std::string>& guard) {
        if (!guard.insert(file + "\n" + name).second) {
            return;
        }
        auto fit = files.find(file);
        if (fit == files.end()) {
            return;
        }
        FileInfo& info = fit->second;
        auto fnit = info.functions.find(name);
        if (fnit != info.functions.end()) {
            // 可視性検査: 非exportシンボルの選択importは診断エラー（第3段。旧経路の警告から昇格）
            if (!fnit->second.is_export && !info.export_list.count(name)) {
                if (error.empty()) {
                    error = i18n::msgf(i18n::MsgId::ImportNonExportedSymbol, name, info.path);
                }
                return;
            }
            include_function(info, name);
            return;
        }
        for (const auto& edge : info.edges) {
            if (!edge.is_reexport) {
                continue;
            }
            if (edge.wildcard || edge.items.empty()) {
                request_item(edge.dep, name, guard);
                continue;
            }
            // 選択的再export（export import x::{items}）は要求名がitemsに含まれる場合に辿る（R9）。
            // ただしMIR組み込みが実体のI/O系はCm定義を取り込むと組み込みの書式処理を影で
            // 置き換えて挙動が変わるため（js/svターゲットではvoid*/FFIが非対応でエラー）従来どおり素通しする
            if (!is_builtin_io_name(name) &&
                std::find(edge.items.begin(), edge.items.end(), name) != edge.items.end()) {
                request_item(edge.dep, name, guard);
            }
        }
    }

    // MIR組み込みが実体のI/O関数名（Cm定義の取り込みで影置換してはならないもの）
    static bool is_builtin_io_name(const std::string& name) {
        return name == "print" || name == "println" || name == "eprint" || name == "eprintln";
    }

    // ワイルドカード要求: 公開面（export関数+exportリスト）を包含し、再export辺へ伝播する。
    // filter非nullは「importer側が参照する名前のみ」の使用箇所駆動（import x::*用。無関係な公開関数の
    // 平坦展開が他モジュールの同名定義と衝突するのを防ぐ）。null（module形importの平坦直接アクセス）は
    // 全export包含で、後続ファイルが未importの兄弟モジュールへ平坦参照する従来連結の可視性を保つ
    void request_wildcard(const std::string& file, const std::string& importer,
                          const std::unordered_set<std::string>* filter,
                          std::unordered_set<std::string>& guard) {
        const std::string guard_key = file + "\n*\n" + (filter ? importer : std::string("<all>"));
        if (!guard.insert(guard_key).second) {
            return;
        }
        auto fit = files.find(file);
        if (fit == files.end()) {
            return;
        }
        FileInfo& info = fit->second;
        for (auto& [name, fi] : info.functions) {
            if ((fi.is_export || info.export_list.count(name)) &&
                (!filter || filter->count(name))) {
                include_function(info, name);
            }
        }
        for (const auto& edge : info.edges) {
            if (!edge.is_reexport) {
                continue;
            }
            if (edge.wildcard || edge.items.empty()) {
                request_wildcard(edge.dep, importer, filter, guard);
                continue;
            }
            // 選択的再export項目は個別要求で辿る（R9。request_itemと同じ組み込みI/O素通し規則。
            // filter付き＝使用箇所駆動のワイルドカードでは参照される名前のみ）
            for (const auto& item : edge.items) {
                if (is_builtin_io_name(item) || (filter && !filter->count(item))) {
                    continue;
                }
                request_item(edge.dep, item, guard);
            }
        }
    }

    // 同名シンボルの多重import診断（M2）: rootの選択importが同名を異なるモジュールから取り込む場合は曖昧
    void check_duplicate_imports(const FileInfo& info) {
        auto rel = [](const std::string& p) {
            std::error_code ec;
            auto r = std::filesystem::relative(p, std::filesystem::current_path(), ec);
            return ec ? p : r.string();
        };
        std::unordered_map<std::string, std::string> exposed;
        for (const auto& edge : info.edges) {
            for (const auto& name : edge.items) {
                auto it = exposed.find(name);
                if (it != exposed.end() && it->second != edge.dep) {
                    if (error.empty()) {
                        error = i18n::msgf(i18n::MsgId::ImportDuplicateSymbol, name,
                                           rel(it->second), rel(edge.dep));
                    }
                    return;
                }
                exposed[name] = edge.dep;
            }
        }
    }

    // 選択解決の全体駆動（ルート全包含・残余参照シード・import辺の要求伝播）
    void resolve_inclusion() {
        std::unordered_set<std::string> guard;
        // root側から処理する（依存順の逆）。各ファイルの包含集合はimporter側の要求で先に確定するため、
        // そのファイル自身のワイルドカードimportを「包含済み関数の参照集合」でフィルタできる
        for (auto rit = order.rbegin(); rit != order.rend(); ++rit) {
            const auto& path = *rit;
            FileInfo& info = files[path];
            if (info.is_root) {
                check_duplicate_imports(info);
                for (auto& [name, fi] : info.functions) {
                    include_function(info, name);
                }
            }
            for (const auto& ref : info.rest_refs) {
                if (info.functions.count(ref)) {
                    include_function(info, ref);
                }
            }
            // importer側の使用参照: 包含済み関数の本文参照+残余テキスト参照
            std::unordered_set<std::string> usage = info.rest_refs;
            for (const auto& name : info.included) {
                auto fit2 = info.functions.find(name);
                if (fit2 != info.functions.end()) {
                    usage.insert(fit2->second.refs.begin(), fit2->second.refs.end());
                }
            }
            for (const auto& edge : info.edges) {
                if (edge.is_reexport) {
                    // 再export辺は公開面の転送であり、要求（request_item/wildcard）経由でのみ辿る
                    continue;
                }
                if (edge.module_form) {
                    auto dit = files.find(edge.dep);
                    if (dit != files.end()) {
                        dit->second.namespace_wraps.push_back(edge.namespace_name);
                    }
                    if (!edge.module_aliased) {
                        // 非aliasのモジュールimportは公開関数の平坦直接アクセスを全export包含で提供する（従来経路と同一）
                        request_wildcard(edge.dep, path, nullptr, guard);
                    }
                    continue;
                }
                if (edge.wildcard) {
                    request_wildcard(edge.dep, path, &usage, guard);
                    continue;
                }
                for (const auto& item : edge.items) {
                    request_item(edge.dep, item, guard);
                }
                for (const auto& [name, alias] : edge.aliased_items) {
                    auto dit = files.find(edge.dep);
                    if (dit != files.end() && dit->second.functions.count(name)) {
                        const auto& dep_info = dit->second;
                        if (!dep_info.functions.at(name).is_export &&
                            !dep_info.export_list.count(name)) {
                            if (error.empty()) {
                                error = i18n::msgf(i18n::MsgId::ImportNonExportedSymbol, name,
                                                   dep_info.path);
                            }
                            continue;
                        }
                        // 別名複製として出力する（元名の平坦出力は要求しない）。同一ファイル内の依存関数のみ包含する
                        dit->second.rename_copies.push_back({name, alias});
                        for (const auto& ref : dit->second.functions[name].refs) {
                            if (ref != name && dit->second.functions.count(ref)) {
                                include_function(dit->second, ref);
                            }
                        }
                    } else {
                        request_item(edge.dep, name, guard);
                    }
                }
            }
        }
    }

    // 出力段のFFI判定用: 全ファイルの包含済み関数の参照と残余参照の和集合、包含済み関数名の集合
    std::unordered_set<std::string> included_usage;
    std::unordered_set<std::string> included_names;
    void finalize_usage() {
        for (const auto& [path, info] : files) {
            included_usage.insert(info.rest_refs.begin(), info.rest_refs.end());
            for (const auto& name : info.included) {
                included_names.insert(name);
                auto fit = info.functions.find(name);
                if (fit != info.functions.end()) {
                    included_usage.insert(fit->second.refs.begin(), fit->second.refs.end());
                }
            }
            // 改名複製（エイリアス付き選択import）も本文が出力されるため参照を数える
            for (const auto& [name, alias] : info.rename_copies) {
                auto fit = info.functions.find(name);
                if (fit != info.functions.end()) {
                    included_usage.insert(fit->second.refs.begin(), fit->second.refs.end());
                }
            }
        }
    }

    // namespace包み出力（module形import由来）。階層再export（export { io::{file, stream} }）は
    // 対象モジュールの実体を入れ子のnamespaceとして再構築する
    void emit_namespace_block(
        const std::string& file, const std::string& ns,
        const std::function<void(const std::string&, const std::string&)>& append) {
        auto fit = files.find(file);
        if (fit == files.end()) {
            return;
        }
        const FileInfo& info = fit->second;
        append(file, "namespace " + ns + " {");
        for (const auto& [ns_path, names] : info.hier_exports) {
            for (const auto& seg : ns_path) {
                append(file, "namespace " + seg + " {");
            }
            for (const auto& name : names) {
                for (const auto& edge : info.edges) {
                    if (edge.module_form && edge.namespace_name == name) {
                        emit_namespace_block(edge.dep, name, append);
                    }
                }
            }
            for (size_t i = 0; i < ns_path.size(); ++i) {
                append(file, "}");
            }
        }
        append(file, info.source);
        append(file, "}");
    }
};

}  // namespace

GraphResult build(const std::string& root_file, const std::string& root_source,
                  const GraphParams& params) {
    GraphResult result;
    Builder builder(params);
    if (!builder.visit(root_file, root_source, true)) {
        result.error = builder.error;
        result.error_has_location = builder.error_has_location;
        return result;
    }
    builder.resolve_inclusion();
    if (!builder.error.empty()) {
        // 包含解決の診断（可視性検査等）
        result.error = builder.error;
        return result;
    }
    builder.finalize_usage();

    // 依存順に連結する。未包含関数は行保存で空行化し、module形importはnamespaceで包んだ複製を出力する
    std::function<void(const std::string&, const std::string&)> append_source =
        [&result](const std::string& path, const std::string& source) {
            std::string text = source;
            if (!text.empty() && text.back() != '\n') {
                text += '\n';
            }
            size_t line = 1;
            size_t pos = 0;
            while (pos < text.size()) {
                size_t nl = text.find('\n', pos);
                if (nl == std::string::npos) {
                    nl = text.size();
                }
                result.source_map.push_back({path, line, ""});
                ++line;
                pos = nl + 1;
            }
            result.combined_source += text;
        };

    for (const auto& path : builder.order) {
        auto& info = builder.files[path];
        const size_t start_offset = result.combined_source.size();

        // 生出力（選択されなかった関数を空行化）
        std::string filtered = info.source;
        for (const auto& [name, fi] : info.functions) {
            if (!info.included.count(name)) {
                Builder::blank_lines(filtered, fi.span_start, fi.span_end);
            }
        }
        // FFI宣言ブロックは、宣言名がいずれかの包含済みコードから参照され、かつ同名のユーザー関数が
        // 包含されていない場合のみ残す（未使用FFI・ユーザー定義に遮蔽されたFFIの名前衝突を防ぐ）
        for (const auto& [span, names] : info.ffi_blocks) {
            bool needed = false;
            for (const auto& n : names) {
                if (builder.included_usage.count(n) && !builder.included_names.count(n)) {
                    needed = true;
                    break;
                }
            }
            if (!needed) {
                Builder::blank_lines(filtered, span.first, span.second);
            }
        }
        if (std::getenv("CM_GRAPH_DEBUG")) {
            std::fprintf(stderr, "[EMIT] %s impls=%zu wraps=%zu included=%zu\n", path.c_str(),
                         info.impl_spans.size(), info.namespace_wraps.size(), info.included.size());
        }
        // namespace包み複製を出す場合、implは複製側にのみ残して平坦側は空行化する（二重実装を防ぐ）
        if (!info.namespace_wraps.empty()) {
            for (const auto& [s, e] : info.impl_spans) {
                Builder::blank_lines(filtered, s, e);
            }
        }
        // 閉包で取り込んだ非exportヘルパー関数は改名して非修飾の直接呼び出しを遮断する
        // （export関数の内部実装としてのみ機能させる。従来経路のH7段階4と同一の可視性遮断）
        std::vector<std::pair<std::string, std::string>> priv_renames;
        if (!info.is_root) {
            std::string prefix = std::filesystem::path(path).stem().string();
            for (auto& c : prefix) {
                if (!std::isalnum(static_cast<unsigned char>(c))) {
                    c = '_';
                }
            }
            size_t priv_index = 0;
            for (const auto& name : info.included) {
                auto fit = info.functions.find(name);
                if (fit == info.functions.end() || fit->second.is_export || fit->second.is_extern ||
                    info.export_list.count(name) || name == "main" || name == "efi_main") {
                    continue;
                }
                if (std::getenv("CM_GRAPH_DEBUG")) {
                    std::fprintf(stderr, "[PRIV] %s rename %s (export=%d extern=%d)\n",
                                 path.c_str(), name.c_str(), (int)fit->second.is_export,
                                 (int)fit->second.is_extern);
                }
                priv_renames.push_back({name, "__cm_priv_" + prefix + "_" +
                                                  std::to_string(priv_index++) + "_" + name});
            }
            // トークン精密改名（文字列内容・コメントを破壊しない。補間{...}内部は改名対象）
            filtered = rename_identifiers(filtered, priv_renames);
        }
        append_source(path, filtered);

        // エイリアス付き選択importの改名複製（関数本文を複製し名前のみ別名へ置換）
        for (const auto& [name, alias] : info.rename_copies) {
            auto fit = info.functions.find(name);
            if (fit == info.functions.end()) {
                continue;
            }
            const size_t s = std::min(fit->second.span_start, info.source.size());
            const size_t e = std::min(fit->second.span_end, info.source.size());
            std::string text = info.source.substr(s, e - s);
            // 本体名→別名とプライベート改名をトークン精密に適用する
            std::vector<std::pair<std::string, std::string>> copy_renames;
            copy_renames.emplace_back(name, alias);
            copy_renames.insert(copy_renames.end(), priv_renames.begin(), priv_renames.end());
            text = rename_identifiers(text, copy_renames);
            append_source(path, text);
        }

        // namespace包み出力（module形import。内容はファイル全体で、名前は検査時にns::修飾される。
        // 階層再exportを持つモジュールは対象モジュールを入れ子namespaceとして再構築する）
        for (const auto& ns : info.namespace_wraps) {
            builder.emit_namespace_block(path, ns, append_source);
        }

        result.module_ranges.push_back({path, "", 0, start_offset, result.combined_source.size()});
        if (path != builder.order.back()) {
            result.imported_modules.push_back(path);
        }
    }

    // デバッグ支援: 連結結果を常時ダンプ（従来の.tmp/preprocessed.cmと対）
    {
        std::ofstream out(".tmp/structured.cm");
        if (out) {
            out << result.combined_source;
        }
    }

    result.ok = true;
    return result;
}

}  // namespace cm::module_graph
