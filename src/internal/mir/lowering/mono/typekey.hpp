#pragma once

#include "internal/hir/types.hpp"

#include <string>
#include <vector>

// ============================================================
// 型キーの可逆エンコーディング（C7/C8/C9対応の中核）
// ============================================================
// フラットな「基底名 + __ + 型引数」連結は非単射で、Box<Box<int>>・Box<Box,int>・
// ユーザー定義Box__Box__intが同一キーへ縮退する。本モジュールは hir::Type ツリーを
// 唯一の真実とし、区切り文字 '$'（Cm識別子に出現しない）による長さプレフィックス付きの
// 可逆エンコーディングを提供する。decode(encode(t)) は構造一致（往復不変）を保証する。
//
// エンコード仕様:
// - プリミティブ型: 正規名をそのまま用いる（int / uint / string / bool 等）
// - 名前のみの型: 名前をそのまま用いる（'$'を含まないため復元は自明）
// - ジェネリック特殊化: base '$' 引数個数 '$' の後に、各引数を「<エンコード長>'$'<エンコード>」で連結
//   例: Pair<int,string> -> "Pair$2$3$int6$string"
//       Box<Pair<int,string>> -> "Box$1$20$Pair$2$3$int6$string"
// - ポインタ: "$P" + 要素のエンコード（例: *int -> "$Pint"）
// - 参照:     "$R" + 要素のエンコード
// - 固定長配列: "$A" サイズ '$' 要素（例: int[4] -> "$A4$int"）、可変長は "$A$" + 要素
// ユーザー識別子は '$' を含めないため、特殊化キーはユーザー型名と原理的に衝突しない（C8）。

namespace cm::mir::typekey {

// 型を可逆キーへエンコードする
std::string encode_type_key(const hir::TypePtr& type);

// キーから型ツリーを復元する（不正な形式は nullptr）
hir::TypePtr decode_type_key(const std::string& key);

// 基底名と型引数から特殊化キーを構築する（引数なしなら基底名のまま）
std::string make_struct_key(const std::string& base_name, const std::vector<hir::TypePtr>& args);

// キーがジェネリック特殊化エンコードを含むか（'$'の有無）
bool is_encoded_key(const std::string& key);

// キーの基底名を取り出す（"Box$1$3$int" -> "Box"、非エンコード名はそのまま）
std::string base_name_of(const std::string& key);

// エンコード済みキーの型引数を復元する（非エンコード名は空）
std::vector<hir::TypePtr> decode_type_args(const std::string& key);

// 表示用（エラーメッセージ・デバッグ）の人間可読名を生成する（Box<Pair<int, string>> 形式）
std::string display_name(const hir::TypePtr& type);
std::string display_name(const std::string& key);

}  // namespace cm::mir::typekey
