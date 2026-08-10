#pragma once

#include "types.hpp"

#include <string>
#include <vector>

// ============================================================
// 型キーの可逆エンコーディング（C7/C8/C9対応の中核）
// ============================================================
// フラットな「基底名 + __ + 型引数」連結は非単射で、Box<Box<int>>・Box<Box,int>・
// ユーザー定義Box__Box__intが同一キーへ縮退する。本モジュールは ast::Type ツリーを
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

namespace cm::ast::typekey {

// 型を可逆キーへエンコードする
std::string encode_type_key(const ast::TypePtr& type);

// キーから型ツリーを復元する（不正な形式は nullptr）
ast::TypePtr decode_type_key(const std::string& key);

// 基底名と型引数から特殊化キーを構築する（引数なしなら基底名のまま）
std::string make_struct_key(const std::string& base_name, const std::vector<ast::TypePtr>& args);

// キーがジェネリック特殊化エンコードを含むか（'$'の有無）
bool is_encoded_key(const std::string& key);

// キーの基底名を取り出す（"Box$1$3$int" -> "Box"、非エンコード名はそのまま）
std::string base_name_of(const std::string& key);

// 特殊化シンボル名から基底名を取る（$エンコード名は$前・フラット名は最初の__前・素名はそのまま）。
// __前提のベース名抽出サイトを$対応で共通化するための正準関数（mono-flat-name-elimination）
std::string spec_base_name(const std::string& name);

// 型引数ツリーからシンボルキー1個分を生成する（Monomorphization::arg_symbol_keyと同一規約:
// ポインタptr_・参照$R・配列$A・プリミティブ正準名・特殊化は$エンコード。既にマングリング済みの名前は素通し）
std::string arg_key_from_tree(const ast::TypePtr& arg);

// 基底名+型引数ツリーから特殊化構造体の正準キー（$エンコード）を生成する
std::string struct_key_from_tree(const std::string& base_name,
                                 const std::vector<ast::TypePtr>& type_args);

// 構造体正準キーから関数名ドメインの接頭辞（base__argkey…）を生成する
// （特殊化関数名はbase__argkey__methodのフラット結合規約のため、dtor等の関数参照はこの形で組む）
std::string spec_fn_prefix(const std::string& struct_key);

// エンコード済みキーの型引数を復元する（非エンコード名は空）
std::vector<ast::TypePtr> decode_type_args(const std::string& key);

// 型ツリーから関数名ドメインの正準接頭辞を組む（HIR/checker期の呼び出し名産生用）。
// type_argsが空なら名前をそのまま返し（非ジェネリック・型パラメータ名）、
// あればbase__argkey1__argkey2…（argkeyはarg_key_from_tree規約＝ネスト特殊化は$エンコード）を返す。
// 名前が既にマングリング済み（__または$含み）でツリーにargsが無い場合もそのまま返す
std::string fn_prefix_from_tree(const ast::Type& type);

// 表示用（エラーメッセージ・デバッグ）の人間可読名を生成する（Box<Pair<int, string>> 形式）
std::string display_name(const ast::TypePtr& type);
std::string display_name(const std::string& key);

}  // namespace cm::ast::typekey
