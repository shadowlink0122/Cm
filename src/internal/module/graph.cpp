#include "graph.hpp"

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

    explicit Builder(const GraphParams& p) : params(p), resolver(p.debug) {}

    std::string apply_conditional(const std::string& source) const {
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
        return conditional.process(source);
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

    // モジュールパスの指定子文字列（ソースの区切りを保存: '/'=パス連結・':'/'.'=::。
    // 相対プレフィックス ./ ../ は先頭セグメントとして連結する）
    static std::string spec_of(const std::vector<std::string>& segs, const std::vector<char>& seps,
                               size_t count) {
        std::string spec;
        size_t start = 0;
        if (!segs.empty() && (segs[0] == "./" || segs[0] == "../")) {
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
        const bool has_rel_prefix = !segs.empty() && (segs[0] == "./" || segs[0] == "../");
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
        info.source = apply_conditional(raw_source);
        rewrite_dir_wildcards(info.source);

        Lexer lexer(info.source);
        auto tokens = lexer.tokenize();
        Parser parser(std::move(tokens), lexer.is_sv());
        ast::Program program = parser.parse();
        if (parser.has_errors()) {
            error = "syntax error in imported module '" + canonical + "'";
            if (!parser.diagnostics().empty()) {
                error += ": " + parser.diagnostics().front().message;
            }
            return false;
        }

        // 宣言走査: import/export指示の収集と空行化・関数表の構築
        std::vector<std::pair<size_t, size_t>> func_spans;
        auto record_function = [&](const std::string& name, const Span& span, bool is_export) {
            FuncInfo fi;
            fi.span_start = span.start;
            fi.span_end = span.end;
            fi.is_export = is_export;
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
                        record_function(fn->name, decl->span, true);
                    }
                }
                continue;
            }
            if (auto* fn = decl->as<ast::FunctionDecl>()) {
                record_function(fn->name, decl->span, fn->visibility == ast::Visibility::Export);
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

        // 参照集合: 各関数本文と、関数以外の残余テキスト（impl・グローバル等）
        for (auto& [name, fi] : info.functions) {
            const size_t s = std::min(fi.span_start, info.source.size());
            const size_t e = std::min(fi.span_end, info.source.size());
            collect_identifiers(info.source.substr(s, e - s), fi.refs);
        }
        {
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
        if (info.functions.count(name)) {
            include_function(info, name);
            return;
        }
        for (const auto& edge : info.edges) {
            if (!edge.is_reexport) {
                continue;
            }
            if (edge.wildcard || edge.items.empty() ||
                std::find(edge.items.begin(), edge.items.end(), name) != edge.items.end()) {
                request_item(edge.dep, name, guard);
            }
        }
    }

    // ワイルドカード要求: 公開面（export関数+exportリスト）のうちimporter側が参照する名前のみ包含し、再export辺へ伝播する。
    // 全包含にしないのは、無関係な公開関数の平坦展開が他モジュールの同名定義と衝突するため（従来経路と同じ使用箇所駆動）
    void request_wildcard(const std::string& file, const std::string& importer,
                          const std::unordered_set<std::string>& filter,
                          std::unordered_set<std::string>& guard) {
        if (!guard.insert(file + "\n*\n" + importer).second) {
            return;
        }
        auto fit = files.find(file);
        if (fit == files.end()) {
            return;
        }
        FileInfo& info = fit->second;
        for (auto& [name, fi] : info.functions) {
            if ((fi.is_export || info.export_list.count(name)) && filter.count(name)) {
                include_function(info, name);
            }
        }
        for (const auto& edge : info.edges) {
            if (!edge.is_reexport) {
                continue;
            }
            if (edge.wildcard || edge.items.empty()) {
                request_wildcard(edge.dep, importer, filter, guard);
            } else {
                for (const auto& item : edge.items) {
                    if (filter.count(item)) {
                        request_item(edge.dep, item, guard);
                    }
                }
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
                        // 非aliasのモジュールimportは公開関数の平坦直接アクセスも提供する（従来経路と同一）
                        request_wildcard(edge.dep, path, usage, guard);
                    }
                    continue;
                }
                if (edge.wildcard) {
                    request_wildcard(edge.dep, path, usage, guard);
                    continue;
                }
                for (const auto& item : edge.items) {
                    request_item(edge.dep, item, guard);
                }
                for (const auto& [name, alias] : edge.aliased_items) {
                    auto dit = files.find(edge.dep);
                    if (dit != files.end() && dit->second.functions.count(name)) {
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
        return result;
    }
    builder.resolve_inclusion();
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
            text = std::regex_replace(text, std::regex("\\b" + name + "\\b"), alias);
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
