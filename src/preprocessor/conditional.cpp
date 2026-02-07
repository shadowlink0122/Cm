#include "conditional.hpp"

#include <cctype>
#include <sstream>

namespace cm::preprocessor {

ConditionalPreprocessor::ConditionalPreprocessor() {
    init_builtin_definitions();
}

void ConditionalPreprocessor::init_builtin_definitions() {
    // ===== アーキテクチャ検出 =====
#if defined(__x86_64__) || defined(_M_X64)
    definitions_.insert("__x86_64__");
    definitions_.insert("__x86__");
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
    definitions_.insert("__arm64__");
    definitions_.insert("__aarch64__");
#endif

#if defined(__i386__) || defined(_M_IX86)
    definitions_.insert("__x86__");
    definitions_.insert("__i386__");
#endif

#if defined(__riscv)
    definitions_.insert("__riscv__");
#endif

    // ===== OS検出 =====
#if defined(__APPLE__) && defined(__MACH__)
    definitions_.insert("__macos__");
    definitions_.insert("__apple__");
    definitions_.insert("__unix__");
#endif

#if defined(__linux__)
    definitions_.insert("__linux__");
    definitions_.insert("__unix__");
#endif

#if defined(_WIN32) || defined(_WIN64)
    definitions_.insert("__windows__");
#endif

#if defined(__FreeBSD__)
    definitions_.insert("__freebsd__");
    definitions_.insert("__unix__");
#endif

    // ===== コンパイラ情報 =====
    definitions_.insert("__CM__");

    // ===== ポインタサイズ =====
#if defined(__LP64__) || defined(_WIN64)
    definitions_.insert("__64BIT__");
#else
    definitions_.insert("__32BIT__");
#endif

    // ===== デバッグモード =====
#if !defined(NDEBUG)
    definitions_.insert("__DEBUG__");
#endif
}

void ConditionalPreprocessor::define(const std::string& name) {
    definitions_.insert(name);
}

void ConditionalPreprocessor::undefine(const std::string& name) {
    definitions_.erase(name);
}

bool ConditionalPreprocessor::is_defined(const std::string& name) const {
    return definitions_.count(name) > 0;
}

ConditionalPreprocessor::Directive ConditionalPreprocessor::parse_directive(
    const std::string& line, std::string& symbol) const {
    // 行頭の空白をスキップ
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
        i++;
    }

    // '#' で始まるかチェック
    if (i >= line.size() || line[i] != '#') {
        return Directive::None;
    }
    i++;

    // ディレクティブ名を読み取る
    size_t start = i;
    while (i < line.size() && std::isalpha(static_cast<unsigned char>(line[i]))) {
        i++;
    }
    std::string directive_name = line.substr(start, i - start);

    if (directive_name == "ifdef") {
        // シンボル名を読み取る
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
            i++;
        }
        size_t sym_start = i;
        while (i < line.size() &&
               (std::isalnum(static_cast<unsigned char>(line[i])) || line[i] == '_')) {
            i++;
        }
        symbol = line.substr(sym_start, i - sym_start);
        return Directive::Ifdef;
    }

    if (directive_name == "ifndef") {
        // シンボル名を読み取る
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
            i++;
        }
        size_t sym_start = i;
        while (i < line.size() &&
               (std::isalnum(static_cast<unsigned char>(line[i])) || line[i] == '_')) {
            i++;
        }
        symbol = line.substr(sym_start, i - sym_start);
        return Directive::Ifndef;
    }

    if (directive_name == "else") {
        return Directive::Else;
    }

    if (directive_name == "end") {
        return Directive::End;
    }

    // 未知のディレクティブ → 通常の行として扱う
    return Directive::None;
}

std::string ConditionalPreprocessor::process(const std::string& source) const {
    std::istringstream stream(source);
    std::string line;
    std::string result;
    result.reserve(source.size());

    // ネストスタック: true = このブロックのコードを出力する
    // スタックが空 → トップレベル（常に出力）
    struct CondState {
        bool active;         // 現在のブランチがアクティブか
        bool parent_active;  // 親ブロックがアクティブか
        bool had_true;       // いずれかのブランチが真だったか（else用）
    };
    std::vector<CondState> stack;

    // 現在コードを出力すべきかどうか
    auto should_output = [&]() -> bool {
        if (stack.empty())
            return true;
        return stack.back().active && stack.back().parent_active;
    };

    while (std::getline(stream, line)) {
        std::string symbol;
        auto directive = parse_directive(line, symbol);

        switch (directive) {
            case Directive::Ifdef: {
                bool parent = should_output();
                bool defined = is_defined(symbol);
                stack.push_back({defined, parent, defined});
                // ディレクティブ行自体は出力しない（空行で置換して行番号を保持）
                result += '\n';
                break;
            }

            case Directive::Ifndef: {
                bool parent = should_output();
                bool not_defined = !is_defined(symbol);
                stack.push_back({not_defined, parent, not_defined});
                result += '\n';
                break;
            }

            case Directive::Else: {
                if (!stack.empty()) {
                    // まだどのブランチも真でなかった場合のみelseを有効化
                    bool should_be_active = !stack.back().had_true;
                    stack.back().active = should_be_active;
                    if (should_be_active) {
                        stack.back().had_true = true;
                    }
                }
                result += '\n';
                break;
            }

            case Directive::End: {
                if (!stack.empty()) {
                    stack.pop_back();
                }
                result += '\n';
                break;
            }

            case Directive::None: {
                if (should_output()) {
                    result += line;
                }
                result += '\n';
                break;
            }
        }
    }

    // 末尾の余分な改行を除去（元のソースが改行で終わっていない場合）
    if (!source.empty() && source.back() != '\n' && !result.empty() && result.back() == '\n') {
        result.pop_back();
    }

    return result;
}

}  // namespace cm::preprocessor
