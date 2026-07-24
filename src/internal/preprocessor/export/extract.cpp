// ============================================================
// importプリプロセッサ - exportブロックの抽出・選択的インポートのフィルタリング・再exportリスト検出
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

std::string ImportPreprocessor::filter_exports(const std::string& module_source,
                                               const std::vector<std::string>& import_items,
                                               bool incremental) {
    // 選択的インポート：指定されたアイテムのみを抽出（行単位解析のため複数行 export { ... } は先に1行へ正規化する）
    std::stringstream result;
    std::stringstream input(normalize_export_blocks(module_source));
    std::string line;
    bool in_wanted_block = false;    // 欲しいエクスポートブロック内
    bool in_unwanted_block = false;  // 不要なエクスポートブロック内
    std::string current_export_name;
    std::vector<std::string> block_lines;
    int brace_depth = 0;
    bool found_opening_brace = false;

    // 事前スキャン: フィルタ後も出力に残る型名（非exportのstruct/enum）を集める。
    // 透過的インポート（別モジュール経由でlibを取り込む）で展開された impl は、対象の型が残る限り
    // 一緒に残さないと、型は使えるのに .method() が解決できなくなる（web::html の impl Html 等）。
    std::set<std::string> kept_types(import_items.begin(), import_items.end());
    // 増分モード（同一ファイルからの2回目以降の選択import）では、非export型は初回展開で
    // 出力済みのため透過保持の事前スキャンを行わず、新規要求シンボルの型のみをimpl保持対象にする
    if (!incremental) {
        std::stringstream scan(normalize_export_blocks(module_source));
        std::string sline;
        const char* type_kws[] = {"struct", "enum"};
        while (std::getline(scan, sline)) {
            size_t sp = skip_ws(sline);
            if (starts_with_keyword(sline, sp, "export"))
                continue;  // exportされた型は選択対象なので import_items 側の判定に委ねる
            for (const char* kw : type_kws) {
                size_t kw_len = std::string(kw).size();
                if (starts_with_keyword(sline, sp, kw)) {
                    size_t np = skip_ws(sline, sp + kw_len);
                    size_t ns = np;
                    while (
                        np < sline.size() &&
                        (std::isalnum(static_cast<unsigned char>(sline[np])) || sline[np] == '_'))
                        np++;
                    if (np > ns)
                        kept_types.insert(sline.substr(ns, np - ns));
                    break;
                }
            }
        }
    }

    // 透過的インポート領域の深さ。ネストした import 展開（別モジュール経由で取り込んだlib等）は
    // このモジュール自身のexportではないので、選択フィルタの対象にせずマーカー内を丸ごと通す。
    // これをしないと、取り込んだexport関数/structが「未指定のexport」とみなされ落ちてしまう。
    int nested_import_depth = 0;
    auto contains = [](const std::string& s, const char* sub) {
        return s.find(sub) != std::string::npos;
    };

    while (std::getline(input, line)) {
        // ネストしたインポート展開のマーカーを検出して領域の内側を素通しにする
        if (contains(line, "===== Wildcard import from") ||
            contains(line, "===== Selective import from") ||
            contains(line, "===== Begin module:")) {
            // 増分モードではネストimport領域は初回展開で出力済みのため再出力しない
            if (!incremental)
                result << line << "\n";
            nested_import_depth++;
            continue;
        }
        if (contains(line, "===== End wildcard import") ||
            contains(line, "===== End selective import") || contains(line, "===== End module:")) {
            if (nested_import_depth > 0)
                nested_import_depth--;
            if (!incremental)
                result << line << "\n";
            continue;
        }
        if (nested_import_depth > 0) {
            // 透過的に取り込んだ内容はそのまま残す（exportの取捨選択をしない）
            if (!incremental)
                result << line << "\n";
            continue;
        }

        // エクスポートされた関数/構造体/定数/implを検出（regexなし）
        bool matched = false;
        bool matched_is_impl = false;
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
                    matched_is_impl = true;
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
                // ジェネリック型引数（Result<string, string> 等）をスキップ
                if (p > type_start && p < line.size() && line[p] == '<') {
                    int depth = 0;
                    while (p < line.size()) {
                        if (line[p] == '<')
                            depth++;
                        else if (line[p] == '>' && --depth == 0) {
                            p++;
                            break;
                        }
                        p++;
                    }
                    // ポインタ修飾（Result<T,E>* 等）
                    while (p < line.size() && line[p] == '*')
                        p++;
                }
                // 配列戻り値型の後置サフィックス（Point[] / int[3] 等）をスキップする。
                // これを扱わないと export Point[] all() の型名解析が [ で止まり、export関数が抽出漏れして落ちる
                while (p < line.size() && line[p] == '[') {
                    auto close = line.find(']', p);
                    if (close == std::string::npos)
                        break;
                    p = close + 1;
                }
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
            // impl は対象型が出力に残るなら（透過的インポート由来でも）一緒に残す
            if (!is_wanted && matched_is_impl && kept_types.count(current_export_name) > 0)
                is_wanted = true;

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
            // エクスポートされていない行はそのまま保持（コメント、型定義など）。
            // 増分モードでは初回展開で出力済みのため再出力しない
            if (!incremental)
                result << line << "\n";
        }
    }

    return result.str();
}

std::vector<std::string> ImportPreprocessor::extract_reexports(const std::string& module_source) {
    // export { M }; または export { M, N, ... }; 形式を検出
    std::vector<std::string> reexports;
    std::regex export_regex(R"(^\s*export\s*\{([^}]+)\}\s*;)");
    // 行単位解析のため複数行 export { ... } は先に1行へ正規化する
    std::istringstream input(normalize_export_blocks(module_source));
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

// exportされたブロック（関数・struct・const等）をモジュールソースから抽出する
// namespace外へのforward展開用: namespaceラップされたモジュールのexportシンボルをnamespace外にも出力して、名前空間修飾なしで呼び出し可能にする
std::string cm::preprocessor::ImportPreprocessor::extract_exported_blocks(
    const std::string& module_source) {
    // 行単位解析のため複数行 export { ... } は先に1行へ正規化する
    std::stringstream result;
    std::stringstream non_export_result;  // 非export定義を格納
    std::stringstream input(normalize_export_blocks(module_source));
    std::string line;
    bool in_export_block = false;
    bool in_sub_exported_section = false;
    bool in_non_export_block = false;  // 非exportブロック内
    std::vector<std::string> block_lines;
    int brace_depth = 0;
    bool found_opening_brace = false;
    bool has_export_functions = false;  // export関数が存在するか（ヘルパー複製の要否判定）

    while (std::getline(input, line)) {
        // サブモジュールのExported symbolsセクションを検出してパススルー
        // これにより推移的なエクスポートが可能になる（モジュールAがBをimport、BがCをimport → AからCのexport関数を呼べる）
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
                // export関数か判定（struct/enum等の型exportではヘルパー関数の複製は不要。
                // 型のみexportするモジュールで非export関数が名前空間外へ複製されると、モジュール内グローバル変数への参照が名前空間外で解決できず壊れるため）
                {
                    std::regex nonfunc_export(
                        R"(^\s*export\s+(struct|enum|interface|typedef|impl|extern|const)\b)");
                    if (!std::regex_search(line, nonfunc_export) &&
                        code_portion(line).find('(') != std::string::npos) {
                        has_export_functions = true;
                    }
                }
                block_lines.clear();
                block_lines.push_back(line);
                brace_depth = 0;
                found_opening_brace = false;

                // 開き括弧をチェック（コメント・文字列リテラル内は除外。
                // 除外しないと複数行の配列初期化子がコメント内の ; や { で途中終了し、export再宣言が先頭行のみに切り詰められる）
                std::string code_line = code_portion(line);
                for (char c : code_line) {
                    if (c == '{') {
                        found_opening_brace = true;
                        brace_depth++;
                    } else if (c == '}') {
                        brace_depth--;
                    }
                }

                // 1行で完結する場合（セミコロンで終わる宣言、括弧なし）
                if (!found_opening_brace && code_line.find(';') != std::string::npos) {
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

            // コメント・文字列リテラル内の ; や括弧で誤終了しないようコード部のみ走査する
            std::string code_line = code_portion(line);
            if (!found_opening_brace && code_line.find('{') != std::string::npos) {
                found_opening_brace = true;
            }

            if (!found_opening_brace && code_line.find(';') != std::string::npos) {
                // exportキーワードを除去して出力
                for (auto& bl : block_lines) {
                    std::regex rm_export(R"(\bexport\s+)");
                    result << std::regex_replace(bl, rm_export, "") << "\n";
                }
                in_export_block = false;
                block_lines.clear();
            } else if (found_opening_brace) {
                for (char c : code_line) {
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
                // use "pkg" { 宣言... } / use js { ... } のFFIブロックもブロック全体をスキップする。
                // 行だけスキップするとブロック本体の宣言と閉じ括弧が非exportコードとして誤処理され、ブレース深度がずれて後続のexport定義が壊れる
                bool is_excluded_block = starts_with_keyword(line, pos, "impl") ||
                                         starts_with_keyword(line, pos, "struct") ||
                                         starts_with_keyword(line, pos, "interface") ||
                                         starts_with_keyword(line, pos, "typedef") ||
                                         starts_with_keyword(line, pos, "enum") ||
                                         starts_with_keyword(line, pos, "use");
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

    // export関数が存在する場合のみ、非export定義も含める（内部ヘルパー関数がexport関数から参照される可能性がある）
    std::string exported = result.str();
    if (has_export_functions) {
        std::string non_exported = non_export_result.str();
        if (!non_exported.empty()) {
            exported = non_exported + exported;
        }
    }

    return exported;
}

}  // namespace cm::preprocessor
