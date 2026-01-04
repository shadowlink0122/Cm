#include "js_minifier.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>

namespace cm::codegen::js::optimizations {

JSMinifier::JSMinifier(const Config& config) : config(config) {
    // JavaScript予約語を初期化
    reservedWords = {
        "break", "case", "catch", "class", "const", "continue", "debugger", "default", "delete",
        "do", "else", "export", "extends", "finally", "for", "function", "if", "import", "in",
        "instanceof", "let", "new", "return", "super", "switch", "this", "throw", "try", "typeof",
        "var", "void", "while", "with", "yield", "async", "await",
        // グローバルオブジェクト
        "window", "document", "console", "Math", "String", "Number", "Boolean", "Array", "Object",
        "Date", "JSON", "Promise", "undefined", "null", "NaN", "Infinity", "globalThis", "self",
        // Cm言語のランタイム関数
        "cm_print", "cm_println", "cm_allocate", "cm_free"};

    initializePatterns();
}

std::string JSMinifier::minify(const std::string& code) {
    stats = Stats();  // 統計をリセット
    stats.originalSize = code.size();

    std::string result = code;

    // 各最適化を順番に適用
    if (config.removeComments || config.removeWhitespace) {
        result = removeWhitespaceAndComments(result);
    }

    if (config.removeDeadCode) {
        result = removeDeadCode(result);
    }

    if (config.mergeVariables) {
        result = mergeVariableDeclarations(result);
    }

    if (config.useArrowFunctions) {
        result = convertToArrowFunctions(result);
    }

    if (config.optimizeConditionals) {
        result = optimizeConditionals(result);
    }

    if (config.optimizeLoops) {
        result = optimizeLoops(result);
    }

    if (config.combineStrings) {
        result = optimizeStringConcatenation(result);
    }

    if (config.inlineSimpleFunctions) {
        result = inlineSimpleFunctions(result);
    }

    if (config.shortenVariableNames) {
        result = shortenVariableNames(result);
    }

    stats.minifiedSize = result.size();
    return result;
}

std::string JSMinifier::removeWhitespaceAndComments(const std::string& code) {
    std::stringstream result;
    bool inString = false;
    bool inSingleComment = false;
    bool inMultiComment = false;
    char stringChar = 0;

    for (size_t i = 0; i < code.size(); ++i) {
        char c = code[i];
        char next = (i + 1 < code.size()) ? code[i + 1] : '\0';

        // 文字列リテラル内
        if (!inSingleComment && !inMultiComment) {
            if ((c == '"' || c == '\'' || c == '`') && !inString) {
                inString = true;
                stringChar = c;
                result << c;
                continue;
            } else if (inString && c == stringChar && code[i - 1] != '\\') {
                inString = false;
                result << c;
                continue;
            }
        }

        if (inString) {
            result << c;
            continue;
        }

        // コメント処理
        if (c == '/' && next == '/' && !inMultiComment) {
            inSingleComment = true;
            stats.commentsRemoved++;
            ++i;  // 次の'/'もスキップ
            continue;
        } else if (c == '/' && next == '*' && !inSingleComment) {
            inMultiComment = true;
            stats.commentsRemoved++;
            ++i;  // 次の'*'もスキップ
            continue;
        } else if (c == '*' && next == '/' && inMultiComment) {
            inMultiComment = false;
            ++i;  // 次の'/'もスキップ
            continue;
        } else if (c == '\n' && inSingleComment) {
            inSingleComment = false;
            // 改行は保持（セミコロン省略の問題を避ける）
            if (config.removeWhitespace) {
                result << ' ';
            } else {
                result << '\n';
            }
            continue;
        }

        if (inSingleComment || inMultiComment) {
            continue;
        }

        // 空白の処理
        if (config.removeWhitespace) {
            if (std::isspace(c)) {
                // 必要な空白のみ保持
                if (i > 0 && i + 1 < code.size()) {
                    char prev = code[i - 1];
                    if ((std::isalnum(prev) || prev == '_' || prev == '$') &&
                        (std::isalnum(next) || next == '_' || next == '$')) {
                        result << ' ';  // 識別子間の空白は保持
                    }
                }
            } else {
                result << c;
            }
        } else {
            result << c;
        }
    }

    return result.str();
}

std::string JSMinifier::shortenVariableNames(const std::string& code) {
    variableMapping.clear();
    shortNameCounter = 0;

    std::stringstream result;
    std::string currentToken;
    bool inString = false;
    char stringChar = 0;

    for (size_t i = 0; i < code.size(); ++i) {
        char c = code[i];

        // 文字列リテラル処理
        if ((c == '"' || c == '\'' || c == '`') && !inString) {
            if (!currentToken.empty()) {
                result << processToken(currentToken);
                currentToken.clear();
            }
            inString = true;
            stringChar = c;
            result << c;
            continue;
        } else if (inString) {
            result << c;
            if (c == stringChar && code[i - 1] != '\\') {
                inString = false;
            }
            continue;
        }

        // トークン処理
        if (std::isalnum(c) || c == '_' || c == '$') {
            currentToken += c;
        } else {
            if (!currentToken.empty()) {
                result << processToken(currentToken);
                currentToken.clear();
            }
            result << c;
        }
    }

    if (!currentToken.empty()) {
        result << processToken(currentToken);
    }

    return result.str();
}

std::string JSMinifier::processToken(const std::string& token) {
    // 予約語はそのまま
    if (isReserved(token)) {
        return token;
    }

    // 数値リテラルはそのまま
    if (std::all_of(token.begin(), token.end(), ::isdigit)) {
        return token;
    }

    // 変数名の短縮
    auto it = variableMapping.find(token);
    if (it != variableMapping.end()) {
        return it->second;
    }

    // 新しい短縮名を生成
    std::string shortName = nextShortName();
    variableMapping[token] = shortName;
    stats.variablesRenamed++;
    return shortName;
}

std::string JSMinifier::nextShortName() {
    // 短い変数名を生成 (a, b, ..., z, aa, ab, ...)
    std::string name;
    unsigned n = shortNameCounter++;

    do {
        name = char('a' + (n % 26)) + name;
        n /= 26;
    } while (n > 0);

    // 予約語と衝突しないかチェック
    if (isReserved(name)) {
        return nextShortName();  // 次の名前を試す
    }

    return name;
}

std::string JSMinifier::inlineSimpleFunctions(const std::string& code) {
    std::string result = code;

    // 単純な1行関数をインライン化
    // 例: function add(a,b){return a+b} -> ((a,b)=>a+b)
    std::regex simpleFunc(R"(function\s+(\w+)\s*\(([^)]*)\)\s*\{\s*return\s+([^;}]+)\s*;\?\s*\})");

    result = std::regex_replace(result, simpleFunc, "const $1=($2)=>$3");

    // インライン化した関数の呼び出しを最適化
    // TODO: より高度なインライン化の実装

    return result;
}

std::string JSMinifier::removeDeadCode(const std::string& code) {
    std::string result = code;

    // return後の到達不能コードを削除
    std::regex deadAfterReturn(R"(return[^;]*;[^}]+(?=\}))");
    result = std::regex_replace(result, deadAfterReturn, "return$1;");

    // if(false) { ... } を削除
    std::regex deadIfFalse(R"(if\s*\(\s*false\s*\)\s*\{[^}]*\})");
    result = std::regex_replace(result, deadIfFalse, "");

    // while(false) { ... } を削除
    std::regex deadWhileFalse(R"(while\s*\(\s*false\s*\)\s*\{[^}]*\})");
    result = std::regex_replace(result, deadWhileFalse, "");

    stats.deadCodeRemoved++;
    return result;
}

std::string JSMinifier::mergeVariableDeclarations(const std::string& code) {
    std::string result = code;

    // 連続するvar文をマージ
    // var a=1;var b=2; -> var a=1,b=2;
    std::regex consecutiveVars(R"(var\s+([^;]+);\s*var\s+)");

    while (std::regex_search(result, consecutiveVars)) {
        result = std::regex_replace(result, consecutiveVars, "var $1,",
                                    std::regex_constants::format_first_only);
    }

    // let文も同様に処理
    std::regex consecutiveLets(R"(let\s+([^;]+);\s*let\s+)");

    while (std::regex_search(result, consecutiveLets)) {
        result = std::regex_replace(result, consecutiveLets, "let $1,",
                                    std::regex_constants::format_first_only);
    }

    return result;
}

std::string JSMinifier::convertToArrowFunctions(const std::string& code) {
    std::string result = code;

    // 単純な関数式を矢印関数に変換
    // function(x){return x*2} -> x=>x*2
    std::regex simpleFuncExpr(R"(function\s*\(([^)]*)\)\s*\{\s*return\s+([^;}]+)\s*;\?\s*\})");
    result = std::regex_replace(result, simpleFuncExpr, "($1)=>$2");

    // 引数なし: function(){return 1} -> ()=>1
    std::regex noArgFunc(R"(function\s*\(\s*\)\s*\{\s*return\s+([^;}]+)\s*;\?\s*\})");
    result = std::regex_replace(result, noArgFunc, "()=>$1");

    return result;
}

std::string JSMinifier::optimizeConditionals(const std::string& code) {
    std::string result = code;

    // 三項演算子への変換
    // if(c)a=1;else a=2; -> a=c?1:2;
    std::regex simpleIfElse(R"(if\s*\(([^)]+)\)\s*([^=]+)=([^;]+);\s*else\s*\2=([^;]+);)");
    result = std::regex_replace(result, simpleIfElse, "$2=$1?$3:$4;");

    // true/false条件の簡約化
    // x==true -> x, x==false -> !x
    std::regex trueComparison(R"(([^=\s]+)\s*==\s*true)");
    result = std::regex_replace(result, trueComparison, "$1");

    std::regex falseComparison(R"(([^=\s]+)\s*==\s*false)");
    result = std::regex_replace(result, falseComparison, "!$1");

    // !!x -> Boolean(x) の簡約化（必要な場合のみ）
    std::regex doubleBang(R"(!\s*!\s*([^&|]+))");
    result = std::regex_replace(result, doubleBang, "Boolean($1)");

    stats.conditionalsOptimized++;
    return result;
}

std::string JSMinifier::optimizeLoops(const std::string& code) {
    std::string result = code;

    // for(var i=0;i<arr.length;i++) -> for(var i=0,l=arr.length;i<l;i++)
    // 配列長のキャッシュ
    std::regex forArrayLength(
        R"(for\s*\(\s*var\s+(\w+)\s*=\s*0\s*;\s*\1\s*<\s*([^.]+)\.length\s*;\s*\1\+\+\s*\))");
    result = std::regex_replace(result, forArrayLength, "for(var $1=0,_l=$2.length;$1<_l;$1++)");

    // while(true) -> for(;;) (わずかに短い)
    std::regex whileTrue(R"(while\s*\(\s*true\s*\))");
    result = std::regex_replace(result, whileTrue, "for(;;)");

    stats.loopsOptimized++;
    return result;
}

std::string JSMinifier::optimizeStringConcatenation(const std::string& code) {
    std::string result = code;

    // 連続する文字列連結を配列joinに変換（長い場合のみ有効）
    // "a"+"b"+"c" -> ["a","b","c"].join("")
    // （この実装では簡単な最適化のみ）

    // 隣接する文字列リテラルの結合
    std::regex adjacentStrings(R"("([^"]*?)"\s*\+\s*"([^"]*?)")");
    result = std::regex_replace(result, adjacentStrings, R"("$1$2")");

    std::regex adjacentSingleQuotes(R"('([^']*?)'\s*\+\s*'([^']*?)')");
    result = std::regex_replace(result, adjacentSingleQuotes, R"('$1$2')");

    stats.stringsOptimized++;
    return result;
}

void JSMinifier::initializePatterns() {
    // 正規表現パターンの初期化
    whitespacePattern = std::regex(R"(\s+)");
    commentPattern = std::regex(R"((/\*[\s\S]*?\*/)|(//.*))"");
    variablePattern = std::regex(R"(\b[a-zA-Z_$][a-zA-Z0-9_$]*\b)");
    functionPattern = std::regex(R"(function\s*\w*\s*\([^)]*\)\s*\{[^}]*\})");
    conditionalPattern = std::regex(R"(if\s*\([^)]+\)[^;]+;)");
    loopPattern = std::regex(R"((for|while)\s*\([^)]+\)[^;]+;)");
    stringConcatPattern = std::regex(R"((['"])[^\1]*?\1\s*\+\s*(['"])[^\2]*?\2)");
}

bool JSMinifier::isIdentifier(const std::string& str) const {
    if (str.empty())
        return false;

    // 最初の文字は文字、_、$のいずれか
    if (!std::isalpha(str[0]) && str[0] != '_' && str[0] != '$') {
        return false;
    }

    // 残りは文字、数字、_、$
    return std::all_of(str.begin() + 1, str.end(),
                       [](char c) { return std::isalnum(c) || c == '_' || c == '$'; });
}

bool JSMinifier::isReserved(const std::string& str) const {
    return reservedWords.find(str) != reservedWords.end();
}

bool JSMinifier::isInString(const std::string& code, size_t pos) const {
    bool inString = false;
    char stringChar = 0;

    for (size_t i = 0; i < pos && i < code.size(); ++i) {
        if (!inString && (code[i] == '"' || code[i] == '\'' || code[i] == '`')) {
            inString = true;
            stringChar = code[i];
        } else if (inString && code[i] == stringChar && code[i - 1] != '\\') {
            inString = false;
        }
    }

    return inString;
}

bool JSMinifier::isInComment(const std::string& code, size_t pos) const {
    // 単純な実装（より正確にするには状態機械が必要）
    for (size_t i = 0; i < pos && i + 1 < code.size(); ++i) {
        if (code[i] == '/' && code[i + 1] == '/') {
            // 行コメント内かチェック
            size_t endLine = code.find('\n', i);
            if (endLine == std::string::npos || pos < endLine) {
                return true;
            }
        } else if (code[i] == '/' && code[i + 1] == '*') {
            // ブロックコメント内かチェック
            size_t endComment = code.find("*/", i + 2);
            if (endComment == std::string::npos || pos < endComment) {
                return true;
            }
        }
    }
    return false;
}

void JSMinifier::printStatistics() const {
    std::cerr << "\n=== JavaScript Minification Statistics ===\n";
    std::cerr << "  Original size: " << stats.originalSize << " bytes\n";
    std::cerr << "  Minified size: " << stats.minifiedSize << " bytes\n";
    std::cerr << "  Size reduction: "
              << (100.0 * (stats.originalSize - stats.minifiedSize) / stats.originalSize) << "%\n";
    std::cerr << "  Comments removed: " << stats.commentsRemoved << "\n";
    std::cerr << "  Variables renamed: " << stats.variablesRenamed << "\n";
    std::cerr << "  Functions inlined: " << stats.functionsInlined << "\n";
    std::cerr << "  Dead code removed: " << stats.deadCodeRemoved << "\n";
    std::cerr << "  Conditionals optimized: " << stats.conditionalsOptimized << "\n";
    std::cerr << "  Loops optimized: " << stats.loopsOptimized << "\n";
    std::cerr << "  Strings optimized: " << stats.stringsOptimized << "\n";
    std::cerr << "==========================================\n";
}

}  // namespace cm::codegen::js::optimizations