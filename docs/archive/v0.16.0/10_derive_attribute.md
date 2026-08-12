# v0.16.0 実装設計 10: `#[derive(...)]` 属性による自動実装（`with` 構文と併存）

優先度: 追加（ユーザー要望 2026-07-11）目的: interface自動実装の指定手段として Rust と同形の `#[derive(INTERFACE, ...)]` 属性を追加する。**既存の `with` 構文はそのまま残し、破壊的変更を発生させない。**

## 背景

現在の自動実装は `with` 構文で指定する:

```cm
struct Point with Eq, Ord {
    int x;
    int y;
}
```

v0.16.0 で属性基盤（`#[test]`・`#[target]`・`#[sv::pin]` 等）が整備され、構造体宣言も属性を受け取れるようになった（`parse_struct` は既に `attributes` を受理する）。
自動実装は「宣言に対するコンパイラへのメタ指示」であり属性の領分でもあるため、Rust ユーザーに馴染みのある `#[derive(...)]` を等価な指定手段として追加する。

## 互換性方針（本設計の前提）

- `with` 自動実装は v0.9.0 からのリリース済み機能であり、**削除・非推奨化は行わない**。
- `#[derive(...)]` は `with` の**別記法（シンタックス上のエイリアス）**として追加し、両者は同一の合成機構へ接続する。
- 両記法に機能上の差別化は設けない: 導出可能インターフェース（コンパイラ組み込みの8種）・意味論・検証・診断のすべてを共有し、違いは記法のみとする。
- 新規コード・チュートリアルでは `#[derive]` を推奨記法として案内するが、`with` を使う既存コードは無修正で動き続ける。
- 両構文を統合するか否かの最終判断は v0.18.0「言語仕様の完成と凍結」の予約語・構文整理で行う（それまで両対応を維持する）。
- 関連提案だった「`with` の構造体メンバ埋め込みへの転用」は**見送り**とした（経緯と理由は [archive/unimplemented/with_struct_embedding.md](../../archive/unimplemented/with_struct_embedding.html) を参照）。

## 構文

```cm
#[derive(Eq, Ord)]
struct Point {
    int x;
    int y;
}

// 複数の derive 属性は単純にマージされる
#[derive(Eq)]
#[derive(Clone, Hash)]
struct Entry {
    int key;
    string value;
}

// 従来の with 構文も引き続き有効（同じ意味）
struct Color with Eq {
    int r;
    int g;
    int b;
}
```

- 引数は裸識別子のカンマ区切りのみ（現行属性パーサの bare identifier 形式で追加実装なしに表現できる）。
- 付与対象は struct 宣言のみ。enum への付与は v0.16.0 では明示エラー（将来拡張の余地として予約）。
- `#[derive]`（引数リストなし）と `#[derive()]`（空リスト）はエラー。
- `#[target(...)]` 等の他属性との併記は順序不問。

## 導出可能インターフェース

導出対象と合成メンバは現行 `with` と完全に同一（変更なし）:

| インターフェース | 合成されるメンバ |
|---|---|
| `Eq` | `operator==` / `operator!=`（全フィールドの比較） |
| `Ord` | `operator<` / `>` / `<=` / `>=`（辞書順比較） |
| `Copy` | なし（マーカー） |
| `Clone` | `clone()` |
| `Hash` | `hash()` |
| `Debug` | `debug()` |
| `Display` | `toString()` |
| `Css` | `css()` / `to_css()` / `isCss()` |

導出可能セットをユーザー定義interfaceへ拡張する構想（フィールドイントロスペクション等の前提機能を含む）は [設計11](11_user_defined_derive.html) で前方検討する。

### 導出可能セットの十分性評価（2026-07-11）

- Rust標準のderive（Clone / Copy / Debug / Default / Eq / PartialEq / Hash / Ord / PartialOrd）との比較で、Cmに未提供なのは **Default のみ**（Eq/PartialEq等の区別はCmの型系に存在しないため対象外）。
- Default導出は「インスタンスなしで呼ぶメソッド」＝静的メソッド呼び出し構文（`Point::default()` 相当）が前提となるため、v0.16.0では追加しない。宣言時のゼロ値初期化（`Point p;`）が既定値を提供しており実用上の穴は小さい。静的メソッド構文の設計とセットで v0.18.0（仕様凍結）までに導入可否を判断する。
- Serialize（JSON等）は組み込み追加せず、設計11のユーザー定義derive（`fields(T)` 反復）の主要ユースケースとして扱う。
- バックエンド別の利用可否は [バックエンド対応マトリクス](../../design/backend_support_matrix.html) に記載する（バックエンドごとに利用可能なセットが異なることを許容する）。

### フィールド型ごとの対応（v0.16.0実装時点）

| フィールド型 | Eq | Ord | Hash | Debug/Display | Clone/Copy |
|---|---|---|---|---|---|
| 整数・bool・char | ✅ | ✅ | ✅ | ✅ | ✅ |
| float/double | ✅ | ✅ | ❌ エラー | ✅ | ✅ |
| string | ✅ | ✅ | ❌ エラー | ✅ | ✅ |
| ネスト構造体 | ✅ 再帰 | ✅ 再帰 | ✅ 再帰 | ✅ 再帰 | ✅ |
| 固定長1次元配列 | ✅ 要素比較 | ❌ エラー | ✅ 要素混合（整数系要素のみ） | ❌ エラー | ✅ |
| 多次元配列・動的スライス | ❌ エラー | ❌ エラー | ❌ エラー | ❌ エラー | ✅ |
| ユニオン型 | ❌ エラー | ❌ エラー | ❌ エラー | ❌ エラー | ✅ |

❌は「不正なコード生成の代わりに明示エラー」を意味する（従来は配列・string等の組み合わせで不正なLLVM IRを生成していた）。対応拡大は需要に応じて行う。

## セマンティクス

- `#[derive(I1, I2)]` は `with I1, I2` と同一の合成機構へ接続する: `StructDecl::auto_impls` → `TypeChecker::register_auto_impl`（`frontend/types/checking/auto_impl.cpp`）→ MIR Pass 1.5（非ジェネリック）/ Pass 5（モノモーフィゼーション後）。
- 同一 struct への `with` と `#[derive]` の併用は許可し、リストは union される。
- 同一インターフェースの重複指定（`#[derive(Eq, Eq)]`、複数 `#[derive]` 間、`with Eq` + `#[derive(Eq)]`）はエラー（記述ミス検出を優先。既存コードは単一機構でしか書かれていないため破壊的変更にはならない）。
- 指定できる名前は両記法とも導出可能セット（上表8種、コンパイラ組み込み）に限定する。未知の名前・セット外の interface は `with` / `#[derive]` のどちらでも同一のエラーとし、`impl <型> for <interface>` の使用を促す。
- これは `with` 側の検証強化（バグ修正）を含む: 現行実装は `interface_names_` への存在チェックのみで、導出可能セット外のユーザー定義 interface を `with` に書くとメソッド合成なしに `impl_interfaces_` へ登録され「実装済み」扱いになる（`auto_impl.cpp:9-53`）。この形を受理しても合成は行われず、別途 `impl` があれば冗長・なければ潜在バグのため、有効なプログラムを壊さないバグ修正として両記法一律のエラーに統一する（リポジトリ内のテスト・examples は導出可能セットのみを使用しており影響なし）。
- 属性名 `derive` はコンパイラ認識属性として扱う。未知属性が無警告で無視される現行ポリシーはタイポ（`#[drive(Eq)]` 等）を握り潰すため、既知属性レジストリによる未知属性警告の導入を検討課題として残す。

## 実装

- `frontend/parser`: `parse_struct`（`parser_decl.cpp`）で受理済みの `attributes` から `name == "derive"` を抽出し、引数列を `StructDecl::auto_impls` へ追記する。`with` 経路（`parser_decl.cpp:463-469`）は**無変更**。
- `frontend/ast`: `StructDecl::auto_impls` をそのまま利用する（derive の引数も同じリストへ追記）。意味論が同一のため由来（with / derive）の区別は保持せず、重複・エラー診断の位置表示用に各エントリの Span を持たせる程度に留める。
- `frontend/types`: `register_auto_impl` の呼び出し元（`checking/decl.cpp:238-239`）に、導出可能セット検証と重複検出を記法共通の単一経路として実装する。
- HIR/MIR: 変更なし（`auto_impls` の内容が同じであれば下流は既存機構がそのまま動く）。
- `vscode-extension`: 属性ルール（`syntaxes/cm.tmLanguage.json` の `#attributes`）で `derive` は既に汎用ハイライトされる。derive 引数のインターフェース名を型名スコープ（`entity.name.type`）で塗る改善を実装時に行う。`with` のハイライトは現状維持。
- ドキュメント（実装完了時に更新）: CANONICAL_SPEC §3.2・§6（`#[derive]` を正式構文に昇格させ「間違い」例から削除、`with` も有効と明記）、FEATURES.md、cm_grammar.md、QUICKSTART、チュートリアル（ja/en は `#[derive]` を推奨記法として掲載し `with` を互換記法として併記）。
- リリースノート: `docs/releases/v0.16.0.md` に**追加機能**（破壊的変更なし）として記載する。

## テスト計画

- 既存の `tests/common/interface/with_*.cm`（12ケース）は**そのまま維持**する（`with` が引き続き正式構文であることの回帰テストを兼ねる）。
- 新規に `derive_*.cm` を追加し、`with_*.cm` と同一の期待出力になることを確認する（全バックエンド）。
- 併用: 同一 struct での `with Eq` + `#[derive(Clone)]`（union 動作）、`#[target(...)]` + `#[derive(...)]` の共存、複数 `#[derive]` 属性のマージ。
- エラー系: 未知名（`#[derive(Foo)]`）、導出可能セット外のユーザー定義 interface（`with` / `#[derive]` の両記法で同一エラーになること）、重複指定（derive 内・derive 間・with と derive 間）、引数なし `#[derive]`、enum への付与。
- ジェネリック構造体: `#[derive(Eq)] struct Pair<T, U>`（現行 `with_generic_pair.cm` 相当）。
