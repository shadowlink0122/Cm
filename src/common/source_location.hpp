#pragma once

#include "span.hpp"

#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace cm {

/// ソースコードの位置情報を管理するクラス
class SourceLocationManager {
   public:
    explicit SourceLocationManager(const std::string& source_code,
                                   const std::string& filename = "");

    /// バイトオフセットから行・列情報を取得
    LineColumn get_line_column(uint32_t offset) const;

    /// 指定した行のテキストを取得
    std::string get_line(uint32_t line_num) const;

    /// エラー位置を示すキャレット文字列を生成
    std::string get_caret_line(uint32_t column) const;

    /// エラー表示用のフォーマット済み文字列を生成
    std::string format_error_location(const Span& span, const std::string& message) const;

    /// ヘルパー: 指定したファイルの指定行を取得
    static std::string get_file_line(
        const std::string& filepath,
        const std::unordered_map<std::string, std::string>& file_contents, size_t line_num);

    /// ソースマップを使用したエラー表示（Rustスタイルのスタックトレース）
    /// テンプレートメソッド: ヘッダーに実装を残す
    template <typename SourceMapEntry>
    std::string format_error_with_source_map(
        const Span& span, const std::string& message, const std::vector<SourceMapEntry>& source_map,
        const std::unordered_map<std::string, std::string>& file_contents) const {
        auto loc = get_line_column(span.start);

        std::string result;

        // ソースマップから元のファイル情報を取得
        std::string original_file = filename_;
        size_t original_line = loc.line;
        std::string import_chain;

        if (loc.line > 0 && loc.line <= source_map.size()) {
            const auto& entry = source_map[loc.line - 1];
            original_file = entry.original_file;
            original_line = entry.original_line;
            import_chain = entry.import_chain;
        }

        // 元のファイルの該当行を取得
        std::string original_line_text = get_file_line(original_file, file_contents, original_line);
        if (original_line_text.empty()) {
            original_line_text = get_line(loc.line);
        }

        // 行番号の桁数を計算（前後の行も考慮）
        size_t max_line = original_line + 1;
        size_t line_width = std::to_string(max_line).length();
        if (line_width < 2)
            line_width = 2;
        std::string padding(line_width, ' ');

        // エラー位置を表示（Rustスタイル）
        result += "error: " + message + "\n";
        result += padding + " --> " + original_file + ":" + std::to_string(original_line) + ":" +
                  std::to_string(loc.column) + "\n";
        result += padding + " |\n";

        // コンテキスト行数（エラー行の前に表示する行数）
        const size_t context_lines = 2;

        // 前のコンテキスト行を表示
        for (size_t i = context_lines; i > 0; i--) {
            if (original_line > i) {
                size_t ctx_line = original_line - i;
                std::string ctx_text = get_file_line(original_file, file_contents, ctx_line);
                if (!ctx_text.empty()) {
                    std::string ctx_num_str = std::to_string(ctx_line);
                    while (ctx_num_str.length() < line_width) {
                        ctx_num_str = " " + ctx_num_str;
                    }
                    result += ctx_num_str + " | " + ctx_text + "\n";
                }
            }
        }

        if (!original_line_text.empty()) {
            // エラー行（行番号を右寄せ）
            std::string line_num_str = std::to_string(original_line);
            while (line_num_str.length() < line_width) {
                line_num_str = " " + line_num_str;
            }
            result += line_num_str + " | " + original_line_text + "\n";

            // キャレット行
            std::string caret_line(loc.column > 0 ? loc.column - 1 : 0, ' ');
            caret_line += "^";
            result += padding + " | " + caret_line + "\n";
        }

        // インポートチェーンからスタックトレースを生成
        if (!import_chain.empty() && import_chain != original_file) {
            // インポートチェーンを " -> " で分割
            std::vector<std::string> chain_parts;
            std::string remaining = import_chain;
            std::string delimiter = " -> ";
            size_t pos;
            while ((pos = remaining.find(delimiter)) != std::string::npos) {
                std::string part = remaining.substr(0, pos);
                if (!part.empty()) {
                    chain_parts.push_back(part);
                }
                remaining = remaining.substr(pos + delimiter.length());
            }
            if (!remaining.empty()) {
                chain_parts.push_back(remaining);
            }

            // チェーンの各ファイルでimport文を探す
            for (size_t i = 0; i + 1 < chain_parts.size(); i++) {
                const std::string& importer_file = chain_parts[i];

                // インポート元ファイルでimport文を探す
                if (file_contents.count(importer_file) > 0) {
                    const std::string& content = file_contents.at(importer_file);
                    std::istringstream iss(content);
                    std::string line;
                    size_t ln = 0;
                    size_t import_line = 0;
                    std::string import_text;

                    // まずimport文の行を探す
                    while (std::getline(iss, line)) {
                        ln++;
                        if (line.find("import") != std::string::npos) {
                            import_line = ln;
                            import_text = line;
                            break;
                        }
                    }

                    if (import_line > 0) {
                        result += padding + " |\n";
                        result += padding + " = note: imported here\n";
                        result += padding + " --> " + importer_file + ":" +
                                  std::to_string(import_line) + ":1\n";
                        result += padding + " |\n";

                        // コンテキスト行を表示
                        for (size_t ctx = context_lines; ctx > 0; ctx--) {
                            if (import_line > ctx) {
                                size_t ctx_ln = import_line - ctx;
                                std::string ctx_text =
                                    get_file_line(importer_file, file_contents, ctx_ln);
                                if (!ctx_text.empty()) {
                                    std::string ctx_num_str = std::to_string(ctx_ln);
                                    while (ctx_num_str.length() < line_width) {
                                        ctx_num_str = " " + ctx_num_str;
                                    }
                                    result += ctx_num_str + " | " + ctx_text + "\n";
                                }
                            }
                        }

                        // import行を表示
                        std::string ln_str = std::to_string(import_line);
                        while (ln_str.length() < line_width) {
                            ln_str = " " + ln_str;
                        }
                        result += ln_str + " | " + import_text + "\n";
                        result += padding + " | " + std::string(import_text.length(), '^') + "\n";
                    }
                }
            }

            // メインファイルでの呼び出し箇所を探す（最初のファイル）
            if (!chain_parts.empty()) {
                const std::string& main_file = chain_parts[0];
                if (file_contents.count(main_file) > 0 && original_file != main_file) {
                    // エラーが発生した関数/変数名を抽出（簡易的）
                    // TODO: より正確な実装
                }
            }
        }

        result += padding + " |\n";

        return result;
    }

   private:
    std::string source_;
    std::string filename_;
    std::vector<size_t> line_starts_;  // 各行の開始位置
};

}  // namespace cm