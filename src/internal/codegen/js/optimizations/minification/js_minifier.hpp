#pragma once

#include <regex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cm::codegen::js::optimizations {

/// JavaScript Minifier - 生成されたJSコードのサイズ最適化
class JSMinifier {
   public:
    /// Minification設定
    struct Config {
        bool removeWhitespace = true;       // 不要な空白を削除
        bool removeComments = true;         // コメントを削除
        bool shortenVariableNames = true;   // 変数名を短縮
        bool inlineSimpleFunctions = true;  // 単純な関数をインライン化
        bool removeDeadCode = true;         // 到達不能コードを削除
        bool mergeVariables = true;         // 変数宣言をマージ
        bool useArrowFunctions = true;      // function式を矢印関数に変換
        bool optimizeConditionals = true;   // 条件式を最適化
        bool optimizeLoops = true;          // ループを最適化
        bool combineStrings = true;         // 文字列連結を最適化
    };

    /// コンストラクタ
    explicit JSMinifier(const Config& config = Config()) : config(config) {}

    /// JavaScriptコードを最小化
    std::string minify(const std::string& code);

    /// 統計情報を取得
    struct Stats {
        size_t originalSize = 0;
        size_t minifiedSize = 0;
        unsigned commentsRemoved = 0;
        unsigned variablesRenamed = 0;
        unsigned functionsInlined = 0;
        unsigned deadCodeRemoved = 0;
        unsigned conditionalsOptimized = 0;
        unsigned loopsOptimized = 0;
        unsigned stringsOptimized = 0;
    };

    const Stats& getStatistics() const { return stats; }
    void printStatistics() const;

   private:
    Config config;
    Stats stats;

    /// 変数名のマッピング（元の名前 -> 短縮名）
    std::unordered_map<std::string, std::string> variableMapping;

    /// 予約語とグローバル変数（変更不可）
    std::unordered_set<std::string> reservedWords;

    /// 次の短縮変数名を生成
    std::string nextShortName();
    unsigned shortNameCounter = 0;

    /// 各種最適化処理

    /// 空白とコメントの削除
    std::string removeWhitespaceAndComments(const std::string& code);

    /// 変数名の短縮
    std::string shortenVariableNames(const std::string& code);

    /// 単純な関数のインライン化
    std::string inlineSimpleFunctions(const std::string& code);

    /// 到達不能コードの削除
    std::string removeDeadCode(const std::string& code);

    /// 変数宣言のマージ (var a; var b; -> var a,b;)
    std::string mergeVariableDeclarations(const std::string& code);

    /// 矢印関数への変換
    std::string convertToArrowFunctions(const std::string& code);

    /// 条件式の最適化
    std::string optimizeConditionals(const std::string& code);

    /// ループの最適化
    std::string optimizeLoops(const std::string& code);

    /// 文字列連結の最適化
    std::string optimizeStringConcatenation(const std::string& code);

    /// ヘルパー関数

    /// 識別子かどうかチェック
    bool isIdentifier(const std::string& str) const;

    /// 予約語かどうかチェック
    bool isReserved(const std::string& str) const;

    /// 文字列リテラル内かどうか判定
    bool isInString(const std::string& code, size_t pos) const;

    /// コメント内かどうか判定
    bool isInComment(const std::string& code, size_t pos) const;

    /// 正規表現パターンのプリコンパイル
    void initializePatterns();

    /// トークンの処理（変数名短縮用）
    std::string processToken(const std::string& token);

    /// パターンマッチング用の正規表現
    std::regex whitespacePattern;
    std::regex commentPattern;
    std::regex variablePattern;
    std::regex functionPattern;
    std::regex conditionalPattern;
    std::regex loopPattern;
    std::regex stringConcatPattern;
};

}  // namespace cm::codegen::js::optimizations