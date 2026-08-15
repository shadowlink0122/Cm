# セルフホストパーサ（宣言リスタ）

Cmで書いたCmソースの宣言レベルパーサです。
セルフホスティングへ向けた実証として、v0.17.2で整備した標準ライブラリ（StringSet・chars相当の字句分類・format・スライスバック構造）だけでコンパイラのフロントエンド前段を構成しています。

- コンパイル可能な構文なら**各種定義一覧**（module / import / use / typedef / const / struct / enum / impl / 関数）を出力します
- 構文エラーなら**エラー箇所**（メッセージ・`ファイル:行:桁`・キャレット付きソース行）を出力して終了コード1を返します
- 失敗し得る解析関数はすべて **`Option`型** を返し（`None`=失敗）、位置・メッセージはLexer/Parserが保持します

## 使い方

```bash
# 任意のCmソースを解析
cm run examples/08_selfhost_parser/main.cm -- path/to/file.cm [more.cm ...]

# 自己解析（パーサが自分自身のソースを解析する）
cm run examples/08_selfhost_parser/main.cm -- examples/08_selfhost_parser/main.cm

# 正常系サンプル: 定義一覧が出る
cm run examples/08_selfhost_parser/main.cm -- examples/08_selfhost_parser/samples/ok.cm

# エラー系サンプル: 13行目のメソッド本体欠落を位置付きで報告する
cm run examples/08_selfhost_parser/main.cm -- examples/08_selfhost_parser/samples/error.cm
```

## ディレクトリ構成（トップは`main.cm`のみ・段階ごとにフォルダ分割）

```
main.cm                  CLI（引数解析・読み込み・結果出力）
diag/                    診断
  messages.cm            エラーメッセージ定義（i18n対応: enum MsgId×Langのテンプレート表・{0}プレースホルダ・CM_LANG=jaで日本語）
lexer/                   字句解析
  token.cm               トークン種別enum（TokenKind）とTokenStream（パラレルスライス）
  scan.cm                字句解析器（コメント/文字列/文字/数値/::融合、キーワード判定はStringSet、バイト値は文字キャスト定数）
parser/                  構文解析
  state.cm               Parser状態・基本操作（expect/skip/ジェネリクス読取）・エラー診断・定義レコーダ
  decl/                  宣言種別ごとの解析
    modules.cm           module / import（選択・相対・ワイルドカード）/ use libc
    types.cm             struct / enum（ネスト型・無名struct宣言子・with句・ペイロード付きバリアント）
    impls.cm             impl / interface（self・~self・private・static・overload）
    funcs.cm             関数 / typedef / const / extern "C"
    dispatch.cm          トップレベル宣言のディスパッチとparse_program
samples/                 パーサへ入力するサンプル
  ok.cm                  正常系（全宣言種別を含む）
  error.cm               エラー系（メソッド本体欠落）
tests/                   単体テスト（#[test]。対象モジュール名 + _test.cm）
```

関数本体・メソッド本体は波括弧の対応で読み飛ばす「定義リスタ」としての解析です（文字列リテラル内の波括弧は字句解析済みのため誤検出しません）。

## 単体テスト

`tests/`フォルダに`#[test]`ディレクティブの単体テスト（対象モジュール名 + `_test.cm`）を集約しています。

```bash
cm test examples/08_selfhost_parser/tests/scan_test.cm
cm test examples/08_selfhost_parser/tests/types_test.cm
# ...（messages / token / state / modules / impls / funcs / dispatch も同様）
```

CI（`scripts/ci/check_examples.sh`）は全`.cm`の型検査に加えて、`tests/`配下の単体実行・正常/エラーサンプル・自己解析を検証します。
エラーメッセージの検証はテストも`diag/messages.cm`の`msgf`とテンプレート表を参照するため、文面変更に自動で追従します。

## エラーメッセージのi18n

Cmコンパイラ本体と同じ構成で、メッセージID（`enum MsgId`）×言語（`enum Lang`）の2次元テンプレート表に文面を集約しています。
テンプレートは`{0}`〜`{2}`のプレースホルダを持つ完結した1文で、語順が言語ごとに異なるため断片連結による文組み立ては行いません。
プレースホルダ埋めはC++側`i18n::msgf`と同じ方式の`msgf(MsgId, string[])`1本で、呼び出し側が`msgf(MsgId::ExpectedButFound, [expected, found])`のようにIDと引数配列を直接渡します。

```bash
# 日本語診断（既定は英語）
CM_LANG=ja cm run examples/08_selfhost_parser/main.cm -- examples/08_selfhost_parser/samples/error.cm
# → 構文エラー: method signature の後には '{' または ';' が必要ですが '}' が見つかりました
```

## エラーハンドリングの設計

`Option<T>`はペイロードに失敗情報を持てないため、「失敗＝`Option::None`を返す・診断（メッセージ/行/桁）は状態側に保持」という規約で統一しています。
最初のエラーだけを保持するため、深い再帰の途中で失敗しても報告位置が上書きされません。
