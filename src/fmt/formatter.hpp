#pragma once

// ============================================================
// Formatter - コスメティック整形
// ============================================================
// K&Rスタイル、インデント正規化、空白整理

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace cm {
namespace fmt {

/// フォーマット結果
struct FormatResult {
    std::string formatted_code;
    bool modified = false;
    size_t changes_applied = 0;
};

/// Formatter - コスメティック整形（K&Rスタイル）
class Formatter {
   public:
    Formatter() = default;

    /// インデント幅を設定
    void set_indent_width(int width) { indent_width_ = width; }

    /// コードをフォーマット
    FormatResult format(const std::string& original_code) {
        FormatResult result;
        std::string code = original_code;
        size_t changes = 0;

        // 1. 行末の空白を削除
        code = trim_trailing_whitespace(code, changes);

        // 2. タブをスペースに変換
        code = tabs_to_spaces(code, changes);

        // 3. 連続空行を1行に制限
        code = normalize_blank_lines(code, changes);

        // 4. K&Rスタイル: 開き波括弧を同一行に
        code = enforce_kr_braces(code, changes);

        // 5. インデント正規化
        code = normalize_indentation(code, changes);

        // 6. 演算子周りの空白
        code = normalize_operator_spacing(code, changes);

        result.formatted_code = code;
        result.modified = (code != original_code);
        result.changes_applied = changes;

        return result;
    }

    /// ファイルをフォーマットして上書き
    bool format_file(const std::string& filepath) {
        std::ifstream ifs(filepath);
        if (!ifs) {
            std::cerr << "エラー: ファイルを読み込めません: " << filepath << "\n";
            return false;
        }

        std::stringstream buffer;
        buffer << ifs.rdbuf();
        std::string original = buffer.str();
        ifs.close();

        auto result = format(original);

        if (result.modified) {
            std::ofstream ofs(filepath);
            if (!ofs) {
                std::cerr << "エラー: ファイルを書き込めません: " << filepath << "\n";
                return false;
            }
            ofs << result.formatted_code;
            return true;
        }

        return false;  // 変更なし
    }

    /// 修正サマリーを表示
    void print_summary(const FormatResult& result, std::ostream& out = std::cout) const {
        if (result.changes_applied > 0) {
            out << "✓ " << result.changes_applied << " 箇所のフォーマット修正\n";
        }
    }

   private:
    int indent_width_ = 4;

    /// 行末の空白を削除
    std::string trim_trailing_whitespace(const std::string& code, size_t& changes) {
        std::istringstream stream(code);
        std::ostringstream result;
        std::string line;
        bool first = true;

        while (std::getline(stream, line)) {
            if (!first)
                result << '\n';
            first = false;

            // 行末の空白を削除
            size_t end = line.find_last_not_of(" \t\r");
            if (end != std::string::npos) {
                std::string trimmed = line.substr(0, end + 1);
                if (trimmed != line) {
                    changes++;
                }
                result << trimmed;
            } else if (!line.empty()) {
                // 空白のみの行
                changes++;
            }
        }

        return result.str();
    }

    /// タブをスペースに変換
    std::string tabs_to_spaces(const std::string& code, size_t& changes) {
        std::string result;
        result.reserve(code.size());

        for (char c : code) {
            if (c == '\t') {
                result += std::string(indent_width_, ' ');
                changes++;
            } else {
                result += c;
            }
        }

        return result;
    }

    /// 連続空行を1行に制限
    std::string normalize_blank_lines(const std::string& code, size_t& changes) {
        std::istringstream stream(code);
        std::ostringstream result;
        std::string line;
        int blank_count = 0;
        bool first = true;

        while (std::getline(stream, line)) {
            bool is_blank = line.find_first_not_of(" \t\r") == std::string::npos;

            if (is_blank) {
                blank_count++;
                if (blank_count <= 1) {
                    if (!first)
                        result << '\n';
                    first = false;
                    result << "";
                } else {
                    changes++;  // 余分な空行を削除
                }
            } else {
                blank_count = 0;
                if (!first)
                    result << '\n';
                first = false;
                result << line;
            }
        }

        return result.str();
    }

    /// K&Rスタイル: 開き波括弧を同一行に
    std::string enforce_kr_braces(const std::string& code, size_t& changes) {
        std::istringstream stream(code);
        std::vector<std::string> lines;
        std::string line;

        // 全行を読み込み
        while (std::getline(stream, line)) {
            lines.push_back(line);
        }

        std::ostringstream result;

        for (size_t i = 0; i < lines.size(); ++i) {
            std::string& curr = lines[i];

            // 次の行が "{" のみかチェック
            if (i + 1 < lines.size()) {
                std::string next_trimmed = lines[i + 1];
                size_t start = next_trimmed.find_first_not_of(" \t");
                if (start != std::string::npos) {
                    next_trimmed = next_trimmed.substr(start);
                }

                // 次の行が "{" のみで、現在行が空でない場合
                if (next_trimmed == "{") {
                    // 現在行の末尾の空白を削除
                    size_t end = curr.find_last_not_of(" \t\r");
                    if (end != std::string::npos) {
                        curr = curr.substr(0, end + 1);
                    }

                    // 現在行に " {" を追加
                    result << curr << " {\n";
                    changes++;
                    i++;  // 次の行をスキップ
                    continue;
                }
            }

            // 通常の行を出力
            result << curr;
            if (i + 1 < lines.size()) {
                result << '\n';
            }
        }

        return result.str();
    }

    /// インデント正規化（4スペース単位）
    std::string normalize_indentation(const std::string& code, size_t& changes) {
        std::istringstream stream(code);
        std::ostringstream result;
        std::string line;
        bool first = true;

        while (std::getline(stream, line)) {
            if (!first)
                result << '\n';
            first = false;

            // 空行はそのまま
            if (line.find_first_not_of(" \t") == std::string::npos) {
                result << "";
                continue;
            }

            // 現在のインデントを計算
            size_t indent = 0;
            for (char c : line) {
                if (c == ' ')
                    indent++;
                else if (c == '\t')
                    indent += indent_width_;
                else
                    break;
            }

            // インデントレベルを計算（4スペース単位に丸める）
            size_t level = indent / indent_width_;
            size_t normalized_indent = level * indent_width_;

            // 行の内容を取得
            size_t content_start = line.find_first_not_of(" \t");
            std::string content =
                (content_start != std::string::npos) ? line.substr(content_start) : "";

            // 正規化されたインデントで出力
            std::string new_line = std::string(normalized_indent, ' ') + content;
            if (new_line != line) {
                changes++;
            }
            result << new_line;
        }

        return result.str();
    }

    /// 演算子周りの空白正規化
    std::string normalize_operator_spacing(const std::string& code, size_t& changes) {
        // シンプルな実装: 基本的な演算子のみ
        // より複雑な実装は文字列リテラルやコメントを考慮する必要がある
        // 現時点ではスキップ（安全のため）
        (void)changes;
        return code;
    }
};

}  // namespace fmt
}  // namespace cm
