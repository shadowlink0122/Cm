---
title: 属性処理の設計（#[test]・#[derive]・#[target]等）
---

# 属性処理の設計（#[test]・#[derive]・#[target]等）

Cmの属性は `#[name(args...)]` / `@[name(args...)]` の統一構文でパースされ、名前と文字列引数の組（`AttributeNode`）として各宣言ノードに保持される。属性の意味づけはパーサでは行わず、消費フェーズが分散する設計になっている。`#[target]`/`#[test]` の宣言フィルタはパース直後のAST走査、`#[test]` のシグネチャ検査は型検査、テスト収集はMIRを見るJITランナー、`#[derive]` は `with` 句と合流して型検査の検証を経てMIRの自動実装生成へ、SV系属性は文字列のままHIR/MIRを素通りしてSVコード生成が解釈する。

## 概要

属性は宣言（parser_decl.cpp:55-57）・構造体フィールド（parser_decl.cpp:538-541）・impl/interface内メソッド（parser_decl.cpp:811-814, 942-945）の3箇所で `parse_attribute()` によりパースされ、それぞれの宣言ノードの `attributes` ベクタに載る。属性名は `sv::pin` のような名前空間区切りを連結した1つの文字列、引数は識別子・文字列・整数・`!ident`・`key:value` を全て文字列化したベクタである（src/internal/syntax/parser/module/attribute.cpp:152-237）。

grepで確認できる全属性とその消費先は次の通り。

| 属性 | 付与対象 | 消費フェーズと実装 |
|---|---|---|
| `#[test]` | 関数 | パース直後の宣言フィルタ（非testモードで除去、target_filtering_visitor.hpp:89）、型検査のシグネチャ検査（types/checking/decl.cpp:1019-1030）、LLVM変換の到達可能性ルート（llvm/core/translate/program.cpp:385-399）、JITテストランナーの収集（cmd/cm/backend/run.cpp:79-90）、SVテストベンチ生成（mir/lowering/impl.cpp:175-184） |
| `#[target(...)]` | 関数・struct・enum・impl・グローバル変数・use/import・externブロック等 | パース直後の `TargetFilteringVisitor` が宣言を除去（target_filtering_visitor.hpp:92-97） |
| `#[derive(...)]` | struct（enumは明示エラー） | パーサで `with` 句の `auto_impls` へ合流（parser_decl.cpp:486-496）→型検査で導出可能性を検証（types/checking/auto_impl.cpp:15-32）→MIRの `AutoImplGenerator` がメソッド本体を生成（mir/lowering/auto_impl/generator.hpp:18-47） |
| `#[input]` / `#[output]` / `#[inout]` | グローバル変数・structフィールド | パーサがSVポートと判定して初期化子省略を許可（parser/module/toplevel.cpp:66-73）、SVコード生成がポート方向として解釈（codegen/sv/analyze.cpp:379-381等） |
| `#[sv::*]` / `#[verilog::*]`（param・parameter・module_name・pin・iostandard・tri・sync・memfile・bram・lutram等） | グローバル変数・struct・関数 | SVコード生成のみ（codegen/sv/analyze.cpp・hierarchy.cpp・constraints.cpp）。native/jitでは文字列として素通りし無視される |
| `#[cfg(...)]` | 任意 | パースで条件文字列を保存するのみで（attribute.cpp:207-228）、評価する消費者は存在しない |
| `@[macro]` | — | パーサが拒否する廃止構文（parser_decl.cpp:60-64、`#macro` 宣言構文を使う） |
| `__prelude` | enum（コンパイラが内部注入） | 型検査がResult/Option注入enumに付けるマーカーで、組み込みメソッド消去の抑止（types/checking/decl.cpp:51）とlint命名検査の除外（types/checking/utils/naming.cpp:203）に使う。ユーザー構文ではない |
| `must_use` | —（属性ではない） | Result型に組み込みの暗黙検査。式文でResult値を捨てると警告する（types/checking/stmt.cpp:134-152）。`#[must_use]` という属性構文は存在しない |

## データ構造とアルゴリズム

### AttributeNodeとパース

属性のAST表現は名前と文字列引数だけの最小構造で、型付き引数や式は持たない。

```cpp
// src/internal/syntax/ast/module.hpp:20-25
struct AttributeNode {
    std::string name;
    std::vector<std::string> args;
    ...
};
```

`parse_attribute()` は `@[...]` と `#[...]` の両形式を受け、`::` 連結の名前空間付き名をパースし、引数の `key: value` 形式は `"key:value"` という1つの文字列に畳んで保持する（attribute.cpp:181-199）。この「全部文字列」という設計により、パーサは未知の属性名でもエラーにせず素通しでき、新しい属性の追加はパーサ変更なしで消費側の実装だけで完結する。裏返しとして属性名のtypoは検出されない（落とし穴の節を参照）。

### ターゲットフィルタリング（パース直後の宣言除去）

`TargetFilteringVisitor` はパース完了直後・型検査より前にProgram全体を走査し、`#[target(...)]` の条件を満たさない宣言と、非testモードの `#[test]` 宣言をASTから物理的に削除する（src/cmd/cm/build.cpp:307-321、lint経路はsrc/cmd/cm/check.cpp:114）。型検査前に消すため、他ターゲット専用のコード（例: JS専用API呼び出し）が現在のターゲットで型エラーになることを防ぐ。これはRustの `#[cfg(...)]` による条件コンパイルに相当する位置づけである。

```cpp
// src/internal/syntax/ast/target_filtering_visitor.hpp:86-97（要約）
bool check_target_attributes(const std::vector<AttributeNode>& attrs) {
    for (const auto& attr : attrs) {
        if (attr.name == "test" && !include_tests_) return false;
        if (attr.name == "target" && !check_target_condition(attr.args)) return false;
    }
    return true;
}
```

条件の意味論は「1つの `#[target(a, b)]` 内の引数はOR、複数の `#[target]` 属性はAND」で、`!js` のような否定と、`js` がJS/Web両ターゲットにマッチする別名解決を持つ（target_filtering_visitor.hpp:110-165）。トップレベル宣言のほかimplメソッドとexternブロック内宣言も再帰的にフィルタする（同:182-216）。

### #[test]の伝播と3系統の消費者

`#[test]` はフィルタを生き残った後、型検査が「引数なし・戻り値void」を強制する（types/checking/decl.cpp:1019-1030）。これはJITテストランナーとSVテストベンチの双方が前提とする呼び出しシグネチャの保証である。属性はHIRで名前文字列となり（hir/lowering/decl.cpp:94-96）、MIRの `MirFunction::attributes` へコピーされる（mir/lowering/impl.cpp:172）。消費者は3系統ある。

1. **JITテストランナー**: `cm test` はMIR関数の `attributes` から `"test"` を持つ関数を宣言順に収集し、関数ごとに独立したJITエンジンで実行する（run.cpp:79-90）。実行機構の詳細は[LLJITエンジン](../codegen-jit/lljit-engine.md)を参照。
2. **LLVM変換の到達可能性**: テスト関数はどこからも呼ばれないため、デッドコード除去の到達可能性走査で `#[test]` をルート扱いにして生成対象へ残す（program.cpp:388-399）。
3. **SVテストベンチ**: SVターゲットではテスト関数のHIR文を保持してテストベンチ生成に使う（mir/lowering/impl.cpp:175-184、sv/validation.cpp:212）。

### #[derive]とwithの合流

`#[derive(Eq, Clone)]` はパーサの段階で `with Eq, Clone` 句と同一の `StructDecl::auto_impls` リストへ合流するため（parser_decl.cpp:486-496）、以降のフェーズに「derive由来かwith由来か」の区別は存在しない。型検査は導出可能セット（Eq・Ord・Copy・Clone・Hash・Debug・Display・Css）とフィールド型の妥当性を検証し、未知のインターフェイスと導出不能なインターフェイスを区別した診断を出す（auto_impl.cpp:15-32）。実際のメソッド本体はMIR loweringの `AutoImplGenerator` が生成し、ジェネリック構造体は単相化後にインスタンスごとの実装を生成する（generator.hpp:35-47）。生成される実装の内容と演算子正規化は[静的ディスパッチ](../interface/static-dispatch.md)が扱う。enumへの `#[derive]` はパーサで明示エラーにして黙った無視を防ぐ（parser/module/toplevel.cpp:267-272）。

### SV系属性の文字列パススルー

`#[sv::pin("U12", io_type: "LVCMOS33")]` のような引数付き属性はnative/jitパイプラインでは何も意味を持たず、SVコード生成まで運ばれてから解釈される。ここに表現上の非対称がある。HIRの属性は関数・フィールド・initialブロックでは名前のみの `std::vector<std::string>` に落ちる（hir/lowering/decl.cpp:94-96, 163-165）のに対し、グローバル変数だけは引数を保存するために `name("arg1", "arg2")` 形式へシリアライズされる。

```cpp
// src/internal/hir/lowering/decl.cpp:526-540（要約）
for (const auto& attr : gv.attributes) {
    if (attr.args.empty()) {
        hir_global->attributes.push_back(attr.name);
    } else {
        std::string attr_str = attr.name + "(";
        ...  // "\"arg\"" をカンマ連結
        hir_global->attributes.push_back(attr_str);
    }
}
```

SVコード生成側は `parse_attr_args` がこの文字列を再パースして引数列へ復元する（codegen/sv/constraints.cpp:31-56）。native/jitの観点では「`cm_*` 命名のSV属性はMIRの文字列ベクタに残るが、どのコード生成器も参照しない」ことだけが不変条件である。

### must_use検査（属性を使わない暗黙規則）

Rustの `#[must_use]` に相当する機能は属性としては存在せず、型検査が式文の型を推論して基底名が `Result` なら未使用警告を出す組み込み規則として実装されている（types/checking/stmt.cpp:134-152、match式文は除外）。属性で個別型into-optできる仕組みではないため、ユーザー定義型へmust_use性を付与する手段は現状ない。

## 実装箇所

| ファイル | 役割 |
|---|---|
| src/internal/syntax/parser/module/attribute.cpp | `parse_attribute()` 本体（#[]/@[]、名前空間名、key:value引数、cfg条件文字列） |
| src/internal/syntax/ast/module.hpp:20-25 | `AttributeNode`（名前+文字列引数）の定義 |
| src/internal/syntax/parser/parser_decl.cpp:55-57, 538-541, 811-814 | 宣言・フィールド・メソッドの属性パース呼び出し位置 |
| src/internal/syntax/parser/parser_decl.cpp:486-496 | `#[derive]` → `auto_impls` 合流 |
| src/internal/syntax/ast/target_filtering_visitor.hpp | `#[target]`/`#[test]` によるパース直後の宣言フィルタ |
| src/cmd/cm/build.cpp:307-321, src/cmd/cm/check.cpp:114 | フィルタの起動位置（ビルド経路・lint経路） |
| src/internal/types/checking/decl.cpp:1019-1030 | `#[test]` のシグネチャ強制 |
| src/internal/types/checking/auto_impl.cpp | derive/withの導出可能性検証と自動impl登録 |
| src/internal/mir/lowering/auto_impl/ | `AutoImplGenerator`（Eq/Ord/Clone/Hash/Debug/Display/Cssの本体生成） |
| src/internal/hir/lowering/decl.cpp:94-96, 526-540 | AST属性→HIR文字列化（グローバル変数のみ引数シリアライズ） |
| src/internal/mir/lowering/impl.cpp:172-184 | MIRへの属性コピーと `#[test]` のHIR文保持 |
| src/cmd/cm/backend/run.cpp:79-90 | JITテストランナーのMIR属性走査による収集 |
| src/internal/codegen/llvm/core/translate/program.cpp:385-399 | `#[test]` の到達可能性ルート化 |
| src/internal/codegen/sv/constraints.cpp:31-56 | シリアライズ済み属性文字列の再パース（SV境界） |
| src/internal/types/checking/stmt.cpp:134-152 | Result未使用のmust_use検査（属性なし） |

## 落とし穴とケア

- **未知の属性名は黙って無視される**: パーサは属性名を検証せず、消費側も自分の知る名前だけを拾うため、`#[tset]` のようなtypoは診断なしで機能が無効になる。特に `#[test]` のtypoは「テストが収集されない」形で顕在化する（テスト0件はランナーがエラーにするため全滅時のみ気づける、run.cpp:91-94）。新属性を足す際は消費側で近似名の警告を検討する余地がある。
- **引数付き属性はHIRで引数が落ちる（グローバル変数を除く）**: 関数・フィールドの属性はHIRで名前のみになるため、引数を持つ新属性を関数に導入しても消費側に引数は届かない。引数が必要ならグローバル変数と同じ `name("args")` シリアライズをhir/lowering/decl.cppの該当箇所へ広げるか、AST段階で消費する設計にする。
- **`#[test]` 関数は非testビルドのASTに存在しない**: フィルタがパース直後に宣言を削除するため、型検査以降のフェーズは非testモードで `#[test]` 関数を一切観測しない。テスト関数内のコンパイルエラーは `cm test` 実行時に初めて報告されることを意味する。
- **`#[target]` の複数属性はAND・引数はOR**: `#[target(js, wasm)]` は「jsまたはwasm」、`#[target(js)] #[target(!web)]` は「jsかつweb以外」である。この意味論はtarget_filtering_visitor.hpp:98-108のコメントにも明記されており、変更は既存コードの条件コンパイル結果を静かに変える破壊的変更になる。
- **`#[cfg]` と `#` ディレクティブ形式は未接続**: `#[cfg(...)]` は条件文字列を保存するだけで評価器がなく、`#name` 形式を読む `parse_directive()`（attribute.cpp:98-147）は呼び出し元が存在しない。どちらも構文だけが先行しているため、ドキュメントやチュートリアルでサポート済みとして扱わないこと。
- **属性の意味はターゲット文脈で変わる**: 同じ `#[test]` でもJITランナー実行とSVテストベンチ生成では実行モデルが異なり、SV系属性はnative/jitでは完全に無視される。属性を増やす際は「他ターゲットで無視されるべきか、エラーにすべきか」を明示的に決め、enumのderive拒否（明示エラー）とSV属性（黙って無視）のどちらの方針に載せるかを選ぶ必要がある。

## 関連資料

- [LLJITエンジン（#[test]のJIT実行機構）](../codegen-jit/lljit-engine.md)
- [静的ディスパッチ（derive自動実装の生成内容）](../interface/static-dispatch.md)
- [コンパイルパイプライン全体像](overview.md)
- [test属性設計（アーカイブ）](../../archive/v0.16.0/06_test_attribute.md)・[derive属性設計（アーカイブ）](../../archive/v0.16.0/10_derive_attribute.md)
- [target属性再設計案（未実装アーカイブ）](../../archive/unimplemented/target_attribute_redesign.md)
