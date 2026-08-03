#include "graph.hpp"

#include "internal/preprocessor/conditional.hpp"
#include "internal/preprocessor/import.hpp"
#include "internal/syntax/lexer/lexer.hpp"
#include "internal/syntax/parser/parser.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace cm::module_graph {

namespace {

// DFSの訪問状態（Visiting中の再訪問=循環）
enum class VisitState { Visiting, Done };

struct Builder {
    const GraphParams& params;
    preprocessor::ImportPreprocessor resolver;  // find_module_fileの解決意味論を共有
    std::unordered_map<std::string, VisitState> state;
    std::vector<std::pair<std::string, std::string>> ordered;  // {絶対パス, 指示行空行化済みソース}
    std::string error;

    explicit Builder(const GraphParams& p) : params(p), resolver(p.debug) {}

    // 条件コンパイルを適用する（従来経路と同一の定義集合）
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

    // importのモジュール指定子をファイルへ解決する（従来と同一のresolve_module_path=mod.cm・相対import対応）。
    // まず全パスをモジュールとして探し、見つからなければ末尾セグメントを選択importの項目とみなして親を探す
    std::filesystem::path resolve_import(const ast::ImportDecl& imp,
                                         const std::filesystem::path& current_file) {
        const auto& segs = imp.path.segments;
        auto try_resolve = [&](const std::string& spec) -> std::filesystem::path {
            try {
                return resolver.resolve_module_path(spec, current_file);
            } catch (const std::exception&) {
                return {};
            }
        };
        auto path = try_resolve(imp.path.to_string());
        if (!path.empty()) {
            return path;
        }
        if (segs.size() > 1) {
            ast::ModulePath parent;
            parent.segments.assign(segs.begin(), segs.end() - 1);
            return try_resolve(parent.to_string());
        }
        return {};
    }

    // 1ファイルを処理する（依存を先に出力へ積み、自身を後置する）
    bool visit(const std::string& file_path, std::string source, bool is_root) {
        std::error_code ec;
        auto canonical = std::filesystem::weakly_canonical(file_path, ec).string();
        if (canonical.empty()) {
            canonical = file_path;
        }
        auto it = state.find(canonical);
        if (it != state.end()) {
            if (it->second == VisitState::Visiting) {
                error = "circular import detected at '" + canonical + "'";
                return false;
            }
            return true;  // 解決済み（重複訪問の抑止）
        }
        state[canonical] = VisitState::Visiting;

        std::string processed = apply_conditional(source);

        // 独立パース（このファイル単体の座標で構文エラーを検出できる）
        Lexer lexer(processed);
        auto tokens = lexer.tokenize();
        Parser parser(std::move(tokens), lexer.is_sv());
        ast::Program program = parser.parse();
        if (parser.has_errors()) {
            error = "syntax error in imported module '" + canonical + "'";
            return false;
        }

        // import収集と指示行の空行化
        struct Pending {
            std::filesystem::path path;
        };
        std::vector<Pending> deps;
        for (const auto& decl : program.declarations) {
            if (!decl) {
                continue;
            }
            if (auto* imp = decl->as<ast::ImportDecl>()) {
                auto dep = resolve_import(*imp, canonical);
                if (dep.empty()) {
                    error = "cannot resolve import '" + imp->path.to_string() + "' in '" +
                            canonical + "'";
                    return false;
                }
                deps.push_back({dep});
                blank_lines(processed, decl->span.start, decl->span.end);
                continue;
            }
            if (auto* exp = decl->as<ast::ExportDecl>()) {
                // 宣言付きexport（export fn ...）は定義そのものなので残す。
                // リスト/再エクスポート指示は第1段では全公開のため空行化する（選択規則は第2段の解決表）
                if (exp->kind != ast::ExportDecl::Declaration) {
                    blank_lines(processed, decl->span.start, decl->span.end);
                }
                continue;
            }
            if (decl->as<ast::ModuleDecl>() && !is_root) {
                // 依存ファイルのmoduleヘッダは結合バッファでは重複するため空行化する（ルートはSVトップ名抽出に使う）
                blank_lines(processed, decl->span.start, decl->span.end);
            }
        }

        for (const auto& dep : deps) {
            const std::string dep_path = dep.path.string();
            std::ifstream ifs(dep.path);
            if (!ifs) {
                error = "cannot read imported module '" + dep_path + "'";
                return false;
            }
            std::stringstream buffer;
            buffer << ifs.rdbuf();
            if (!visit(dep_path, buffer.str(), false)) {
                return false;
            }
        }

        state[canonical] = VisitState::Done;
        ordered.emplace_back(canonical, std::move(processed));
        return true;
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

    // 依存順（依存先が先）に連結し、行単位のsource_mapとファイル単位のmodule_rangesを生成する
    for (auto& [path, source] : builder.ordered) {
        const size_t start_offset = result.combined_source.size();
        if (!source.empty() && source.back() != '\n') {
            source += '\n';
        }
        size_t line = 1;
        size_t pos = 0;
        while (pos < source.size()) {
            size_t nl = source.find('\n', pos);
            if (nl == std::string::npos) {
                nl = source.size();
            }
            result.source_map.push_back({path, line, ""});
            ++line;
            pos = nl + 1;
        }
        result.combined_source += source;
        result.module_ranges.push_back({path, "", 0, start_offset, result.combined_source.size()});
        if (path != builder.ordered.back().first) {
            result.imported_modules.push_back(path);
        }
    }

    result.ok = true;
    return result;
}

}  // namespace cm::module_graph
