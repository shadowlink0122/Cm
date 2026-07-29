// ============================================================
// importプリプロセッサ - モジュールパス解決・探索
// ============================================================

#include "import.hpp"
#include "import_internal.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace cm::preprocessor {

std::filesystem::path ImportPreprocessor::find_module_file(
    const std::string& module_name, const std::filesystem::path& current_file) {
    // モジュール名をファイルパスに変換
    std::string filename = module_name;
    std::replace(filename.begin(), filename.end(), ':', '/');
    filename += ".cm";

    // まず相対パスでチェック（現在のファイルからの相対）
    if (!current_file.empty()) {
        auto relative_path = current_file.parent_path() / filename;
        if (std::filesystem::exists(relative_path)) {
            return relative_path;
        }
    }

    // 検索パスから探す
    for (const auto& search_path : search_paths) {
        auto full_path = search_path / filename;
        if (std::filesystem::exists(full_path)) {
            return full_path;
        }

        // mod.cm ファイルも試す
        auto mod_path = search_path / module_name / "mod.cm";
        if (std::filesystem::exists(mod_path)) {
            return mod_path;
        }
    }

    return {};  // 見つからない
}

std::string ImportPreprocessor::load_module_file(const std::filesystem::path& module_path) {
    std::ifstream file(module_path);
    if (!file) {
        throw std::runtime_error("Failed to open module file: " + module_path.string());
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    // H7段階4: 生ソース（export情報が完全に残る唯一の時点）から非exportヘルパー関数を
    // 収集してcanonicalパス鍵で保持する。改名の適用は展開断片のemit時に行う
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(module_path, ec);
    const std::string key = ec ? module_path.string() : canonical.string();
    if (module_internal_fns_.find(key) == module_internal_fns_.end()) {
        std::string prefix = module_path.stem().string();
        for (auto& c : prefix) {
            if (!std::isalnum(static_cast<unsigned char>(c))) {
                c = '_';
            }
        }
        prefix += "_" + std::to_string(module_internal_fns_.size());
        module_internal_fns_[key] = {prefix, collect_non_export_function_names(content)};
    }
    return content;
}

std::filesystem::path ImportPreprocessor::find_project_root(
    const std::filesystem::path& current_path) {
    auto path = std::filesystem::absolute(current_path);

    // プロジェクトルートの検出優先順位：
    // 1. cm.toml がある場所
    // 2. .git がある場所
    // 3. 環境変数 CM_PROJECT_ROOT
    // 4. 現在のディレクトリ

    // 上に向かって検索
    while (!path.empty() && path != path.parent_path()) {
        // cm.tomlを探す
        if (std::filesystem::exists(path / "cm.toml")) {
            return path;
        }
        // .gitディレクトリを探す
        if (std::filesystem::exists(path / ".git")) {
            return path;
        }
        path = path.parent_path();
    }

    // 環境変数をチェック
    if (const char* env_root = std::getenv("CM_PROJECT_ROOT")) {
        auto env_path = std::filesystem::path(env_root);
        if (std::filesystem::exists(env_path)) {
            return std::filesystem::absolute(env_path);
        }
    }

    // デフォルトは現在のディレクトリ
    return std::filesystem::current_path();
}

std::filesystem::path ImportPreprocessor::resolve_module_path(
    const std::string& module_specifier, const std::filesystem::path& current_file) {
    // 相対パス（./ または ../）の場合
    if (module_specifier.substr(0, 2) == "./" || module_specifier.substr(0, 3) == "../") {
        if (current_file.empty()) {
            throw std::runtime_error("Relative imports require a current file context");
        }

        auto base_dir = current_file.parent_path();

        // ./path/module::submodule 形式をチェック
        // パス部分と::サブモジュール部分を分離
        std::string path_part = module_specifier;
        size_t colon_pos = module_specifier.find("::");
        if (colon_pos != std::string::npos) {
            // ./path/module::sub の場合、./path/module がファイルパス
            path_part = module_specifier.substr(0, colon_pos);
        }

        auto relative_path = base_dir / path_part;

        // .cmファイルを試す
        auto cm_file = relative_path;
        cm_file += ".cm";
        if (std::filesystem::exists(cm_file)) {
            return std::filesystem::canonical(cm_file);
        }

        // ディレクトリ内のエントリーポイントを探す
        if (std::filesystem::exists(relative_path) &&
            std::filesystem::is_directory(relative_path)) {
            auto entry = find_module_entry_point(relative_path);
            if (!entry.empty()) {
                return entry;
            }
        }

        throw std::runtime_error("Module not found: " + module_specifier);
    }

    // 階層的インポート（std::io, lib::utils::strutil など）の場合
    // 階層パスを解決

    // :: で分割
    std::vector<std::string> segments;
    std::string current_segment;
    for (size_t i = 0; i < module_specifier.length(); ++i) {
        if (i + 1 < module_specifier.length() && module_specifier[i] == ':' &&
            module_specifier[i + 1] == ':') {
            if (!current_segment.empty()) {
                segments.push_back(current_segment);
                current_segment.clear();
            }
            ++i;  // skip second ':'
        } else {
            current_segment += module_specifier[i];
        }
    }
    if (!current_segment.empty()) {
        segments.push_back(current_segment);
    }

    // ファイル名を生成
    // segments が3つ以上の場合（例: std::mem::malloc）、最後の要素は関数/変数名として扱い、モジュールパスは最後の1つ手前まで
    std::string full_filename = module_specifier;
    std::replace(full_filename.begin(), full_filename.end(), ':', '/');

    // モジュールパス（最後のセグメントが小文字始まりの場合、それは関数/変数名）
    // ただし、完全パスに対応するファイルが存在する場合はモジュールとして扱う
    std::string module_path = full_filename;
    if (segments.size() >= 3) {
        // 最後のセグメントが小文字始まりなら、関数/変数名として扱う
        const std::string& last_segment = segments.back();
        if (!last_segment.empty() && std::islower(last_segment[0])) {
            // まずフルパスがモジュールファイルとして存在するかチェック
            // (例: std/sync/mutex.cm が存在するなら mutex はモジュール名)
            bool full_path_exists = false;
            if (!current_file.empty()) {
                auto check_path = current_file.parent_path() / (full_filename + ".cm");
                if (std::filesystem::exists(check_path)) {
                    full_path_exists = true;
                }
            }
            if (!full_path_exists) {
                for (const auto& sp : search_paths) {
                    auto check_path = sp / (full_filename + ".cm");
                    if (std::filesystem::exists(check_path)) {
                        full_path_exists = true;
                        break;
                    }
                }
            }

            if (full_path_exists) {
                // フルパスがファイルとして存在する → 最後のセグメントもモジュール名
                // module_path はそのまま full_filename を維持
                if (debug_mode) {
                    std::cout << "[PREPROCESSOR] Full path exists as module file, keeping: "
                              << module_path << "\n";
                }
            } else {
                // フルパスが存在しない → 最後のセグメントは関数/変数名
                module_path = "";
                for (size_t i = 0; i < segments.size() - 1; ++i) {
                    if (i > 0)
                        module_path += "/";
                    module_path += segments[i];
                }
                if (debug_mode) {
                    std::cout << "[PREPROCESSOR] Selective import detected, module path: "
                              << module_path << "\n";
                }
            }
        }
    }

    // 最初のコンポーネントだけのファイル名
    std::string root_filename = segments.empty() ? module_specifier : segments[0];

    // まず現在のファイルと同じディレクトリをチェック
    if (!current_file.empty()) {
        auto current_dir = current_file.parent_path();

        // セグメントが3つ以上（選択的インポート）の場合、module_pathを優先
        if (segments.size() >= 3 && module_path != full_filename) {
            // 1. モジュールパス（std/mem.cm など）を試す
            auto mod_file_path = current_dir / (module_path + ".cm");
            if (std::filesystem::exists(mod_file_path)) {
                if (debug_mode) {
                    std::cout << "[PREPROCESSOR] Found module file: " << mod_file_path << "\n";
                }
                return std::filesystem::canonical(mod_file_path);
            }

            // 2. ディレクトリ内のエントリーポイント（std/mem/mod.cm など）
            auto mod_dir_path = current_dir / module_path;
            if (std::filesystem::exists(mod_dir_path) &&
                std::filesystem::is_directory(mod_dir_path)) {
                auto entry = find_module_entry_point(mod_dir_path);
                if (!entry.empty()) {
                    if (debug_mode) {
                        std::cout << "[PREPROCESSOR] Found module entry point: " << entry << "\n";
                    }
                    return entry;
                }
            }

            // サブモジュールが見つからない場合、ルートへのフォールバックは行わない
            // (import std::nonexistent::foo が std/mod.cm に解決されるのを防ぐ)
            // 検索パスも試行する（下の for ループに委ねる）
        } else {
            // 非選択的インポート: ルートフォールバックを通常通り試行

            // 1. 完全パス（std/io/file.cm など）を最初に試す
            //    サブモジュールへの直接アクセスを優先
            auto full_path = current_dir / (full_filename + ".cm");
            if (std::filesystem::exists(full_path)) {
                if (debug_mode) {
                    std::cout << "[PREPROCESSOR] Found full path module: " << full_path << "\n";
                }
                return std::filesystem::canonical(full_path);
            }

            // 2. ルートコンポーネントのファイル（std.cm）を試す
            //    これは再エクスポートベースの解決に必要
            auto root_path = current_dir / (root_filename + ".cm");
            if (std::filesystem::exists(root_path)) {
                if (debug_mode) {
                    std::cout << "[PREPROCESSOR] Found root module: " << root_path << "\n";
                }
                return std::filesystem::canonical(root_path);
            }

            // 3. ルートディレクトリ内のエントリーポイント（std/std.cm）
            //    ただし2セグメント以上の場合、サブモジュールが実在するか検証
            auto root_dir_path = current_dir / root_filename;
            if (std::filesystem::exists(root_dir_path) &&
                std::filesystem::is_directory(root_dir_path)) {
                // サブモジュール存在チェック: std::io → std/io.cm or std/io/ が必要
                bool submodule_valid = true;
                if (segments.size() >= 2) {
                    auto sub_file = root_dir_path / (segments[1] + ".cm");
                    auto sub_dir = root_dir_path / segments[1];
                    if (!std::filesystem::exists(sub_file) &&
                        !(std::filesystem::exists(sub_dir) &&
                          std::filesystem::is_directory(sub_dir))) {
                        submodule_valid = false;
                        if (debug_mode) {
                            std::cout << "[PREPROCESSOR] Submodule '" << segments[1]
                                      << "' not found in " << root_dir_path << "\n";
                        }
                    }
                }
                if (submodule_valid) {
                    auto entry = find_module_entry_point(root_dir_path);
                    if (!entry.empty()) {
                        return entry;
                    }
                }
            }

            // 4. ディレクトリ内のエントリーポイント（std/io/io.cm など）
            auto dir_path = current_dir / full_filename;
            if (std::filesystem::exists(dir_path) && std::filesystem::is_directory(dir_path)) {
                auto entry = find_module_entry_point(dir_path);
                if (!entry.empty()) {
                    return entry;
                }
            }
        }
    }

    // 検索パスから探す
    for (const auto& search_path : search_paths) {
        // 選択的インポート（3セグメント以上）の場合、module_pathを優先
        if (segments.size() >= 3 && module_path != full_filename) {
            // 1. モジュールパス（std/nonexistent.cm など）を試す
            auto mod_file_path = search_path / (module_path + ".cm");
            if (std::filesystem::exists(mod_file_path)) {
                if (debug_mode) {
                    std::cout << "[PREPROCESSOR] Found module file in search path: "
                              << mod_file_path << "\n";
                }
                return std::filesystem::canonical(mod_file_path);
            }

            // 2. ディレクトリ内のエントリーポイント（std/mem/mod.cm など）
            auto mod_dir_path = search_path / module_path;
            if (std::filesystem::exists(mod_dir_path) &&
                std::filesystem::is_directory(mod_dir_path)) {
                auto entry = find_module_entry_point(mod_dir_path);
                if (!entry.empty()) {
                    if (debug_mode) {
                        std::cout << "[PREPROCESSOR] Found module entry point in search path: "
                                  << entry << "\n";
                    }
                    return entry;
                }
            }

            // サブモジュールが見つからない場合、ルートへのフォールバックは行わない
            continue;
        }

        // 1. 完全パスを最優先で試す (std/io/file.cm など)
        auto full_path = search_path / (full_filename + ".cm");
        if (std::filesystem::exists(full_path)) {
            if (debug_mode) {
                std::cout << "[PREPROCESSOR] Found full path in search path: " << full_path << "\n";
            }
            return std::filesystem::canonical(full_path);
        }

        // 2. ディレクトリ内のエントリーポイントを探す (std/io/file/mod.cm)
        auto dir_path = search_path / full_filename;
        if (std::filesystem::exists(dir_path) && std::filesystem::is_directory(dir_path)) {
            auto entry = find_module_entry_point(dir_path);
            if (!entry.empty()) {
                if (debug_mode) {
                    std::cout << "[PREPROCESSOR] Found module entry point in search path: " << entry
                              << "\n";
                }
                return entry;
            }
        }

        // 3. ルートコンポーネントを試す（検索パスでは後ろ側に）
        auto root_path = search_path / (root_filename + ".cm");
        if (std::filesystem::exists(root_path)) {
            return std::filesystem::canonical(root_path);
        }

        // 4. ルートディレクトリ内のエントリーポイント
        //    ただし2セグメント以上の場合、サブモジュールが実在するか検証
        auto root_dir_path = search_path / root_filename;
        if (std::filesystem::exists(root_dir_path) &&
            std::filesystem::is_directory(root_dir_path)) {
            bool submodule_valid = true;
            if (segments.size() >= 2) {
                auto sub_file = root_dir_path / (segments[1] + ".cm");
                auto sub_dir = root_dir_path / segments[1];
                if (!std::filesystem::exists(sub_file) &&
                    !(std::filesystem::exists(sub_dir) && std::filesystem::is_directory(sub_dir))) {
                    submodule_valid = false;
                }
            }
            if (submodule_valid) {
                auto entry = find_module_entry_point(root_dir_path);
                if (!entry.empty()) {
                    return entry;
                }
            }
        }
    }

    return {};  // 見つからない
}

std::filesystem::path ImportPreprocessor::find_module_entry_point(
    const std::filesystem::path& directory) {
    // module文を含むファイルを探す
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() == ".cm") {
            std::ifstream file(entry.path());
            std::string line;
            // 最初の数行だけチェック
            int line_count = 0;
            while (std::getline(file, line) && line_count++ < 10) {
                // コメントをスキップ
                if (line.find("//") == 0)
                    continue;

                // module文を検出
                std::regex module_regex(R"(^\s*module\s+([a-zA-Z_][a-zA-Z0-9_:]*)\s*;)");
                if (std::regex_search(line, module_regex)) {
                    return entry.path();
                }
            }
        }
    }

    // module文が見つからない場合、ディレクトリ名と同じ名前の.cmファイルを探す
    auto dir_name = directory.filename().string();
    auto same_name_path = directory / (dir_name + ".cm");
    if (std::filesystem::exists(same_name_path)) {
        return same_name_path;
    }

    // mod.cmを探す（後方互換性）
    auto mod_path = directory / "mod.cm";
    if (std::filesystem::exists(mod_path)) {
        return mod_path;
    }

    return {};  // エントリーポイントが見つからない
}

std::string ImportPreprocessor::format_circular_dependency_error(
    const std::vector<std::string>& stack, const std::string& module) {
    std::stringstream error;
    error << "Circular dependency detected:\n";
    for (size_t i = 0; i < stack.size(); ++i) {
        error << "  " << (i + 1) << ". " << stack[i] << "\n";
    }
    error << "  " << (stack.size() + 1) << ". " << module << " (circular reference)\n";
    return error.str();
}

std::vector<std::filesystem::path> ImportPreprocessor::find_all_modules_recursive(
    const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> modules;

    if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory)) {
        return modules;
    }

    // 再帰的にディレクトリを探索
    // .cmファイルはすべてモジュールとして扱う
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".cm") {
            modules.push_back(entry.path());
        }
    }

    // ソートして一貫した順序を保証
    std::sort(modules.begin(), modules.end());

    return modules;
}

}  // namespace cm::preprocessor
