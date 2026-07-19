// ============================================================
// importプリプロセッサ - export構文の書き換え（キーワード除去・namespace変換・階層再export処理）
// ============================================================

#include "internal/preprocessor/import.hpp"
#include "internal/preprocessor/import_internal.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace cm::preprocessor {

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
    // 行単位解析のため複数行 export { ... } は先に1行へ正規化する
    std::stringstream result;
    std::stringstream input(normalize_export_blocks(source));
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
                    // 宣言行自身の括弧も数える（1行で閉じる構造体に対応）
                    int brace_count = 0;
                    for (size_t k = brace_pos; k < cur_line.size(); ++k) {
                        if (cur_line[k] == '{')
                            brace_count++;
                        else if (cur_line[k] == '}')
                            brace_count--;
                    }
                    // 開始行を記録する（終了行を記録するとPhase 3の処理済みマークが本体行とずれ、重複・欠落出力になる）
                    size_t start_i = i;

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

                    definitions[name] = {static_cast<int>(start_i), def};
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
                    // export リストに載った定義は export キーワード付きで出力する（名前空間内の非export関数は外部から参照できないため）
                    const std::string& def_text = definitions[name].second;
                    size_t def_pos = skip_ws(def_text);
                    if (!starts_with_keyword(def_text, def_pos, "export")) {
                        result << "export ";
                    }
                    result << def_text << "\n";
                    output_lines.insert(line_num);
                }
                // 定義の全行を処理済みにする（開始行だけを記録すると
                // 複数行定義の本体が「その他の行」として重複出力される）
                const std::string& def = definitions[name].second;
                int def_line_count = 1 + static_cast<int>(std::count(def.begin(), def.end(), '\n'));
                for (int k = 0; k < def_line_count; ++k) {
                    processed_lines.insert(line_num + k);
                }
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
            // エクスポートされた構造体のimplの場合、exportキーワードを追加（まだexportキーワードがない場合のみ）
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

}  // namespace cm::preprocessor
