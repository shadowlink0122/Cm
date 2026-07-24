// ============================================================
// importプリプロセッサ - import文の再帰展開・namespaceラップ・ソースマップ生成
// ============================================================

#include "internal/preprocessor/import.hpp"
#include "internal/preprocessor/import_internal.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cm::preprocessor {

std::string ImportPreprocessor::process_imports(const std::string& source,
                                                const std::filesystem::path& current_file,
                                                std::unordered_set<std::string>& imported_files,
                                                SourceMap& source_map,
                                                std::vector<ModuleRange>& module_ranges,
                                                const std::string& import_chain,
                                                size_t /* import_line_in_parent */) {
    std::stringstream result;
    std::stringstream input(source);
    std::string line;
    size_t line_number = 0;  // 元ファイルの行番号を追跡

    std::string current_file_str =
        current_file.empty()
            ? "<unknown>"
            : std::filesystem::relative(current_file, std::filesystem::current_path()).string();

    // 出力行を追加するヘルパー
    auto emit_line = [&](const std::string& output_line, const std::string& orig_file,
                         size_t orig_line, const std::string& chain) {
        result << output_line << "\n";
        source_map.push_back({orig_file, orig_line, chain});
    };

    // 複数行のソースを追加するヘルパー
    auto emit_source = [&](const std::string& src, const std::string& orig_file,
                           const std::string& chain, size_t start_line = 1) {
        std::stringstream ss(src);
        std::string l;
        size_t ln = start_line;
        while (std::getline(ss, l)) {
            emit_line(l, orig_file, ln++, chain);
        }
    };

    // 各行を処理
    while (std::getline(input, line)) {
        line_number++;
        // インポート文を検出（複数パターンに対応）
        // 基本: import module;
        // エイリアス: import module as alias;
        // from構文: import { items } from module;
        // 相対: import ./module;
        // std::regex を排除 — 高速な文字列チェックに置換
        if (debug_mode) {
            std::cout << "[PREPROCESSOR] Processing line: " << line << "\n";
        }

        // 再export行（export import X;）もimportとして展開する（M7）。
        // 従来この構文は未処理で、mod.cmの再exportを経由する選択import
        // （import std::collections::{Vector} 等）が本体を一切取り込めなかった
        bool is_export_import = false;
        {
            size_t exp_pos = skip_ws(line);
            if (starts_with_keyword(line, exp_pos, "export")) {
                size_t imp_pos = skip_ws(line, exp_pos + 6);
                is_export_import = starts_with_keyword(line, imp_pos, "import");
            }
        }

        if (is_import_line(line) || is_export_import) {
            if (debug_mode) {
                std::cout << "[PREPROCESSOR] Matched import line: " << line << "\n";
            }
            // コメントを除去してからパース（複数行対応）
            auto strip_comment = [](const std::string& text) {
                return text.substr(0, text.find("//"));
            };
            std::string import_statement = strip_comment(line);
            std::string import_source_line = line;
            if (is_export_import) {
                // 先頭のexportキーワードを落としてimport文として解析する
                size_t exp_pos = skip_ws(import_statement);
                import_statement = import_statement.substr(exp_pos + 6);
            }

            auto count_braces = [](const std::string& text) {
                int count = 0;
                for (char c : text) {
                    if (c == '{') {
                        count++;
                    } else if (c == '}') {
                        count--;
                    }
                }
                return count;
            };

            size_t import_line_number = line_number;
            int brace_depth = count_braces(import_statement);
            bool has_semicolon = import_statement.find(';') != std::string::npos;

            while ((!has_semicolon || brace_depth > 0) && std::getline(input, line)) {
                line_number++;
                if (debug_mode) {
                    std::cout << "[PREPROCESSOR] Processing line: " << line << "\n";
                }
                import_source_line += "\n" + line;
                std::string part = strip_comment(line);
                import_statement += " " + part;
                brace_depth += count_braces(part);
                if (part.find(';') != std::string::npos) {
                    has_semicolon = true;
                }
            }

            // 末尾の空白を除去
            import_statement.erase(import_statement.find_last_not_of(" \t\n\r;") + 1);

            // インポート文をパース
            auto import_info = parse_import_statement(import_statement);
            if (is_export_import) {
                // 再exportはドット区切りのモジュール名（module std.collections.vector 形式）を
                // 取るため::形式へ変換し、アイテム指定が無ければ内容を非修飾で取り込む
                // （wildcard相当。再exportの意味論＝サブモジュールのexportを自モジュールの表面にする）
                if (import_info.module_name.find("::") == std::string::npos &&
                    import_info.module_name.find('/') == std::string::npos) {
                    std::string converted;
                    for (char mc : import_info.module_name) {
                        if (mc == '.') {
                            converted += "::";
                        } else {
                            converted += mc;
                        }
                    }
                    import_info.module_name = converted;
                }
                if (!import_info.items.empty()) {
                    // 選択的再export（export import x::{items}）は展開しない。
                    // io/mod.cmのprintln等はMIR組み込みが実体であり、Cm定義を取り込むと
                    // 組み込みの書式処理を影で置き換えて挙動が変わるため、従来どおり素通しする
                    emit_source(import_source_line, current_file_str, import_chain,
                                import_line_number);
                    continue;
                }
                import_info.is_wildcard = true;
                import_info.is_reexport = true;
            }
            import_info.line_number = import_line_number;
            // ファイル名を相対パスに変換
            import_info.source_file =
                std::filesystem::relative(current_file, std::filesystem::current_path()).string();
            import_info.source_line = import_source_line;

            // 階層的インポート（std::io）を処理
            // std::ioは3 に解決する必要がある

            if (debug_mode) {
                std::cout << "[PREPROCESSOR] Found import: " << import_info.module_name;
                if (!import_info.alias.empty()) {
                    std::cout << " as " << import_info.alias;
                }
                if (import_info.is_recursive_wildcard) {
                    std::cout << " (recursive wildcard)";
                }
                std::cout << "\n";
            }

            // 再帰的ワイルドカードインポートの処理
            if (import_info.is_recursive_wildcard) {
                // ディレクトリパスを解決
                std::filesystem::path base_dir;
                if (import_info.module_name.substr(0, 2) == "./" ||
                    import_info.module_name.substr(0, 3) == "../") {
                    base_dir = current_file.parent_path() / import_info.module_name;
                } else {
                    base_dir = project_root / import_info.module_name;
                }

                if (!std::filesystem::exists(base_dir) ||
                    !std::filesystem::is_directory(base_dir)) {
                    std::stringstream error;
                    error << import_info.source_file << ":" << import_info.line_number << ":8: ";
                    error << "エラー: ディレクトリが見つかりません: " << import_info.module_name
                          << "\n";
                    throw std::runtime_error(error.str());
                }

                // パスを正規化（相対パス計算のため）
                base_dir = std::filesystem::canonical(base_dir);

                // すべてのモジュールを再帰的に検出
                auto all_modules = find_all_modules_recursive(base_dir);

                if (debug_mode) {
                    std::cout << "[PREPROCESSOR] Found " << all_modules.size() << " modules in "
                              << base_dir << "\n";
                }

                // 選択的インポートの場合、モジュール名でフィルタリング
                // import ./path/*::{mod1, mod2} 形式
                if (!import_info.items.empty()) {
                    std::vector<std::filesystem::path> filtered;
                    for (const auto& mod_path : all_modules) {
                        std::string stem = mod_path.stem().string();
                        if (std::find(import_info.items.begin(), import_info.items.end(), stem) !=
                            import_info.items.end()) {
                            filtered.push_back(mod_path);
                        }
                    }
                    all_modules = std::move(filtered);

                    if (debug_mode) {
                        std::cout << "[PREPROCESSOR] Filtered to " << all_modules.size()
                                  << " modules\n";
                    }
                }

                // 基準パスからの相対パスを計算（正規化する）
                auto parent_dir = current_file.parent_path();
                if (parent_dir.empty()) {
                    parent_dir = std::filesystem::current_path();
                } else {
                    parent_dir = std::filesystem::canonical(parent_dir);
                }

                // 各モジュールをインポート
                for (const auto& mod_path : all_modules) {
                    // 相対パスを計算してインポート文を生成
                    auto rel_path = std::filesystem::relative(mod_path, parent_dir);
                    std::string rel_str = rel_path.string();
                    // 拡張子を削除
                    if (rel_str.length() > 3 && rel_str.substr(rel_str.length() - 3) == ".cm") {
                        rel_str = rel_str.substr(0, rel_str.length() - 3);
                    }
                    // ./プレフィックスを追加
                    if (rel_str[0] != '.') {
                        rel_str = "./" + rel_str;
                    }

                    if (debug_mode) {
                        std::cout << "[PREPROCESSOR] Recursive import: " << rel_str << "\n";
                    }

                    // 擬似的なインポート文を作成して処理
                    std::string pseudo_import = "import " + rel_str + ";";
                    result << "// Recursive import: " << rel_str << "\n";

                    // 再帰的に処理（このインポートを追加）
                    auto sub_info = parse_import_statement(pseudo_import);
                    sub_info.line_number = import_info.line_number;
                    sub_info.source_file = import_info.source_file;
                    sub_info.source_line = pseudo_import;

                    // モジュールパスを解決して処理
                    auto sub_module_path = resolve_module_path(sub_info.module_name, current_file);
                    if (sub_module_path.empty())
                        continue;

                    std::string sub_canonical =
                        std::filesystem::canonical(sub_module_path).string();

                    // 既にインポート済みならスキップ
                    if (imported_modules.count(sub_canonical) > 0)
                        continue;

                    // 循環依存チェック
                    if (std::find(import_stack.begin(), import_stack.end(), sub_canonical) !=
                        import_stack.end())
                        continue;

                    import_stack.push_back(sub_canonical);
                    imported_modules.insert(sub_canonical);

                    // モジュールを読み込み
                    std::string sub_module_source = load_module_file(sub_module_path);
                    std::string sub_file_str =
                        std::filesystem::relative(sub_module_path, std::filesystem::current_path())
                            .string();
                    std::string sub_chain = import_chain + " -> " + sub_file_str;
                    sub_module_source =
                        process_imports(sub_module_source, sub_module_path, imported_files,
                                        source_map, module_ranges, sub_chain, line_number);

                    // exportブロック抽出用にオリジナルソースを保存（remove前に）
                    std::string original_sub_source = sub_module_source;
                    sub_module_source = remove_export_keywords(sub_module_source);

                    import_stack.pop_back();

                    // 名前空間パスを計算（ディレクトリ構造から）
                    auto dir_rel =
                        std::filesystem::relative(sub_module_path.parent_path(), base_dir);
                    std::string ns_path = dir_rel.string();
                    // バックスラッシュをスラッシュに変換
                    std::replace(ns_path.begin(), ns_path.end(), '\\', '/');

                    // 名前空間を構築
                    std::vector<std::string> ns_parts;
                    std::stringstream ns_ss(ns_path);
                    std::string ns_part;
                    while (std::getline(ns_ss, ns_part, '/')) {
                        if (!ns_part.empty() && ns_part != ".") {
                            ns_parts.push_back(ns_part);
                        }
                    }

                    // 名前空間を開く
                    for (const auto& ns : ns_parts) {
                        result << "namespace " << ns << " {\n";
                    }

                    result << sub_module_source;

                    // 名前空間を逆順で閉じる
                    for (auto it = ns_parts.rbegin(); it != ns_parts.rend(); ++it) {
                        result << "} // namespace " << *it << "\n";
                    }

                    // exportされたシンボルをnamespace外にも展開
                    std::string sub_exported = extract_exported_blocks(original_sub_source);
                    if (!sub_exported.empty()) {
                        result << "// ===== Exported symbols from " << rel_str
                               << " (direct access) =====\n";
                        result << sub_exported << "\n";
                        result << "// ===== End exported symbols =====\n";
                    }

                    imported_files.insert(sub_canonical);
                }

                continue;  // 次の行へ
            }

            // モジュールパスを解決
            auto module_path = resolve_module_path(import_info.module_name, current_file);

            // 大文字開始の末尾セグメント（型名）はモジュールではなく親モジュールからの選択importとして再解釈する（M7）。
            // 大小文字を区別しないファイルシステム（macOS等）では std::collections::Vector が
            // vector.cm へ誤解決され、namespace Vector と struct Vector が衝突してDuplicate methodになる。
            // 大小文字を区別するファイルシステム（Linux等）では解決失敗またはルートモジュール
            // （std/mod.cm等）への誤フォールバックになるため、いずれも同じ再解釈で
            // プラットフォーム差を無くす
            if (!import_info.is_wildcard && import_info.items.empty()) {
                size_t last_sep = import_info.module_name.rfind("::");
                if (last_sep != std::string::npos && last_sep > 0) {
                    std::string last_part = import_info.module_name.substr(last_sep + 2);
                    bool upper_head = !last_part.empty() &&
                                      std::isupper(static_cast<unsigned char>(last_part[0]));
                    // 解決結果が末尾セグメントに対応しているか検証する。
                    // 対応する形は「<last_part>.cm ファイル」（大小文字完全一致）または
                    // 「<last_part>/ ディレクトリのエントリポイント」のみ。
                    // それ以外（大小文字違いの一致・親/ルートモジュールへのフォールバック）は
                    // 誤解決とみなして再解釈する
                    bool resolved_matches_segment = false;
                    if (upper_head && !module_path.empty()) {
                        std::string actual = module_path.filename().string();
                        std::string parent_dir = module_path.parent_path().filename().string();
                        resolved_matches_segment =
                            (actual == last_part + ".cm") || (parent_dir == last_part);
                    }
                    if (upper_head && (module_path.empty() || !resolved_matches_segment)) {
                        std::string parent = import_info.module_name.substr(0, last_sep);
                        auto parent_path = resolve_module_path(parent, current_file);
                        if (!parent_path.empty()) {
                            import_info.module_name = parent;
                            import_info.items.push_back(last_part);
                            module_path = parent_path;
                            if (debug_mode) {
                                std::cout << "[PREPROCESSOR] Reinterpreted as selective import: "
                                          << parent << "::{" << last_part << "}\n";
                            }
                        }
                    }
                }
            }

            if (module_path.empty()) {
                // 詳細なエラーメッセージ
                std::stringstream error;
                error << import_info.source_file << ":" << import_info.line_number << ":8: ";
                error << "エラー: モジュールが見つかりません: " << import_info.module_name << "\n";
                error << "  " << import_info.source_line << "\n";
                error << "         ^" << std::string(import_info.module_name.length() - 1, '~')
                      << "\n";
                throw std::runtime_error(error.str());
            }

            // 正規化されたパスを取得
            std::string canonical_path = std::filesystem::canonical(module_path).string();

            // 循環依存チェック（再インポート防止より先に行う）
            if (std::find(import_stack.begin(), import_stack.end(), canonical_path) !=
                import_stack.end()) {
                // 詳細なエラーメッセージを生成
                std::stringstream error;
                error << "Circular dependency detected:\n";
                error << import_info.source_file << ":" << import_info.line_number << ":1: ";
                error << "エラー: 循環依存が検出されました\n";
                error << "  " << import_info.source_line << "\n";

                // インポートスタックを表示（相対パスで）
                error << "\n依存関係:\n";
                auto cwd = std::filesystem::current_path();
                for (size_t i = 0; i < import_stack.size(); ++i) {
                    auto rel_path = std::filesystem::relative(import_stack[i], cwd);
                    error << "  " << (i + 1) << ". " << rel_path.string() << "\n";
                }
                auto rel_canonical = std::filesystem::relative(canonical_path, cwd);
                error << "  " << (import_stack.size() + 1) << ". " << rel_canonical.string()
                      << " (循環参照)\n";

                throw std::runtime_error(error.str());
            }

            // 選択的インポートの場合、新しいシンボルがあるかチェック
            bool need_process = false;
            std::vector<std::string> new_items;

            // 同一ファイルからの2回目以降の選択importか（初回展開で本体・ネスト領域は出力済み）
            bool repeat_selective_import = false;
            if (!import_info.items.empty() && !import_info.is_wildcard) {
                // 選択的インポート: 新しいシンボルのみをインポート
                auto& imported = imported_symbols[canonical_path];
                repeat_selective_import = !imported.empty();
                for (const auto& item : import_info.items) {
                    if (imported.find(item) == imported.end()) {
                        new_items.push_back(item);
                        imported.insert(item);
                        need_process = true;
                    }
                }

                if (!need_process) {
                    if (debug_mode) {
                        std::cout << "[PREPROCESSOR] All symbols already imported from: "
                                  << canonical_path << "\n";
                    }
                    result << "// All symbols already imported from: " << import_info.module_name
                           << "\n";
                    continue;
                }

                if (debug_mode) {
                    std::cout << "[PREPROCESSOR] New symbols to import: ";
                    for (const auto& item : new_items) {
                        std::cout << item << " ";
                    }
                    std::cout << "\n";
                }
            } else {
                // ワイルドカードまたはモジュール全体のインポート
                // 再インポート防止チェック
                if (imported_modules.count(canonical_path) > 0) {
                    if (debug_mode) {
                        std::cout << "[PREPROCESSOR] Skipping already imported: " << canonical_path
                                  << "\n";
                    }
                    result << "// Already imported: " << import_info.module_name << "\n";
                    continue;
                }
                imported_modules.insert(canonical_path);
                need_process = true;
            }

            // インポートスタックに追加
            import_stack.push_back(canonical_path);

            // キャッシュチェック
            std::string module_source;
            std::string module_file_str =
                std::filesystem::relative(module_path, std::filesystem::current_path()).string();
            std::string module_chain = import_chain + " -> " + module_file_str;

            // 再帰呼び出し用のダミーソースマップ（実際のマッピングは出力時に行う）
            SourceMap dummy_source_map;
            std::vector<ModuleRange> dummy_module_ranges;

            // export抽出用にオリジナルファイル（再帰import展開前）を保存
            std::string raw_module_source;

            if (module_cache.count(canonical_path) > 0) {
                module_source = module_cache[canonical_path];
                // キャッシュからraw sourceも取得
                raw_module_source = raw_module_cache[canonical_path];
            } else {
                // モジュールファイルを読み込む
                module_source = load_module_file(module_path);
                // 再帰import展開前のソースを保存（export抽出用）
                raw_module_source = module_source;

                // モジュール内のインポートを再帰的に処理（ダミーソースマップを使用）
                module_source =
                    process_imports(module_source, module_path, imported_files, dummy_source_map,
                                    dummy_module_ranges, module_chain, line_number);

                // キャッシュに保存
                module_cache[canonical_path] = module_source;
                raw_module_cache[canonical_path] = raw_module_source;

                if (debug_mode) {
                    std::cerr << "[IMPORT-DBG] " << module_file_str
                              << " raw=" << raw_module_source.size()
                              << " expanded=" << module_source.size()
                              << " smap=" << dummy_source_map.size() << "\n";
                }
            }

            // インポートスタックから削除
            import_stack.pop_back();

            // エクスポートフィルタリング（選択的インポートの場合）。
            // 2回目以降は増分モードで新規シンボルのみを出力し、初回展開で出力済みの
            // ネストimport領域・非export型/implの再出力によるDuplicate methodを防ぐ（M7）
            if (!import_info.items.empty() && !import_info.is_wildcard) {
                // 新しいシンボルのみをフィルタリング
                if (!new_items.empty()) {
                    module_source =
                        filter_exports(module_source, new_items, repeat_selective_import);
                } else {
                    module_source =
                        filter_exports(module_source, import_info.items, repeat_selective_import);
                }
            }

            // exportブロック抽出用にサブインポート展開済みソースを保存（export キーワードあり + Exported symbols セクションあり）
            std::string export_extraction_source = module_source;

            // exportキーワードを削除（キャッシュして重複処理を回避）
            if (processed_module_cache.count(canonical_path) > 0 && import_info.items.empty()) {
                // 選択的importでなければキャッシュを使用
                module_source = processed_module_cache[canonical_path];
            } else {
                module_source = remove_export_keywords(module_source);
                if (import_info.items.empty()) {
                    processed_module_cache[canonical_path] = module_source;
                }
            }

            // エイリアスの処理
            if (!import_info.alias.empty()) {
                result << "\n// ===== Begin module: " << import_info.module_name << " (as "
                       << import_info.alias << ") =====\n";
                result << "namespace " << import_info.alias << " {\n";
                result << module_source;
                result << "} // namespace " << import_info.alias << "\n";
                result << "// ===== End module: " << import_info.module_name << " =====\n\n";
            } else if ((import_info.is_from_import || !import_info.items.empty()) &&
                       !import_info.is_wildcard) {
                // from構文または選択的インポート（::{items}）の場合
                // 名前空間でラップせずにインポート（直接アクセス可能）
                emit_line("", "<generated>", 0, import_chain);
                emit_line("// ===== Selective import from " + import_info.module_name + " =====",
                          "<generated>", 0, import_chain);

                // サブモジュールパスがある場合、そのサブモジュールの名前空間内の内容を展開
                std::string submodule_ns;
                size_t path_end = import_info.module_name.find_last_of("/");
                if (path_end != std::string::npos) {
                    size_t colon_pos = import_info.module_name.find("::", path_end);
                    if (colon_pos != std::string::npos) {
                        submodule_ns = import_info.module_name.substr(colon_pos + 2);
                    }
                }

                std::string source_to_emit;
                if (!submodule_ns.empty()) {
                    // サブモジュールの名前空間内の内容を抽出
                    std::string extracted = extract_namespace_content(module_source, submodule_ns);
                    if (!extracted.empty()) {
                        // 選択的インポートの場合、アイテムのみをフィルタ
                        if (!import_info.items.empty()) {
                            extracted = filter_exports(extracted, import_info.items);
                        }
                        source_to_emit = remove_export_keywords(extracted);
                    } else {
                        source_to_emit = remove_export_keywords(module_source);
                    }
                } else {
                    // フィルタリングして出力
                    if (!import_info.items.empty()) {
                        source_to_emit = filter_exports(module_source, import_info.items);
                    } else {
                        source_to_emit = module_source;
                    }
                    source_to_emit = remove_export_keywords(source_to_emit);
                }

                // emit_sourceでソースマップに追加
                emit_source(source_to_emit, module_file_str, module_chain, 1);

                emit_line(
                    "// ===== End selective import from " + import_info.module_name + " =====",
                    "<generated>", 0, import_chain);
                emit_line("", "<generated>", 0, import_chain);
            } else if (import_info.is_wildcard && !import_info.is_recursive_wildcard) {
                // ワイルドカードインポート（::*）の場合
                // サブモジュールパスがある場合、そのサブモジュールの名前空間内の内容を展開
                std::string submodule_ns;
                size_t path_end = import_info.module_name.find_last_of("/");
                if (path_end != std::string::npos) {
                    size_t colon_pos = import_info.module_name.find("::", path_end);
                    if (colon_pos != std::string::npos) {
                        submodule_ns = import_info.module_name.substr(colon_pos + 2);
                    }
                }

                result << "\n// ===== Wildcard import from " << import_info.module_name
                       << " =====\n";
                if (!submodule_ns.empty()) {
                    std::string extracted = extract_namespace_content(module_source, submodule_ns);
                    if (!extracted.empty()) {
                        result << remove_export_keywords(extracted) << "\n";
                    } else {
                        result << remove_export_keywords(module_source) << "\n";
                    }
                } else {
                    result << remove_export_keywords(module_source) << "\n";
                }
                result << "// ===== End wildcard import from " << import_info.module_name
                       << " =====\n\n";
            } else {
                // 通常のインポート - namespaceでラップ
                emit_line("", "<generated>", 0, import_chain);
                emit_line("// ===== Begin module: " + import_info.module_name + " =====",
                          "<generated>", 0, import_chain);

                // ./path/module::submodule 形式をチェック
                std::string submodule_path;
                std::string base_module_name = import_info.module_name;

                // 相対パス内の :: を探す（パス部分の後）
                size_t path_end = base_module_name.find_last_of("/");
                if (path_end != std::string::npos) {
                    size_t colon_pos = base_module_name.find("::", path_end);
                    if (colon_pos != std::string::npos) {
                        submodule_path = base_module_name.substr(colon_pos + 2);
                        base_module_name = base_module_name.substr(0, colon_pos);
                    }
                }

                // namespace名を決定
                std::string module_namespace;

                // サブモジュールパスがある場合、サブモジュールのみを名前空間として使用（親モジュールの名前空間はスキップ）
                if (!submodule_path.empty()) {
                    module_namespace = submodule_path;
                } else {
                    // 1. モジュールソースから module 宣言を抽出
                    // 2. なければパスの最後のコンポーネントを使用
                    module_namespace = extract_module_namespace(module_source);
                }

                if (module_namespace.empty()) {
                    // module宣言がない場合、パスの最後のコンポーネントを使用
                    std::string namespace_path = base_module_name;
                    // ./ または ../ を削除
                    if (namespace_path.find("./") == 0) {
                        namespace_path = namespace_path.substr(2);
                    } else if (namespace_path.find("../") == 0) {
                        namespace_path = namespace_path.substr(3);
                    }

                    // 最後のコンポーネントを取得（/ または :: で分割）
                    size_t last_sep = namespace_path.find_last_of("/");
                    if (last_sep != std::string::npos) {
                        module_namespace = namespace_path.substr(last_sep + 1);
                    } else {
                        // :: で分割を試みる
                        size_t last_colon = namespace_path.rfind("::");
                        if (last_colon != std::string::npos) {
                            module_namespace = namespace_path.substr(last_colon + 2);
                        } else {
                            module_namespace = namespace_path;
                        }
                    }
                }

                // :: を含む場合は階層的な名前空間を作成
                std::vector<std::string> namespace_parts;
                std::string current;
                for (size_t i = 0; i < module_namespace.length(); ++i) {
                    if (i + 1 < module_namespace.length() && module_namespace[i] == ':' &&
                        module_namespace[i + 1] == ':') {
                        if (!current.empty()) {
                            namespace_parts.push_back(current);
                            current.clear();
                        }
                        ++i;  // skip second ':'
                    } else {
                        current += module_namespace[i];
                    }
                }
                if (!current.empty()) {
                    namespace_parts.push_back(current);
                }

                // 階層的な名前空間を開く
                // サブモジュールパスがある場合は外側の名前空間をスキップ（モジュールソース内ですでに正しい名前空間が生成されている）
                if (submodule_path.empty()) {
                    for (const auto& ns : namespace_parts) {
                        emit_line("namespace " + ns + " {", "<generated>", 0, import_chain);
                    }
                }

                // exportキーワードを削除
                std::string cleaned_source = remove_export_keywords(module_source);
                // モジュールソースの各行をemit_sourceで出力（元ファイルの行番号を追跡）
                emit_source(cleaned_source, module_file_str, module_chain, 1);

                // 名前空間を逆順で閉じる
                if (submodule_path.empty()) {
                    for (auto it = namespace_parts.rbegin(); it != namespace_parts.rend(); ++it) {
                        // namespace閉じ行はコンパイラ生成なので、元ファイル情報なし
                        emit_line("} // namespace " + *it, "<generated>", 0, import_chain);
                    }
                }
                emit_line("// ===== End module: " + import_info.module_name + " =====",
                          "<generated>", 0, import_chain);

                // exportされたシンボルをnamespace外にも展開
                // これにより名前空間修飾なしでも呼び出し可能になる
                // サブインポート展開済みソースを使用し推移的エクスポートも含める
                std::string exported_blocks = extract_exported_blocks(export_extraction_source);
                if (!exported_blocks.empty()) {
                    emit_line("// ===== Exported symbols from " + import_info.module_name +
                                  " (direct access) =====",
                              "<generated>", 0, import_chain);
                    emit_source(exported_blocks, module_file_str, module_chain, 1);
                    emit_line("// ===== End exported symbols =====", "<generated>", 0,
                              import_chain);
                }

                emit_line("", "<generated>", 0, import_chain);
            }

            // imported_filesに追加（後方互換性のため）
            imported_files.insert(canonical_path);
        } else {
            // インポート文以外はそのまま出力
            emit_line(line, current_file_str, line_number, import_chain);
        }
    }

    return result.str();
}

}  // namespace cm::preprocessor
