// ============================================================
// Formatter - コスメティック整形（エントリポイント）
// ============================================================
// 各整形パスの実装は formatter/ 配下の責務別ファイルにある

#include "formatter.hpp"

#include "internal/base/i18n.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace cm {
namespace fmt {

FormatResult Formatter::format(const std::string& original_code) {
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

    // 5. セミコロン後の改行（括弧内除外）- インデント前に実行
    code = enforce_semicolon_newline(code, changes);

    // 6. インデント正規化（セミコロン改行後のコードも正規化）
    code = normalize_indentation(code, changes);

    // 7. 演算子周りの空白
    code = normalize_operator_spacing(code, changes);

    // 8. 最大行幅を超える宣言・式の折り返し（空白正規化で行長が確定した後に実行し、折り返した継続行のインデントは次のインデント正規化の再実行で整える）
    code = wrap_long_lines(code, changes);

    // 9. インデント正規化の再実行（折り返しで生じた継続行を整える）
    code = normalize_indentation(code, changes);

    // 10. 行末コメントの位置揃え
    code = align_inline_comments(code, changes);

    // 11. ファイル末尾に1つの改行を保証
    code = ensure_trailing_newline(code, changes);

    result.formatted_code = code;
    result.modified = (code != original_code);
    result.changes_applied = changes;

    return result;
}

bool Formatter::format_file(const std::string& filepath) {
    std::ifstream ifs(filepath);
    if (!ifs) {
        std::cerr << i18n::msgf(i18n::MsgId::FmtCannotReadFile, filepath);
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
            std::cerr << i18n::msgf(i18n::MsgId::FmtCannotWriteFile, filepath);
            return false;
        }
        ofs << result.formatted_code;
        return true;
    }

    return false;  // 変更なし
}

void Formatter::print_summary(const FormatResult& result, std::ostream& out) const {
    if (result.changes_applied > 0) {
        out << i18n::msgf(i18n::MsgId::FmtFormattingFixEs, result.changes_applied);
    }
}

}  // namespace fmt
}  // namespace cm
