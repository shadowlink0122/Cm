#pragma once

#include <string>
#include <unordered_set>

namespace cm::preprocessor {

/// 条件付きコンパイル プリプロセッサ
/// #ifdef, #ifndef, #else, #end ディレクティブを処理し、
/// アーキテクチャ/OS/コンパイラ情報に基づいてソースコードをフィルタリングする。
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

    /// ソースコードを処理し、条件付きコンパイルを適用
    /// @param source 入力ソースコード
    /// @return フィルタリング済みソースコード
    std::string process(const std::string& source) const;

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
        End,     // #end
        None,    // ディレクティブではない
    };

    /// 行がディレクティブかどうか判定し、種類とシンボル名を返す
    Directive parse_directive(const std::string& line, std::string& symbol) const;

    /// 組み込み定数・ユーザー定義の両方を含む定義セット
    std::unordered_set<std::string> definitions_;
};

}  // namespace cm::preprocessor
