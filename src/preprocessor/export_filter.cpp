// ============================================================
// importプリプロセッサ - export構文の抽出・除去・再export処理
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

namespace cm::preprocessor {

std::string ImportPreprocessor::filter_exports(const std::string& module_source,
                                               const std::vector<std::string>& import_items) {
    // 選択的インポート：指定されたアイテムのみを抽出
    std::stringstream result;
    std::stringstream input(module_source);
    std::string line;
    bool in_wanted_block = false;    // 欲しいエクスポートブロック内
    bool in_unwanted_block = false;  // 不要なエクスポートブロック内
    std::string current_export_name;
    std::vector<std::string> block_lines;
    int brace_depth = 0;
    bool found_opening_brace = false;

    while (std::getline(input, line)) {
        // エクスポートされた関数/構造体/定数/implを検出（regexなし）
        bool matched = false;
        size_t pos = skip_ws(line);

        // impl パターンを先にチェック: [export] impl Type for Interface
        if (!in_wanted_block && !in_unwanted_block) {
            size_t impl_pos = pos;
            bool has_exp = starts_with_keyword(line, pos, "export");
            if (has_exp)
                impl_pos = skip_ws(line, pos + 6);

            if (starts_with_keyword(line, impl_pos, "impl")) {
                size_t after_impl = skip_ws(line, impl_pos + 4);
                size_t name_start = after_impl;
                while (after_impl < line.size() &&
                       (std::isalnum(static_cast<unsigned char>(line[after_impl])) ||
                        line[after_impl] == '_'))
                    after_impl++;
                if (after_impl > name_start) {
                    current_export_name = line.substr(name_start, after_impl - name_start);
                    matched = true;
                }
            }
        }

        // export キーワードで始まる場合
        if (!matched && !in_wanted_block && !in_unwanted_block &&
            starts_with_keyword(line, pos, "export")) {
            size_t after_export = skip_ws(line, pos + 6);

            // const パターン: export const type name =
            if (starts_with_keyword(line, after_export, "const")) {
                size_t after_const = skip_ws(line, after_export + 5);
                // 型名をスキップ
                while (after_const < line.size() &&
                       (std::isalnum(static_cast<unsigned char>(line[after_const])) ||
                        line[after_const] == '_'))
                    after_const++;
                after_const = skip_ws(line, after_const);
                // 名前を取得
                size_t name_start = after_const;
                while (after_const < line.size() &&
                       (std::isalnum(static_cast<unsigned char>(line[after_const])) ||
                        line[after_const] == '_'))
                    after_const++;
                if (after_const > name_start) {
                    current_export_name = line.substr(name_start, after_const - name_start);
                    matched = true;
                }
            }

            // 一般的な export パターン: export [修飾子...] type name
            if (!matched) {
                size_t p = after_export;
                // 修飾子をスキップ: extern "C", <T>, static, inline, async
                while (p < line.size()) {
                    if (starts_with_keyword(line, p, "extern")) {
                        p += 6;
                        p = skip_ws(line, p);
                        if (p < line.size() && line[p] == '"') {
                            auto close = line.find('"', p + 1);
                            if (close != std::string::npos)
                                p = close + 1;
                        }
                        p = skip_ws(line, p);
                    } else if (line[p] == '<') {
                        auto close = line.find('>', p);
                        if (close != std::string::npos)
                            p = close + 1;
                        p = skip_ws(line, p);
                    } else if (starts_with_keyword(line, p, "static") ||
                               starts_with_keyword(line, p, "inline") ||
                               starts_with_keyword(line, p, "async")) {
                        size_t kw_len = starts_with_keyword(line, p, "static")   ? 6
                                        : starts_with_keyword(line, p, "inline") ? 6
                                                                                 : 5;
                        p = skip_ws(line, p + kw_len);
                    } else {
                        break;
                    }
                }
                // 型名を取得
                size_t type_start = p;
                while (p < line.size() && (std::isalnum(static_cast<unsigned char>(line[p])) ||
                                           line[p] == '_' || line[p] == '*'))
                    p++;
                if (p > type_start) {
                    p = skip_ws(line, p);
                    // 名前を取得
                    size_t name_start = p;
                    while (p < line.size() &&
                           (std::isalnum(static_cast<unsigned char>(line[p])) || line[p] == '_'))
                        p++;
                    if (p > name_start) {
                        current_export_name = line.substr(name_start, p - name_start);
                        matched = true;
                    }
                }
            }
        }

        if (matched) {
            // 指定されたアイテムかチェック
            bool is_wanted = std::find(import_items.begin(), import_items.end(),
                                       current_export_name) != import_items.end();

            if (is_wanted) {
                in_wanted_block = true;
            } else {
                in_unwanted_block = true;
            }

            block_lines.clear();
            block_lines.push_back(line);
            brace_depth = 0;
            found_opening_brace = false;

            // 開き括弧をチェック
            if (line.find('{') != std::string::npos) {
                found_opening_brace = true;
                // 括弧の深さを正確にカウント
                for (char c : line) {
                    if (c == '{')
                        brace_depth++;
                    else if (c == '}')
                        brace_depth--;
                }
            }

            // 1行で完結する場合（セミコロンで終わる宣言）
            if (!found_opening_brace && line.find(';') != std::string::npos) {
                if (in_wanted_block) {
                    for (const auto& block_line : block_lines) {
                        result << block_line << "\n";
                    }
                }
                in_wanted_block = false;
                in_unwanted_block = false;
                block_lines.clear();
            }
            // 1行で完結する場合（括弧が閉じている）
            else if (found_opening_brace && brace_depth == 0) {
                if (in_wanted_block) {
                    for (const auto& block_line : block_lines) {
                        result << block_line << "\n";
                    }
                }
                in_wanted_block = false;
                in_unwanted_block = false;
                block_lines.clear();
            }
        } else if (in_wanted_block || in_unwanted_block) {
            // エクスポートブロック内
            block_lines.push_back(line);

            // まだ開き括弧を見つけていない場合
            if (!found_opening_brace && line.find('{') != std::string::npos) {
                found_opening_brace = true;
            }

            if (!found_opening_brace && line.find(';') != std::string::npos) {
                if (in_wanted_block) {
                    for (const auto& block_line : block_lines) {
                        result << block_line << "\n";
                    }
                }
                in_wanted_block = false;
                in_unwanted_block = false;
                block_lines.clear();
            } else if (found_opening_brace) {
                for (char c : line) {
                    if (c == '{')
                        brace_depth++;
                    else if (c == '}')
                        brace_depth--;
                }

                // ブロックの終了を検出
                if (brace_depth == 0) {
                    // 欲しいブロックの場合のみ出力
                    if (in_wanted_block) {
                        for (const auto& block_line : block_lines) {
                            result << block_line << "\n";
                        }
                    }
                    in_wanted_block = false;
                    in_unwanted_block = false;
                    block_lines.clear();
                    found_opening_brace = false;
                }
            }
        } else if (line.find("export") == std::string::npos) {
            // エクスポートされていない行はそのまま保持（コメント、型定義など）
            result << line << "\n";
        }
    }

    return result.str();
}

std::string ImportPreprocessor::remove_export_keywords(const std::string& source) {
    // 先にexport構文を処理
    std::string processed = process_export_syntax(source);
    processed = process_namespace_exports(processed);

    // 階層再構築エクスポートを処理: export { ns::{item1, item2} }
    processed = process_hierarchical_reexport(processed);

    // Note: implの暗黙的エクスポートは現在無効化
    // パーサーが "export impl" をサポートしていないため
    // 将来的にはパーサーを修正してサポートする予定
    // processed = process_implicit_impl_export(processed);

    std::stringstream result;
    std::stringstream input(processed);
    std::string line;

    while (std::getline(input, line)) {
        // module 宣言を削除（namespace内で不要）
        if (line_starts_with(line, "module")) {
            // "module <name>;" パターンかチェック
            auto trimmed_line = line;
            while (!trimmed_line.empty() &&
                   (trimmed_line.back() == ' ' || trimmed_line.back() == '\t'))
                trimmed_line.pop_back();
            if (!trimmed_line.empty() && trimmed_line.back() == ';') {
                result << "// " << line << " (removed)\n";
                continue;
            }
        }

        // import 宣言もコメント化（既に処理済み）
        if (line_starts_with(line, "import")) {
            result << "// " << line << "\n";
            continue;
        }

        // ジェネリック関数の export <T> type name() から export を除去
        // パーサーは export <T> 構文を未サポートのため
        {
            size_t pos = skip_ws(line);
            if (starts_with_keyword(line, pos, "export")) {
                size_t after_export = pos + 6;
                size_t next = skip_ws(line, after_export);
                if (next < line.size() && line[next] == '<') {
                    // export <T> ... → <T> ...
                    line = line.substr(0, pos) + line.substr(next);
                }
            }
        }

        // Note: 通常のexportキーワードは保持する
        result << line << "\n";
    }

    return result.str();
}

std::string ImportPreprocessor::process_export_syntax(const std::string& source) {
    std::stringstream result;
    std::stringstream input(source);
    std::string line;
    std::vector<std::string> lines;

    // 全行を読み込む
    while (std::getline(input, line)) {
        lines.push_back(line);
    }

    // 定義を収集するためのマップ
    std::map<std::string, std::pair<int, std::string>>
        definitions;  // name -> (line_start, definition)
    std::set<std::string> exported_names;

    // Phase 1: 定義を収集
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& cur_line = lines[i];
        size_t pos = skip_ws(cur_line);

        // use libc { ... } ブロックを検出
        if (starts_with_keyword(cur_line, pos, "use")) {
            size_t after_use = skip_ws(cur_line, pos + 3);
            if (starts_with_keyword(cur_line, after_use, "libc") &&
                cur_line.find('{') != std::string::npos) {
                // ブロック全体を収集
                std::string def = cur_line;
                int brace_count = 1;
                size_t start_i = i;

                // use libc ブロック内の関数名も収集
                std::vector<std::string> ffi_func_names;

                for (size_t j = i + 1; j < lines.size() && brace_count > 0; ++j) {
                    def += "\n" + lines[j];
                    for (char c : lines[j]) {
                        if (c == '{')
                            brace_count++;
                        else if (c == '}')
                            brace_count--;
                    }

                    // 関数宣言を検出: 空白 関数名(
                    {
                        size_t fpos = skip_ws(lines[j]);
                        // 戻り型をスキップ
                        while (fpos < lines[j].size() &&
                               (std::isalnum(static_cast<unsigned char>(lines[j][fpos])) ||
                                lines[j][fpos] == '_'))
                            fpos++;
                        fpos = skip_ws(lines[j], fpos);
                        // 関数名を取得
                        size_t fname_start = fpos;
                        while (fpos < lines[j].size() &&
                               (std::isalnum(static_cast<unsigned char>(lines[j][fpos])) ||
                                lines[j][fpos] == '_'))
                            fpos++;
                        if (fpos > fname_start && fpos < lines[j].size() && lines[j][fpos] == '(') {
                            ffi_func_names.push_back(
                                lines[j].substr(fname_start, fpos - fname_start));
                        }
                    }

                    if (brace_count == 0) {
                        i = j;
                        break;
                    }
                }

                // 各FFI関数を定義として登録
                for (const auto& func_name : ffi_func_names) {
                    definitions[func_name] = {static_cast<int>(start_i), def};
                }
                continue;
            }
        }

        // export があれば除去して検査
        bool has_export = starts_with_keyword(cur_line, pos, "export");
        size_t decl_pos = has_export ? skip_ws(cur_line, pos + 6) : pos;

        // 関数定義を検出: 型名 関数名(
        // 簡易チェック: 識別子 空白 識別子 ( のパターン
        {
            size_t p = decl_pos;
            // 型名をスキップ
            size_t type_start = p;
            while (p < cur_line.size() &&
                   (std::isalnum(static_cast<unsigned char>(cur_line[p])) || cur_line[p] == '_'))
                p++;
            if (p > type_start) {
                size_t after_type = skip_ws(cur_line, p);
                // 関数名を取得
                size_t fname_start = after_type;
                while (after_type < cur_line.size() &&
                       (std::isalnum(static_cast<unsigned char>(cur_line[after_type])) ||
                        cur_line[after_type] == '_'))
                    after_type++;
                if (after_type > fname_start && after_type < cur_line.size() &&
                    cur_line[after_type] == '(') {
                    std::string name = cur_line.substr(fname_start, after_type - fname_start);
                    std::string def = cur_line;
                    int brace_count = 0;
                    size_t j = i;

                    // 関数本体を収集
                    for (; j < lines.size(); ++j) {
                        if (j > i)
                            def += "\n" + lines[j];
                        for (char c : lines[j]) {
                            if (c == '{')
                                brace_count++;
                            else if (c == '}') {
                                brace_count--;
                                if (brace_count == 0)
                                    break;
                            }
                        }
                        if (brace_count == 0 && lines[j].find('}') != std::string::npos)
                            break;
                    }

                    definitions[name] = {static_cast<int>(i), def};
                    i = j;  // スキップ
                    continue;
                }
            }
        }

        // 構造体定義を検出: [export] struct Name { または [export] extern struct Name {
        size_t struct_pos = decl_pos;
        if (starts_with_keyword(cur_line, struct_pos, "extern")) {
            struct_pos = skip_ws(cur_line, struct_pos + 6);
        }
        if (starts_with_keyword(cur_line, struct_pos, "struct")) {
            size_t after_struct = skip_ws(cur_line, struct_pos + 6);
            size_t sname_start = after_struct;
            while (after_struct < cur_line.size() &&
                   (std::isalnum(static_cast<unsigned char>(cur_line[after_struct])) ||
                    cur_line[after_struct] == '_'))
                after_struct++;
            if (after_struct > sname_start) {
                std::string name = cur_line.substr(sname_start, after_struct - sname_start);
                size_t brace_pos = cur_line.find('{', after_struct);
                if (brace_pos != std::string::npos) {
                    std::string def = cur_line;
                    int brace_count = 1;

                    // 構造体本体を収集
                    for (size_t j = i + 1; j < lines.size() && brace_count > 0; ++j) {
                        def += "\n" + lines[j];
                        for (char c : lines[j]) {
                            if (c == '{')
                                brace_count++;
                            else if (c == '}')
                                brace_count--;
                        }
                        if (brace_count == 0) {
                            i = j;
                            break;
                        }
                    }

                    definitions[name] = {static_cast<int>(i), def};
                }
            }
        }
    }

    // Phase 2: export { name1, name2, ... } および export { ns::{item1, item2} } を検出
    bool has_export_list = false;

    for (size_t i = 0; i < lines.size(); ++i) {
        // export { ... } パターンを文字列で検出
        size_t pos = skip_ws(lines[i]);
        if (!starts_with_keyword(lines[i], pos, "export"))
            continue;
        size_t after_export = skip_ws(lines[i], pos + 6);
        if (after_export >= lines[i].size() || lines[i][after_export] != '{')
            continue;

        size_t close_brace = lines[i].find('}', after_export + 1);
        if (close_brace == std::string::npos)
            continue;

        has_export_list = true;
        std::string names = lines[i].substr(after_export + 1, close_brace - after_export - 1);

        // 階層再構築パターンをチェック: ns::{item1, item2}
        size_t hier_pos = names.find("::{");
        if (hier_pos != std::string::npos) {
            // 名前空間名を抽出
            std::string before = names.substr(0, hier_pos);
            // 末尾のトリム
            size_t ns_end = before.find_last_not_of(" \t");
            // 先頭のトリム
            size_t ns_start = before.find_first_not_of(" \t");
            if (ns_start != std::string::npos && ns_end != std::string::npos) {
                std::string namespace_name = before.substr(ns_start, ns_end - ns_start + 1);
                size_t sub_close = names.find('}', hier_pos + 3);
                if (sub_close != std::string::npos) {
                    std::string sub_items = names.substr(hier_pos + 3, sub_close - hier_pos - 3);

                    // サブアイテムをパース
                    std::stringstream sub_ss(sub_items);
                    std::string sub_item;
                    while (std::getline(sub_ss, sub_item, ',')) {
                        sub_item.erase(0, sub_item.find_first_not_of(" \t\n\r"));
                        sub_item.erase(sub_item.find_last_not_of(" \t\n\r") + 1);
                        if (!sub_item.empty()) {
                            exported_names.insert(namespace_name + "::" + sub_item);
                        }
                    }
                }
            }
        } else {
            // 通常の名前リストをパース
            std::stringstream ss(names);
            std::string name;
            while (std::getline(ss, name, ',')) {
                name.erase(0, name.find_first_not_of(" \t\n\r"));
                name.erase(name.find_last_not_of(" \t\n\r") + 1);
                if (!name.empty()) {
                    exported_names.insert(name);
                }
            }
        }

        // export {...} 行をコメント化
        lines[i] = "// " + lines[i] + " (processed)";
    }

    // Phase 3: 出力を生成
    if (has_export_list) {
        // 名前列挙形式の場合、定義を再配置
        std::set<int> processed_lines;
        std::set<int> output_lines;  // 既に出力した行

        // エクスポートされた定義を先に出力
        for (const auto& name : exported_names) {
            if (definitions.count(name) > 0) {
                int line_num = definitions[name].first;
                // 同じ行番号の定義は一度だけ出力
                if (output_lines.count(line_num) == 0) {
                    result << definitions[name].second << "\n";
                    output_lines.insert(line_num);
                }
                processed_lines.insert(line_num);
            }
        }

        // その他の行を出力
        for (size_t i = 0; i < lines.size(); ++i) {
            if (processed_lines.count(i) == 0) {
                // 既に処理された定義はスキップ
                bool skip = false;
                for (const auto& [name, info] : definitions) {
                    if (info.first == static_cast<int>(i) && exported_names.count(name) > 0) {
                        skip = true;
                        break;
                    }
                }
                if (!skip) {
                    result << lines[i] << "\n";
                }
            }
        }
    } else {
        // 通常の形式はそのまま返す
        return source;
    }

    return result.str();
}

std::string ImportPreprocessor::process_namespace_exports(const std::string& source) {
    std::stringstream result;
    std::stringstream input(source);
    std::string line;
    bool in_namespace_export = false;
    std::string namespace_name;
    std::vector<std::string> namespace_content;
    int brace_depth = 0;

    while (std::getline(input, line)) {
        // export NS { ... } を検出（regexなし）
        if (!in_namespace_export) {
            size_t pos = skip_ws(line);
            bool matched_ns_export = false;
            if (starts_with_keyword(line, pos, "export")) {
                size_t after_export = skip_ws(line, pos + 6);
                // 名前を取得
                size_t name_start = after_export;
                while (after_export < line.size() &&
                       (std::isalnum(static_cast<unsigned char>(line[after_export])) ||
                        line[after_export] == '_'))
                    after_export++;
                if (after_export > name_start) {
                    size_t after_name = skip_ws(line, after_export);
                    if (after_name < line.size() && line[after_name] == '{') {
                        // サブ名前空間エクスポートの開始
                        namespace_name = line.substr(name_start, after_export - name_start);
                        in_namespace_export = true;
                        brace_depth = 1;
                        matched_ns_export = true;

                        // namespace宣言に変換
                        result << "namespace " << namespace_name << " {\n";

                        // 開き括弧の後の内容があればそれも処理
                        if (after_name + 1 < line.length()) {
                            std::string rest = line.substr(after_name + 1);
                            namespace_content.push_back(rest);
                        }
                    }
                }
            }
            if (!matched_ns_export) {
                // 通常の行
                result << line << "\n";
            }
        } else {
            // サブ名前空間内のコンテンツを収集
            for (char c : line) {
                if (c == '{')
                    brace_depth++;
                else if (c == '}') {
                    brace_depth--;
                    if (brace_depth == 0) {
                        // 名前空間の終了
                        in_namespace_export = false;

                        // 収集したコンテンツを出力
                        for (const auto& content_line : namespace_content) {
                            result << "    " << content_line << "\n";
                        }

                        // 閉じ括弧の前までを出力
                        size_t close_pos = line.find('}');
                        if (close_pos > 0) {
                            std::string before_close = line.substr(0, close_pos);
                            if (!before_close.empty()) {
                                result << "    " << before_close << "\n";
                            }
                        }

                        result << "} // namespace " << namespace_name << "\n";
                        namespace_content.clear();
                        break;
                    }
                }
            }

            if (in_namespace_export) {
                // まだ名前空間内
                namespace_content.push_back(line);
            }
        }
    }

    return result.str();
}

std::vector<std::string> ImportPreprocessor::extract_reexports(const std::string& module_source) {
    // export { M }; または export { M, N, ... }; 形式を検出
    std::vector<std::string> reexports;
    std::regex export_regex(R"(^\s*export\s*\{([^}]+)\}\s*;)");
    std::istringstream input(module_source);
    std::string line;

    while (std::getline(input, line)) {
        std::smatch match;
        if (std::regex_match(line, match, export_regex)) {
            // カンマで分割
            std::string items = match[1].str();
            std::stringstream ss(items);
            std::string item;
            while (std::getline(ss, item, ',')) {
                // 前後の空白を削除
                item.erase(0, item.find_first_not_of(" \t\n\r"));
                item.erase(item.find_last_not_of(" \t\n\r") + 1);
                if (!item.empty()) {
                    reexports.push_back(item);
                }
            }
        }
    }

    return reexports;
}

std::string ImportPreprocessor::process_implicit_impl_export(const std::string& source) {
    // exportされた構造体を検出
    std::set<std::string> exported_structs;

    std::regex export_struct_regex(R"(export\s+struct\s+(\w+))");
    std::smatch match;
    std::string::const_iterator search_start = source.cbegin();
    while (std::regex_search(search_start, source.cend(), match, export_struct_regex)) {
        exported_structs.insert(match[1].str());
        search_start = match.suffix().first;
    }

    if (exported_structs.empty()) {
        return source;  // エクスポートされた構造体がない場合はそのまま返す
    }

    // implを検出して、エクスポートされた構造体のimplにexportを追加
    std::stringstream result;
    std::istringstream input(source);
    std::string line;

    // impl Type for Interface パターン
    std::regex impl_regex(R"(^(\s*)impl\s+(\w+)\s+for\s+(\w+))");
    // impl Type パターン（コンストラクタ用）
    std::regex impl_ctor_regex(R"(^(\s*)impl\s+(\w+)\s*\{)");

    while (std::getline(input, line)) {
        std::smatch impl_match;

        // impl Type for Interface をチェック
        if (std::regex_search(line, impl_match, impl_regex)) {
            std::string type_name = impl_match[2].str();
            // エクスポートされた構造体のimplの場合、exportキーワードを追加
            // （まだexportキーワードがない場合のみ）
            if (exported_structs.count(type_name) > 0 && line.find("export") == std::string::npos) {
                std::string indent = impl_match[1].str();
                line = indent + "export " + line.substr(indent.length());
            }
        }
        // impl Type { (コンストラクタ用) をチェック
        else if (std::regex_search(line, impl_match, impl_ctor_regex)) {
            std::string type_name = impl_match[2].str();
            if (exported_structs.count(type_name) > 0 && line.find("export") == std::string::npos) {
                std::string indent = impl_match[1].str();
                line = indent + "export " + line.substr(indent.length());
            }
        }

        result << line << "\n";
    }

    return result.str();
}

std::string ImportPreprocessor::process_hierarchical_reexport(const std::string& source) {
    // export { ns::{item1, item2} } パターンを検出
    // 例: export { io::{file, stream} }
    // これは、fileとstreamの名前空間をio名前空間内に移動する

    // コメント化された形式: // export { io::{file, stream} }; (processed)
    std::regex hier_export_regex(R"(//\s*export\s*\{\s*(\w+)::\{([^}]+)\}\s*\};\s*\(processed\))");
    std::smatch match;

    if (!std::regex_search(source, match, hier_export_regex)) {
        return source;  // 階層再構築パターンがない場合はそのまま返す
    }

    std::string parent_ns = match[1].str();  // 例: "io"
    std::string items_str = match[2].str();  // 例: "file, stream"

    // アイテムをパース
    std::set<std::string> items_to_move;
    std::stringstream items_ss(items_str);
    std::string item;
    while (std::getline(items_ss, item, ',')) {
        // トリム
        item.erase(0, item.find_first_not_of(" \t\n\r"));
        item.erase(item.find_last_not_of(" \t\n\r") + 1);
        if (!item.empty()) {
            items_to_move.insert(item);
        }
    }

    if (items_to_move.empty()) {
        return source;
    }

    // 各アイテムの名前空間を抽出して、親名前空間内に配置
    std::stringstream result;
    std::istringstream input(source);
    std::string line;

    // 移動する名前空間の内容を収集
    std::map<std::string, std::string> namespace_contents;
    std::string current_ns;
    std::stringstream current_content;
    int brace_depth = 0;
    bool in_target_ns = false;

    // Pass 1: 対象の名前空間を収集
    while (std::getline(input, line)) {
        std::regex ns_start_regex(R"(^\s*namespace\s+(\w+)\s*\{)");
        std::smatch ns_match;

        if (!in_target_ns && std::regex_search(line, ns_match, ns_start_regex)) {
            std::string ns_name = ns_match[1].str();
            if (items_to_move.count(ns_name) > 0) {
                current_ns = ns_name;
                in_target_ns = true;
                brace_depth = 1;
                current_content.str("");
                current_content.clear();
                continue;  // namespace行自体はスキップ
            }
        }

        if (in_target_ns) {
            for (char c : line) {
                if (c == '{')
                    brace_depth++;
                else if (c == '}')
                    brace_depth--;
            }

            if (brace_depth == 0) {
                // 名前空間の終了（閉じ括弧の行は含めない）
                namespace_contents[current_ns] = current_content.str();
                in_target_ns = false;
                current_ns.clear();
            } else {
                current_content << line << "\n";
            }
        }
    }

    // Pass 2: 出力を生成
    input.clear();
    input.str(source);
    in_target_ns = false;
    brace_depth = 0;
    // bool parent_ns_opened = false;
    bool items_inserted = false;

    while (std::getline(input, line)) {
        std::regex ns_start_regex(R"(^\s*namespace\s+(\w+)\s*\{)");
        std::smatch ns_match;

        // 対象の名前空間をスキップ
        if (!in_target_ns && std::regex_search(line, ns_match, ns_start_regex)) {
            std::string ns_name = ns_match[1].str();
            if (items_to_move.count(ns_name) > 0) {
                in_target_ns = true;
                brace_depth = 1;
                current_ns = ns_name;
                continue;
            }
        }

        if (in_target_ns) {
            for (char c : line) {
                if (c == '{')
                    brace_depth++;
                else if (c == '}')
                    brace_depth--;
            }

            if (brace_depth == 0) {
                in_target_ns = false;
                current_ns.clear();
            }
            continue;  // 対象の名前空間は出力しない
        }

        // export コメントの位置で親名前空間と内容を挿入
        if (!items_inserted && line.find("(processed)") != std::string::npos &&
            line.find(parent_ns + "::") != std::string::npos) {
            result << "namespace " << parent_ns << " {\n";
            for (const auto& [ns_name, content] : namespace_contents) {
                result << "namespace " << ns_name << " {\n";
                result << content;
                result << "} // namespace " << ns_name << "\n";
            }
            result << "} // namespace " << parent_ns << "\n";
            result << "// " << line << "\n";  // 元のコメントもコメントとして保持
            items_inserted = true;
            continue;
        }

        result << line << "\n";
    }

    return result.str();
}

// exportされたブロック（関数・struct・const等）をモジュールソースから抽出する
// namespace外へのforward展開用: namespaceラップされたモジュールのexportシンボルを
// namespace外にも出力して、名前空間修飾なしで呼び出し可能にする
std::string cm::preprocessor::ImportPreprocessor::extract_exported_blocks(
    const std::string& module_source) {
    std::stringstream result;
    std::stringstream non_export_result;  // 非export定義を格納
    std::stringstream input(module_source);
    std::string line;
    bool in_export_block = false;
    bool in_sub_exported_section = false;
    bool in_non_export_block = false;  // 非exportブロック内
    std::vector<std::string> block_lines;
    int brace_depth = 0;
    bool found_opening_brace = false;
    bool has_export_blocks = false;  // export関数が存在するか

    while (std::getline(input, line)) {
        // サブモジュールのExported symbolsセクションを検出してパススルー
        // これにより推移的なエクスポートが可能になる
        // （モジュールAがBをimport、BがCをimport → AからCのexport関数を呼べる）
        if (!in_export_block && !in_sub_exported_section && !in_non_export_block &&
            line.find("// ===== Exported symbols from ") != std::string::npos) {
            in_sub_exported_section = true;
            continue;  // セクション開始コメントはスキップ
        }
        if (in_sub_exported_section) {
            if (line.find("// ===== End exported symbols =====") != std::string::npos) {
                in_sub_exported_section = false;
                continue;  // セクション終了コメントはスキップ
            }
            // サブモジュールのexported関数をそのまま出力
            result << line << "\n";
            continue;
        }

        // module宣言やimport文はスキップ
        if (line.find("module ") != std::string::npos && line.find(';') != std::string::npos) {
            std::regex module_regex(R"(^\s*/?\s*module\s+[\w:.]+\s*;\s*$)");
            if (std::regex_match(line, module_regex)) {
                continue;
            }
        }
        if (line.find("import ") != std::string::npos) {
            std::regex import_regex(R"(^\s*import\s+)");
            if (std::regex_search(line, import_regex)) {
                continue;
            }
        }

        // exportで始まる行を検出（シンプルなパターン）
        if (!in_export_block && !in_non_export_block && line.find("export ") != std::string::npos) {
            // 行がexportで始まるか確認（先頭空白は許容）
            std::regex export_start(R"(^\s*export\s+)");
            if (std::regex_search(line, export_start)) {
                // 再エクスポート構文（export { ... }）はスキップ
                // export NAME1, NAME2; 形式のリストエクスポートもスキップ
                std::regex reexport_regex(R"(^\s*export\s*\{)");
                std::regex list_export_regex(R"(^\s*export\s+\w+\s*,)");
                std::regex name_only_export_regex(R"(^\s*export\s+\w+\s*;)");
                if (std::regex_search(line, reexport_regex) ||
                    std::regex_search(line, list_export_regex) ||
                    std::regex_search(line, name_only_export_regex)) {
                    continue;
                }
                in_export_block = true;
                has_export_blocks = true;
                block_lines.clear();
                block_lines.push_back(line);
                brace_depth = 0;
                found_opening_brace = false;

                // 開き括弧をチェック
                for (char c : line) {
                    if (c == '{') {
                        found_opening_brace = true;
                        brace_depth++;
                    } else if (c == '}') {
                        brace_depth--;
                    }
                }

                // 1行で完結する場合（セミコロンで終わる宣言、括弧なし）
                if (!found_opening_brace && line.find(';') != std::string::npos) {
                    for (auto& bl : block_lines) {
                        std::regex rm_export(R"(\bexport\s+)");
                        result << std::regex_replace(bl, rm_export, "") << "\n";
                    }
                    in_export_block = false;
                    block_lines.clear();
                }
                // 1行で括弧が閉じている場合
                else if (found_opening_brace && brace_depth == 0) {
                    for (auto& bl : block_lines) {
                        std::regex rm_export(R"(\bexport\s+)");
                        result << std::regex_replace(bl, rm_export, "") << "\n";
                    }
                    in_export_block = false;
                    block_lines.clear();
                }

                continue;
            }
        }

        if (in_export_block) {
            block_lines.push_back(line);

            if (!found_opening_brace && line.find('{') != std::string::npos) {
                found_opening_brace = true;
            }

            if (!found_opening_brace && line.find(';') != std::string::npos) {
                // exportキーワードを除去して出力
                for (auto& bl : block_lines) {
                    std::regex rm_export(R"(\bexport\s+)");
                    result << std::regex_replace(bl, rm_export, "") << "\n";
                }
                in_export_block = false;
                block_lines.clear();
            } else if (found_opening_brace) {
                for (char c : line) {
                    if (c == '{')
                        brace_depth++;
                    else if (c == '}')
                        brace_depth--;
                }
                if (brace_depth == 0) {
                    // exportキーワードを除去して出力
                    for (auto& bl : block_lines) {
                        std::regex rm_export(R"(\bexport\s+)");
                        result << std::regex_replace(bl, rm_export, "") << "\n";
                    }
                    in_export_block = false;
                    block_lines.clear();
                    found_opening_brace = false;
                }
            }
            continue;
        }

        // Bug#15修正: 非export関数定義のみ収集
        // export関数から呼ばれる内部ヘルパー関数をnamespace外でも参照可能にする
        // 注意: struct/impl/interfaceは重複定義を避けるため除外
        if (!in_non_export_block) {
            size_t pos = skip_ws(line);
            bool is_definition = false;

            // キーワードブロック（interface/struct/impl/enum等）内の行はスキップ
            // これらのブロック内の宣言（void print(); 等）は関数定義ではない
            if (pos < line.size() && line[pos] != '/' && line[pos] != '#') {
                bool is_excluded_block = starts_with_keyword(line, pos, "impl") ||
                                         starts_with_keyword(line, pos, "struct") ||
                                         starts_with_keyword(line, pos, "interface") ||
                                         starts_with_keyword(line, pos, "typedef") ||
                                         starts_with_keyword(line, pos, "enum");
                if (is_excluded_block) {
                    // ブロック全体をスキップ
                    int skip_depth = 0;
                    bool skip_found_brace = false;
                    for (char c : line) {
                        if (c == '{') {
                            skip_found_brace = true;
                            skip_depth++;
                        } else if (c == '}') {
                            skip_depth--;
                        }
                    }
                    // 1行で閉じていない場合、ブロック終了まで読み進める
                    if (skip_found_brace && skip_depth > 0) {
                        while (std::getline(input, line)) {
                            for (char c : line) {
                                if (c == '{')
                                    skip_depth++;
                                else if (c == '}')
                                    skip_depth--;
                            }
                            if (skip_depth <= 0)
                                break;
                        }
                    } else if (!skip_found_brace && line.find('{') == std::string::npos) {
                        // 括弧なし宣言: 次の行に括弧がある可能性（struct Name\n{）
                        // セミコロンで終わるなら1行宣言
                        if (line.find(';') == std::string::npos) {
                            // 次の行に { があるかチェック
                            // この場合は安全のためスキップしない
                        }
                    }
                    continue;
                }

                // namespace行やclosing braceもスキップ
                if (starts_with_keyword(line, pos, "namespace") ||
                    starts_with_keyword(line, pos, "const") ||
                    starts_with_keyword(line, pos, "}") || starts_with_keyword(line, pos, "use")) {
                    continue;
                }

                // 関数定義: type name( パターンのみ
                size_t p = pos;
                // 型名チェック（識別子 + オプションのポインタ修飾子）
                size_t type_start = p;
                while (p < line.size() && (std::isalnum(static_cast<unsigned char>(line[p])) ||
                                           line[p] == '_' || line[p] == '*'))
                    p++;
                if (p > type_start) {
                    p = skip_ws(line, p);
                    // 関数名チェック
                    size_t fname_start = p;
                    while (p < line.size() &&
                           (std::isalnum(static_cast<unsigned char>(line[p])) || line[p] == '_'))
                        p++;
                    if (p > fname_start && p < line.size() && line[p] == '(') {
                        // efi_main は通常エントリポイントなので除外
                        std::string fname = line.substr(fname_start, p - fname_start);
                        if (fname != "efi_main" && fname != "main") {
                            is_definition = true;
                        }
                    }
                }
            }

            if (is_definition) {
                in_non_export_block = true;
                block_lines.clear();
                block_lines.push_back(line);
                brace_depth = 0;
                found_opening_brace = false;

                for (char c : line) {
                    if (c == '{') {
                        found_opening_brace = true;
                        brace_depth++;
                    } else if (c == '}') {
                        brace_depth--;
                    }
                }

                // 1行完結（セミコロン終了、括弧なし）
                if (!found_opening_brace && line.find(';') != std::string::npos) {
                    for (const auto& bl : block_lines) {
                        non_export_result << bl << "\n";
                    }
                    in_non_export_block = false;
                    block_lines.clear();
                }
                // 1行で括弧が閉じている場合
                else if (found_opening_brace && brace_depth == 0) {
                    for (const auto& bl : block_lines) {
                        non_export_result << bl << "\n";
                    }
                    in_non_export_block = false;
                    block_lines.clear();
                }
                continue;
            }
        }

        if (in_non_export_block) {
            block_lines.push_back(line);

            if (!found_opening_brace && line.find('{') != std::string::npos) {
                found_opening_brace = true;
            }

            if (!found_opening_brace && line.find(';') != std::string::npos) {
                for (const auto& bl : block_lines) {
                    non_export_result << bl << "\n";
                }
                in_non_export_block = false;
                block_lines.clear();
            } else if (found_opening_brace) {
                for (char c : line) {
                    if (c == '{')
                        brace_depth++;
                    else if (c == '}')
                        brace_depth--;
                }
                if (brace_depth == 0) {
                    for (const auto& bl : block_lines) {
                        non_export_result << bl << "\n";
                    }
                    in_non_export_block = false;
                    block_lines.clear();
                    found_opening_brace = false;
                }
            }
            continue;
        }
    }

    // export関数が存在する場合のみ、非export定義も含める
    // （内部ヘルパー関数がexport関数から参照される可能性がある）
    std::string exported = result.str();
    if (has_export_blocks) {
        std::string non_exported = non_export_result.str();
        if (!non_exported.empty()) {
            exported = non_exported + exported;
        }
    }

    return exported;
}

}  // namespace cm::preprocessor
