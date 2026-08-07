#pragma once

#include <string>
#include <unordered_set>
#include <vector>

namespace cm::preprocessor {

/// 条件付きコンパイル プリプロセッサ
/// #ifdef, #ifndef, #else, #end（別名 #endif）ディレクティブを処理し、アーキテクチャ/OS/コンパイラ情報に基づいてソースコードをフィルタリングする。
///
/// 使用例:
///   #ifdef __x86_64__
///       __asm__("addl $$75, ${+r:x}");
///   #end
///
///   #ifndef __arm64__
///       // ARM64以外で実行
///   #else
///       // ARM64のコード
///   #end
class ConditionalPreprocessor {
   public:
    ConditionalPreprocessor();

    /// 構造検査の違反種別（R6: 従来は閉じ忘れ・過剰#end・#defineが全て無診断だった）
    enum class IssueKind {
        UnclosedConditional,  // #ifdef/#ifndef がEOFまで閉じない
        UnmatchedDirective,   // 対応するブロックのない #end/#endif/#else
        DefineNotSupported,  // #define は未実装（-D または組み込みシンボルを案内）
    };

    /// 構造検査の違反1件。lineは1始まりの行番号、detailはディレクティブ名やシンボル名
    struct Issue {
        IssueKind kind;
        int line;
        std::string detail;
    };

    /// ソースコードを処理し、条件付きコンパイルを適用
    /// @param source 入力ソースコード
    /// @param issues 構造検査の違反出力（閉じ忘れ・過剰#end・#define）
    /// @return フィルタリング済みソースコード
    std::string process(const std::string& source, std::vector<Issue>& issues) const;

    /// カスタム定義を追加（-D オプション等）
    void define(const std::string& name);

    /// 定義を削除
    void undefine(const std::string& name);

    /// 定義されているかチェック
    bool is_defined(const std::string& name) const;

    /// 現在の定義一覧を取得
    const std::unordered_set<std::string>& definitions() const { return definitions_; }

   private:
    /// プラットフォーム検出に基づく組み込み定数を初期化
    void init_builtin_definitions();

    /// ディレクティブの種類
    enum class Directive {
        Ifdef,   // #ifdef
        Ifndef,  // #ifndef
        Else,    // #else
        End,     // #end（別名 #endif）
        Define,  // #define（未実装。専用診断を出す）
        None,    // ディレクティブではない
    };

    /// 行がディレクティブかどうか判定し、種類とシンボル名を返す
    Directive parse_directive(const std::string& line, std::string& symbol) const;

    /// 組み込み定数・ユーザー定義の両方を含む定義セット
    std::unordered_set<std::string> definitions_;
};

}  // namespace cm::preprocessor
