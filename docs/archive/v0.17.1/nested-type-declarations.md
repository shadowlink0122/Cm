# ネスト型宣言（struct内struct・enum内enum）と修飾アクセスチェーン

## 目的

struct本体・enum本体の中に別のstruct/enum型を宣言できるようにし、外部からは `Outer::Inner` という修飾型パス、enum値は `Outer::Inner::MEM` という修飾チェーンでアクセス可能にする。
インスタンスのメンバチェーン `o.inner.mem` は既存機能で動作するため、本機能は「型宣言のネスト」と「型名前空間のチェーン解決」を追加するものである。

## 現状調査の結果

- `o.inner.mem` のような任意段数のメンバチェーンは既に動作する。
- `Outer::Inner` 型パスは `parse_type`（`src/internal/syntax/parser/type.cpp` の `::` 連結ループ）で既にフラット名 `"Outer::Inner"` の `NamedType` としてパースされる。
- `Outer::Inner::MEM` 式は `primary.cpp` の `::` 連結ループで既にフラット名の `IdentExpr` としてパースされる。
- struct本体はメンバの型開始トークンのみ受理するため `struct` キーワードで「Expected type」エラーになる（`parser/decl.cpp` のフィールドループ）。
- enum本体は `expect_ident` が予約語 `enum` を拒否する（`parser/module/toplevel.cpp` のメンバループ）。
- namespace（`mod`）は宣言名を `parent::name` に一時リネームして登録する前例があり、型テーブル・HIR・MIR・各バックエンドは `::` を含むフラット名をそのまま扱える。
- 文法ドキュメントに記載のあるenumのstruct風variant `Circle { int r; }` は実際にはパースされないため、enum本体への `enum`/`struct` キーワード宣言の追加に構文上の衝突はない。

## 構文仕様

```
struct_member ::= field_decl
                | nested_type_decl

nested_type_decl ::= struct_decl        # struct Inner { ... };（末尾;は任意）
                   | enum_decl          # enum Mode { A, B }（末尾;は任意）

enum_body_item ::= enum_variant
                 | nested_type_decl    # enum Inner { ... } または struct S { ... }（区切りカンマは variant と同様に任意）
```

使用例:

```cm
struct Outer {
    struct Inner {
        int mem;
    };
    enum Mode {
        FAST,
        SLOW,
    }
    Inner inner;
    Mode mode;
};

enum Category {
    enum Sub {
        MEM,
        REG,
    },
    A,
    B,
}

int main() {
    Outer o;
    o.inner.mem = 42;            // インスタンスのメンバチェーン（既存機能）
    o.mode = Outer::Mode::FAST;  // ネストenum値の修飾チェーン
    Outer::Inner i;              // ネスト型を修飾パスで使用
    Category c = Category::A;
    Category::Sub s = Category::Sub::MEM;
    return 0;
}
```

## 意味論

- ネスト型は外側の型の**名前空間に属する独立した型**であり、C++のネストクラスに相当する（外側のインスタンスとメモリ上の関係は持たない）。
- 正準名はフラット名 `Outer::Inner` で、ネスト段数は任意（`Outer::Mid::Inner`）。
- 外側の型本体の中では、ネスト型を非修飾名（`Inner`）または部分修飾名（`Mid::Inner`）で参照できる（内側スコープ優先）。
- 外側の型本体の外では、完全修飾パス（`Outer::Inner`）で参照する。
- enum本体のネスト型宣言はvariantの値割り当てに影響しない（値スロットを消費せず、オートインクリメントは素通しする）。
- ネスト型の可視性は外側の型に追従する（`export struct Outer` のネスト型はexport扱い）。
- 名前解決はhoist時の書き換えで実現するため、型チェッカー以降に新しいスコープ概念は導入しない。

### 初版の制限（明示診断を追加）

- ジェネリックstruct/enumの本体にはネスト型を宣言できない（monoの実体化単位が型単位でないため将来拡張とする）。
- ネスト型宣言自身にジェネリックパラメータは付けられない。
- 匿名ネスト（`struct { ... } field;`）と宣言同時フィールド（`struct Inner { ... } field;`）は未対応（将来拡張。フィールドは別行で `Inner field;` と書く）。
- extern struct内のネスト型宣言は未対応。
- フィールド既定値式（IO/externフィールド）の中からネスト型のenum値を参照する場合は完全修飾で書く（非修飾名の書き換えは型参照のみが対象）。

## 実装方針

パース後のASTパス（hoist）でネスト型をトップレベルへ平坦化し、型チェッカー以降は既存のフラット名機構をそのまま使う。

1. **AST拡張**: `StructDecl`/`EnumDecl` に `nested_types`（`std::vector<DeclPtr>`）を追加する。
2. **パーサ**: struct本体のフィールドループとenum本体のメンバループの先頭で `KwStruct`/`KwEnum` を検出したら `parse_struct`/`parse_enum_decl` を再帰呼び出しし、`nested_types` に格納する。
3. **hoistパス**（新規 `src/internal/syntax/ast/nested.cpp`）: `run_frontend` のパース直後に実行する。
   - 各トップレベルstruct/enum（`mod` 本体の宣言も再帰対象）について `nested_types` を深さ優先で取り出し、名前を `Outer::Inner` に書き換えて外側宣言の**直前**に挿入する（レイアウト依存の前方参照を避けるため内側が先）。
   - スコープスタック（単純名→フラット名）を保持し、外側本体内の型参照（フィールド型・enum連想データ型・ジェネリック引数・ポインタ/参照/配列要素・union構成型）の先頭セグメントを書き換える。
   - `mod` 内の型は相対フラット名でhoistし、namespace修飾は既存の登録機構（`namespace.cpp`）に委ねる。
4. **フォーマッタ**: トークン/行ベースであるためネストブレースは既存ロジックで整形される（AST変更の影響なし）。検証テストのみ追加する。
5. **診断**: 上記制限のための `MsgId` を追加する（ja/en両方）。

この方式により、型チェッカー・HIR・MIR・全バックエンド（interpreter/llvm/js/sv）は変更不要となる見込みである（namespace型で `::` 名は実証済み）。

## 段階分割

1. AST拡張＋パーサ受理（struct内struct・struct内enum・enum内enum・enum内struct、任意深度）
2. hoistパス実装と `run_frontend` への組み込み、制限診断の追加
3. テスト（integration全バックエンド＋regression＋エラーケース）
4. ドキュメント（文法・チュートリアルja/en・VSCode拡張・リリースノート）

## テスト計画

- `tests/common/types/structs/nested/`: struct内struct（2段・3段）、struct内enum、フィールドチェーン `o.inner.mem`、修飾型パスでの変数宣言 `Outer::Inner i;`、implによるネスト型へのメソッド定義。
- `tests/common/types/enum/nested/`: enum内enum、`Outer::Inner::MEM` アクセス、外側variantの値割り当てが素通しであること、match文での使用、ネストenumのswitch/比較。
- `tests/common/errors/`: ジェネリック外側へのネスト宣言、ネスト宣言自身へのジェネリクス付与、未定義ネスト型参照。
- regression: パーサ受理＋hoist結果の検証、fmtがネスト定義を保存整形すること。
- SVバックエンドテストはファイル名アンダースコア規約に従う。

## 将来拡張

- 宣言同時フィールド `struct Inner { ... } field;` と匿名ネスト。
- ジェネリックstruct/enum本体へのネスト宣言とネスト型自身のジェネリクス。
- interface/implのネスト宣言。

## 追記（実装後）

宣言同時フィールドと匿名ネストは同バージョン（v0.17.1）でC/C++スタイルの単一宣言として実装済み。
匿名型は先頭宣言子から `__anon_<宣言子>` 名を合成し、トップレベルの宣言子はゼロ初期化のグローバル変数を合成する（詳細はCANONICAL_SPEC 6.3節）。
