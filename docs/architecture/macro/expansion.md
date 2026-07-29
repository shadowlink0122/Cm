# マクロシステムと展開

Cmのマクロは字句レベルのテキスト置換ではなく、`macro TYPE NAME = 値;` 構文で宣言する型付きマクロとしてASTに取り込まれ、通常の宣言と同じ型検査を通ってからHIRで定数畳み込みされる。値がリテラルの定数マクロは使用箇所の識別子がHIRリテラルへインライン置換され、値がラムダ式の関数マクロはパース時点で通常の関数宣言へ変換されるため、後段のMIR・LLVM・native/jitバックエンドにはマクロ固有の表現が一切現れない。これとは別に、`src/internal/macro/` にはトークンツリーとパターンマッチングによる宣言的マクロ（`name!(...)` 形式）の展開器一式（matcher・expander・hygiene）が存在するが、これはビルド対象に接続されていない参照実装であり、現行パイプラインの挙動には関与しない。

## 概要

マクロ宣言はトップレベル宣言のパース中に `macro` キーワードで検出される（`src/internal/syntax/parser/parser_decl.cpp:282`）。同じ `parse_macro` へ合流する入口が他に2つあり、`export macro` によるエクスポート付き定義（`parser_decl.cpp:124`）と、C++風の `#macro` 表記（`parser_decl.cpp:257`）が受理される。属性形式の `@[macro]` は受理されず nullptr が返る（`parser_decl.cpp:59-62`）。

展開のタイミングは2段階に分かれる。

1. **パース時**: `parse_macro`（`src/internal/syntax/parser/module/attribute.cpp:26`）が `macro TYPE NAME = EXPR;` を読み、値がラムダ式なら即座に `ast::FunctionDecl` へ変換する（関数マクロの「展開」はここで完結する）。それ以外は `ast::MacroDecl`（Kind::Constant）を生成する。
2. **HIR lowering時**: 定数マクロの値がリテラルであれば名前→値のマップに登録され（`src/internal/hir/lowering/decl.cpp:612-640`）、以降の式loweringで同名の識別子参照が `HirLiteral` へ置き換わる（`src/internal/hir/lowering/expr.cpp:94-125`）。

つまり独立した「マクロ展開フェーズ」は存在せず、パーサとHIR loweringの中に埋め込まれた形で処理される。条件付きコンパイル（`#ifdef`/`#ifndef`/`#else`/`#end`）はマクロシステムとは別系統のプリプロセッサ機構であり、本文書の対象外である。

Cm言語での構文と使用例（`tests/common/macro/typed_macro.cm` および `tests/common/macro/macro_function.cm` から引用）:

```cm
// int型マクロ
macro int VERSION = 13;

// string型マクロ
macro string APP_NAME = "CmApp";

// bool型マクロ
macro bool DEBUG = true;

// 関数マクロ - 引数なし
macro int*() get_version = () => VERSION;

// 関数マクロ - 引数あり
macro int*(int, int) add = (int a, int b) => a + b;

int main() {
    const int v = VERSION;        // 使用箇所でリテラル13に置換される
    if (DEBUG) { /* ... */ }      // bool値がインラインされ分岐条件になる
    const int sum = add(3, 5);    // 通常の関数呼び出しとして解決される
    const int next = VERSION + 1; // 式の中でも使える
    return 0;
}
```

ジェネリクスとの組み合わせでもマクロ側に特別な処理は不要で、単相化前に値がリテラル化されるため型引数推論と自然に共存する（`tests/common/generics/generic_with_macro.cm`）。

## データ構造とアルゴリズム

### 内部表現: ast::MacroDecl

マクロ宣言のAST表現は `ast::MacroDecl`（`src/internal/syntax/ast/module.hpp:161`）で、Kindとして Function・Attribute・Procedural・Constant を持つが、現行パイプラインで生成されるのは Constant のみである。

```cpp
// src/internal/syntax/ast/module.hpp:161
struct MacroDecl {
    enum Kind { Function, Attribute, Procedural, Constant };
    Kind kind;
    std::string name;
    TypePtr type;              // 定数の型
    ExprPtr value;             // 定数の値
    bool is_exported = false;  // エクスポートフラグ
};
```

### 関数マクロのパース時関数化

値がラムダ式のマクロは `parse_macro` 内で `ast::FunctionDecl` へ書き換えられる（`src/internal/syntax/parser/module/attribute.cpp:47-84`）。戻り値型は関数ポインタ型 `int*(int, int)` の戻り値型、ラムダの明示注釈の順で決定され、式本体 `=> expr` は `return expr;` 文へ変換される。以降は通常の関数と完全に同一の経路（型検査→HIR→MIR→codegen）を辿るため、関数マクロの呼び出しに展開処理は発生しない。

```cpp
// src/internal/syntax/parser/module/attribute.cpp:67-73（式本体のreturn文化）
if (lambda->is_expr_body()) {
    auto ret = std::make_unique<ast::ReturnStmt>();
    ret->value = std::move(std::get<ast::ExprPtr>(lambda->body));
    // ... Stmt化してbodyへ
}
```

### 定数マクロの型検査

型検査は `src/internal/types/checking/decl.cpp:403-480` で行われる。値を `evaluate_const_expr` でコンパイル時評価して整数値を取得し（`decl.cpp:451`）、宣言型が未定義型ならエラーを報告したうえで（`decl.cpp:467-470`）、グローバルスコープに `is_const = true` の変数として登録する（`decl.cpp:473`）。この登録によりマクロ名は配列サイズ等の定数文脈でも通常のconstと同様に使える。型検査器にもラムダ値のマクロを関数シグネチャとして登録する分岐（`decl.cpp:410-443`）があるが、パース時に関数化されるため通常は到達しない防御経路である。

### HIRでの定数インライン置換

HIR loweringの `lower_macro`（`src/internal/hir/lowering/decl.cpp:559`）は、リテラル値を型別のマップ `macro_values_`（int）・`macro_string_values_`・`macro_bool_values_`（`src/internal/hir/lowering/fwd.hpp:59-61`）へ登録し、さらにマクロ全体を `is_const = true` の `HirGlobalVar` としても出力する（`decl.cpp:643-651`）。式loweringは識別子参照をこれらのマップと突き合わせ、ヒットすれば変数参照ではなくリテラルノードを生成する。

```cpp
// src/internal/hir/lowering/expr.cpp:94-101（int型マクロのインライン置換）
auto macro_it = macro_values_.find(ident->name);
if (macro_it != macro_values_.end()) {
    auto lit = std::make_unique<HirLiteral>();
    lit->value = macro_it->second;
    return std::make_unique<HirExpr>(std::move(lit), ast::make_int());
}
```

置換はマップ登録後に処理される式にのみ効くが、置換されなかった参照も `HirGlobalVar` のconst読み出しとして同じ値に解決されるため、意味は変わらない（インライン化は最適化であって意味論の担い手ではない）。

### 衛生性（hygiene）の扱い

現行の型付きマクロには衛生性の問題がそもそも発生しない。定数マクロの展開結果はリテラル1個であり新しい識別子を導入しないため、呼び出し側の名前を捕獲する余地がない。関数マクロは実体が通常の関数であり、パラメータや本体のローカル変数は関数自身のレキシカルスコープに閉じる。制約はマクロ名がグローバルスコープの単一シンボル空間を通常のconst・関数と共有することで、同名定義の衝突はスコープ登録時の通常の重複診断として扱われる。

### 診断とソース位置

マクロ定義の構文エラーはパーサの `expect` 系（`=` や `;` の欠落等）が通常のトークン位置付きで報告する。型エラーは `ast::MacroDecl` を包む `Decl` のSpan（`attribute.cpp:88-92` で `Span{start_pos, previous().end}` として付与）を使って報告されるため、位置は元ソースのマクロ宣言行に対応する。使用箇所側は、型検査時点ではまだ置換が起きていない（グローバルconstとして解決される）ため、未定義マクロ名の参照は通常の未定義識別子診断として使用箇所の位置で報告される。HIRでの置換後は単なるリテラルであり、後段でマクロ由来を区別する必要はない。

### 参照実装: トークンツリー式宣言的マクロ（未接続）

`src/internal/macro/` には Rustの `macro_rules!` に相当する宣言的マクロの実装一式が存在する。`TokenTree`（単一トークン・デリミタ群・メタ変数 `$name:spec`・繰り返し `$(...)*`）を基本単位とし、フラグメント指定子は expr/stmt/pat/ty/ident/path/literal/block/item/meta/tt の11種（`src/internal/macro/token_tree.hpp:15-27`）、マクロ定義は「パターン→トランスクライバー」のルール列（`token_tree.hpp:160-170`）で表す。

- **マッチング**（`src/internal/macro/matcher.cpp:37`）: パターン列に対する再帰下降で、失敗時のエラー生成のため最深マッチ位置を記録する。繰り返しはセパレータ対応の貪欲ループで、1周で入力が進まない場合に打ち切る無限ループガードを持つ（`matcher.cpp:203-264`）。フラグメントマッチングはデリミタの深さ追跡による近似実装で、構文解析器を呼び出さない。
- **展開**（`src/internal/macro/expander.cpp:136`）: トークン列から `identifier!` 形式の呼び出しを検出し（`expander.cpp:415`）、ルールを先頭から試して最初にマッチしたトランスクライバーへバインディングを代入する。ネスト展開は `expand_all` の再帰で処理し、再帰深度上限（既定128）と展開トークン数上限（既定65536）を超えると `MacroExpansionError`（RECURSION_LIMIT / EXPANSION_OVERFLOW）を投げる（`src/internal/macro/expander.hpp:53-59`）。同一引数の展開結果はキャッシュされる。
- **衛生性**（`src/internal/macro/hygiene.cpp:62`）: 展開ごとに一意IDの `SyntaxContext` を割り当て、異なるコンテキストの同名識別子を検出した場合に `name_ctx{id}_{n}` 形式へリネームする方式で、`gensym` によるユニーク名生成も提供する。

この一式はCMakeのソース一覧（トップレベル `CMakeLists.txt` の cm_frontend 節）に含まれておらず、現行lexerの `TokenKind` ベースのTokenとは異なる旧API（`TokenType`・`value` フィールド）を前提としているためリンクもコンパイルもされない。将来 `name!(...)` 形式の宣言的マクロを導入する際の設計資産として保持されているものであり、現在のマクロの挙動を調査する際にこのディレクトリを読んでも実挙動とは一致しない点に注意する。

## 実装箇所

| ファイル | 役割 |
|---|---|
| `src/internal/syntax/parser/parser_decl.cpp:124,257,282` | `export macro` / `#macro` / `macro` の3入口から `parse_macro` へディスパッチ |
| `src/internal/syntax/parser/module/attribute.cpp:26` | `parse_macro` 本体。ラムダ値の関数化とMacroDecl生成 |
| `src/internal/syntax/ast/module.hpp:161` | `ast::MacroDecl` の定義（Kind・型・値・エクスポートフラグ） |
| `src/internal/types/checking/decl.cpp:403` | 定数マクロの型検査・const値評価・グローバルconst登録 |
| `src/internal/hir/lowering/decl.cpp:559` | `lower_macro`。リテラル値のマップ登録と `HirGlobalVar` 生成 |
| `src/internal/hir/lowering/fwd.hpp:59-61` | int/string/bool の名前→値マップ |
| `src/internal/hir/lowering/expr.cpp:94-125` | 識別子参照のリテラルへのインライン置換 |
| `src/internal/macro/token_tree.{hpp,cpp}` | （未接続）トークンツリー・フラグメント指定子・マクロルール表現 |
| `src/internal/macro/macro_parser.{hpp,cpp}` | （未接続）`macro_rules` 風定義のパーサ |
| `src/internal/macro/matcher.{hpp,cpp}` | （未接続）パターンマッチングとバインディング構築 |
| `src/internal/macro/expander.{hpp,cpp}` | （未接続）展開駆動・再帰/サイズ上限・キャッシュ・エラー型 |
| `src/internal/macro/hygiene.{hpp,cpp}` | （未接続）SyntaxContextによる識別子リネームとgensym |

## 落とし穴とケア

- **二重表現の値一致**: 定数マクロは「HIRインライン置換用のマップ」と「`HirGlobalVar` のconst初期化子」の2箇所に値を持つ。両者は同じ `macro.value` から導出されるという不変条件を維持すること。片方だけ変換規則を変えると、置換された参照と置換されなかった参照で値が食い違うバグクラスになる。
- **インライン置換はリテラル限定**: マップ登録は値が `ast::LiteralExpr` の場合のみ行われる。リテラル以外の定数式を許す拡張をする場合、置換経路ではなく型検査の `evaluate_const_expr` とグローバルconst経路が意味を担うことを理解して変更する。
- **関数マクロの唯一の経路はパース時変換**: 型検査器とHIR loweringにもラムダ値MacroDeclを処理する分岐が残っているが、これは防御的な到達不能コードである。関数マクロの挙動を変える際は `parse_macro` の変換を変更し、防御分岐との整合を保つ。
- **列挙・enum定数との解決順序**: 式loweringの識別子解決はenum定数→マクロマップ→通常変数の順で照合する（`expr.cpp:70-130` 付近）。この順序を入れ替えると同名シンボルの解決が変わるため、順序自体が仕様である。
- **未接続コードを実挙動と混同しない**: `src/internal/macro/` の展開器・衛生性機構は動作していない。`name!(...)` 構文やフラグメント指定子に関するバグ報告・仕様説明をこのコードに基づいて行わないこと。接続する場合は現行Token APIへの移植・ビルド登録・テスト整備が前提になる。
- **回帰テストの場所**: 統合テストは `tests/common/macro/`（`macro_basic.cm`・`typed_macro.cm`・`macro_function.cm` と対応する `.expect`）、ジェネリクスとの相互作用は `tests/common/generics/generic_with_macro.cm` にあり、バックエンドスイート（`make test-interpreter` / `make test-llvm` 等）で全バックエンド一致を検証する。

## 関連資料

- [コンパイルパイプライン全体像](../pipeline/overview.md) — マクロ処理が埋め込まれるパース段とHIR lowering段の位置付け
- [型推論](../types/inference.md) — マクロ値の型付けが従う式推論の一般規則
- [モジュール解決](../modules/import-resolution.md) — `export macro` がモジュール間で可視になる仕組み
- マクロのチュートリアル: `docs/tutorials/ja/advanced/macros.md`
- 条件付きコンパイル（別機構）: `docs/tutorials/ja/compiler/common/preprocessor.md`
