---
title: モジュール可視性の強制と選択importの重複排除
parent: v0.17.0 Design
---

# モジュール可視性の強制と選択importの重複排除

本文書は監査レポート `docs/design/v0.17.0/large-scale-bottleneck-audit.md` のH7・M7・M2に対する実装設計である。
これら3所見はいずれも「preprocessorのimport展開が可視性とファイル同一性を正しく扱えていない」ことに帰着し、単一の一貫した設計で対処する。

## 対象所見

| # | 領域 | 所見 | 状態 |
|---|------|------|------|
| H7 | 言語 | モジュールの`export`が自由関数に対して未強制で、非export関数を他モジュールからimportして呼べる（カプセル化が機能しない） | 未着手 |
| M7 | コンパイル時間 | 選択importの重複排除漏れ（`import std::collections::Vector; import std::json;`で「Duplicate method」例外）、パスベースimportは正しく排除される | 実装済み（真因は3点: (1)大文字開始の末尾セグメントがitemにならず大小文字非区別FSでVector.cm→vector.cmが誤解決されnamespace Vectorとstruct Vectorが衝突、(2)同一ファイルからの2回目以降の選択importが展開済みの型・impl・ネスト領域を再出力、(3)export import構文が未処理でmod.cm経由の選択importが本体を取り込めない。expand.cppの型名item再解釈+filter_exportsの増分モード+ワイルドカード再exportの展開で解消。選択的再export（export import x::{items}、io形式）はMIR組み込みprintln等の意味論を壊すため展開せず素通しを維持） |
| M2 | 識別子 | 同名シンボルの多重import（`import a::{compute}; import b::{compute};`）がエラーにならず先勝ちで黙って解決される | 未着手 |

## 背景と根本原因

### H7: exportが自由関数に強制されない

選択importのフィルタは `ImportPreprocessor::filter_exports`（src/internal/preprocessor/export/extract.cpp:21）が行う。
この関数は`export`で始まる宣言ブロックを検出し、`import_items`に含まれるものだけを出力する設計だが、末尾の分岐（extract.cpp:313-316）で「exportで始まらない行はそのまま保持する」ため、非export関数の定義行がすべて素通しで出力に残る。
結果として`import mod::{compute}`で`compute`が非exportでも、その関数本体は`export`キーワードを持たない通常行の集合として通過し、呼び出し可能になる。

さらに`ImportPreprocessor::extract_exported_blocks`（extract.cpp:353）は、`has_export_functions`が真のとき非export定義（内部ヘルパー関数）をnamespace外へ複製する（extract.cpp:504-622, 657-664）。
これはexport関数が内部ヘルパーを参照できるようにするための意図的処理だが、非exportシンボルをnamespace修飾なしのグローバル名で公開してしまい、カプセル化を破る副作用を持つ。

### M7: 選択importと間接importで重複排除キーが異なる

`process_imports`（src/internal/preprocessor/import/expand.cpp:23）は2種類の重複排除表を持つ。

- 選択import（`mod::{items}`）は`imported_symbols[canonical_path]`にシンボル名単位で登録する（expand.cpp:347-388）。
- ワイルドカード・モジュール全体importは`imported_modules.count(canonical_path)`でファイル単位に登録する（expand.cpp:378-386）。

この2表は互いを参照しない。
`import std::collections::Vector;`は`imported_symbols`に`Vector`を登録するが`imported_modules`には登録しない。
続く`import std::json;`が内部で`std::collections`をモジュール全体importすると、`imported_modules`側は初回とみなして`std::collections`の本体（Vectorのimpl群を含む）を再度展開する。
その結果、同一ファイル由来のimplメソッドが二重に出力され、型検査で `TypeChecker::register_impl` の重複検出（src/internal/types/checking/decl.cpp:698-702）が `throw std::runtime_error("Duplicate method: ...")` を投げる。

`process`が持つ`imported_files`（正規化パス集合、src/internal/preprocessor/import/setup.cpp:172,185-197）はキャッシュ・フィンガープリント用途のみで、再展開の抑止には使われていない。

### M2: 同名シンボルの多重importが先勝ちで黙る

`import a::{compute}; import b::{compute};`では`a`と`b`は別canonical pathなので、`imported_symbols`は衝突を検知せず両方の`compute`本体を出力に残す。
型検査の`Scope::define_function`（src/internal/types/scope.cpp:30-46）は既存名があると`return false`で黙って登録をスキップし（scope.cpp:32-33）、呼び出し側（decl.cpp:260, 460ほか）は戻り値を無視する。
このため後勝ちのdefineは捨てられ、先に定義された`compute`が黙って選ばれる（診断なし）。

## 設計方針

3所見を貫く方針は「preprocessor層でファイル同一性と可視性を確定させ、型検査層は最終的な衝突検出のみを担う」ことである。

### 方針1: exportの可視性を選択importで強制する（H7）

`filter_exports`を「未指定・非exportの宣言は出力しない」方針に変更する。
具体的には、選択import経路では非export行の無条件パススルー（extract.cpp:313-316）をやめ、次の3分類に落とす。

- exportされ、かつ`import_items`に含まれる宣言 → 出力する。
- exportされているが`import_items`に含まれない宣言 → 出力しない（従来通り）。
- exportされていない自由関数・const（型定義を除く） → 出力せず、当該シンボルが`import_items`に明示指定されていた場合は診断を生成する（「'compute' はモジュール 'mod' でexportされていないためimportできません」）。

型定義（struct/enum/interface/typedef）は現状も透過的に残す必要がある（メソッド解決・impl残置のため。extract.cpp:37-61の`kept_types`ロジック）ので、この分類の対象は自由関数・constに限定する。
`extract_exported_blocks`のnamespace外複製（extract.cpp:657-664）は、非exportヘルパーをグローバル公開しないよう「複製したシンボルはユーザーコードから非修飾名で参照できない内部リンケージ扱いにする」設計に寄せる（第3段で扱う。当面はマーカコメントで内部生成であることを明示し、可視性強制の対象外とする）。

診断は選択importで明示指定されたシンボルが非exportだった場合に限る（ワイルドカードは可視シンボルのみを対象とするため診断不要）。

### 方針2: canonical pathを唯一の重複排除キーにする（M7）

選択importと間接importで同一ファイルが二重展開されないよう、ファイル本体（struct/enum/impl/自由関数定義）の出力を`canonical_path`単位で1回に限定する。

- ファイルの「定義本体」を初めて展開したcanonical pathを`emitted_files`（新設、正規化パス集合）に記録する。
- 2回目以降の同一ファイルimport（選択・ワイルドカード・全体を問わず）は、定義本体の再出力をスキップし、可視シンボルの参照可能化（選択importなら`import_items`のnamespace外エイリアス生成）だけを行う。
- 選択importで新規シンボルを要求されたが本体は既出の場合、本体を再展開せず該当exportシンボルだけを可視化する。

これにより`import std::collections::Vector; import std::json;`でも`std::collections`のimpl群は1回だけ出力され、`Duplicate method`は原理的に発生しなくなる。

**重要（採用しない対処）**: 型検査層でのband-aid、すなわち`decl.cpp:698-702`の`throw`を「重複メソッドは黙ってスキップ」に変える対処は採らない。
これは検証済みで、重複implの2つ目をスキップするとメソッドテーブルの整合が崩れ、下流で `not a function` エラーに化けることが確認されている。
重複の真因は「同一ファイルが二重展開されること」であり、正攻法はpreprocessor層でのファイル重複排除（本方針2）である。
型検査の重複検出（decl.cpp:698-702）は、preprocessor修正後に残る真の重複（同名implの多重定義など）を捕捉する最終防壁として維持する。

### 方針3: 同名シンボルの多重importを衝突として診断する（M2）

異なるモジュールから同名の自由関数・型を非修飾で取り込むケースを衝突として扱う。

- preprocessor側: 選択import・全体importで公開する非修飾シンボル名を、そのソースcanonical pathとともに`exposed_symbols`（新設、名前→由来パスの表）に登録する。同名が別パスから登録されようとしたら、衝突として診断を生成する（「シンボル 'compute' は 'a' と 'b' の両方からimportされ曖昧です。エイリアス（`import b as bb`）か修飾名を使ってください」）。
- 型検査側の防壁: `Scope::define_function`が既存名で`false`を返した場合に、呼び出し側で診断を出せるよう戻り値を検査する経路を追加する。ただしこれは同一シンボルの再importなど正当な重複（方針2で排除済み）を誤検出しないよう、preprocessorの`exposed_symbols`判定を一次情報源とする。

診断の一次責任はpreprocessor（由来パスを知っている層）に置き、型検査側は補助防壁とする。

## 構文例・出力例

### H7: 非exportシンボルの選択import（新規に診断）

```cm
// mod.cm
export int add(int a, int b) { return a + b; }
int secret(int x) { return x * 2; }  // 非export

// main.cm
import ./mod::{add, secret};  // secret は非export
```

修正後の期待診断:

```
main.cm:2:20: エラー: 'secret' はモジュール './mod' でexportされていないためimportできません
```

### M7: 選択importと間接importの共存（診断なしで成功）

```cm
import std::collections::Vector;  // 選択import
import std::json;                 // 内部で std::collections を全体import
// 修正前: 「Duplicate method: Vector already has method 'push'」で失敗
// 修正後: std::collections は一度だけ展開され、正常にコンパイルされる
```

### M2: 同名シンボルの多重import（新規に診断）

```cm
import ./a::{compute};
import ./b::{compute};  // a::compute と衝突

// main.cm:2:14: エラー: シンボル 'compute' は 'a' と 'b' の両方からimportされ曖昧です
//   エイリアス（import ./b as b）か修飾名で参照してください
```

## 実装の段階分割

1. 段階1（M7・被害最大）: `process_imports`に`emitted_files`（canonical path集合）を導入し、定義本体の再出力を1回に限定する。選択import経路（expand.cpp:437-515）とワイルドカード経路（expand.cpp:516-541）の両方で参照する。回帰: 既存のmodules系テストが全て緑であること。
2. 段階2（H7）: `filter_exports`の非exportパススルー（extract.cpp:313-316）を分類化し、選択importで明示指定された非exportシンボルに診断を出す。型定義の透過は維持する。
3. 段階3（M2）: preprocessorに`exposed_symbols`表を追加し、別パス由来の同名非修飾シンボルを衝突診断する。`Scope::define_function`の戻り値を検査する補助防壁を型検査側に追加する。
4. 段階4（H7の残り）: `extract_exported_blocks`のnamespace外複製が非exportヘルパーを公開する問題に対し、複製シンボルへ内部生成マーカを付け、ユーザーコードからの非修飾解決対象から外す。

## テスト計画（tests/common/ 配下）

- tests/common/modules/visibility_non_export/ — 非exportシンボルの選択importが診断で拒否されることを検証（H7）。`.expect`はエラーメッセージを期待する負テスト。
- tests/common/modules/export_only_callable/ — exportされた関数のみ呼び出せ、非export関数は未定義参照になることを検証（H7）。
- tests/common/modules/selective_then_indirect/ — `import std::collections::Vector; import std::json;`が`Duplicate method`なくコンパイル・実行できることを検証（M7）。
- tests/common/modules/dedup_symbol_and_whole/ — 選択importと全体importの任意順の組み合わせで二重展開が起きないことを検証（M7）。
- tests/common/modules/ambiguous_same_name_import/ — 別モジュールからの同名importが衝突診断されることを検証（M2）。負テスト。
- 全バックエンド（interpreter/llvm/llvm-wasm/js）で挙動一致を確認する。既存のmodules系テストの非回帰を確認する。

## リスクと非互換性

- **後方非互換（H7）**: 従来は非export関数を選択importで呼べていたコードがコンパイルエラーになる。CLAUDE.mdの「破壊的変更回避」方針に照らし、まず警告として導入し、`--strict`または次メジャーでエラー化する段階移行を検討する。標準ライブラリと`tests/common`が非exportシンボルに依存していないかを事前に全数調査する。
- **重複排除の副作用（M7）**: `emitted_files`によるスキップが、同一パスを別名エイリアス（`import x as a; import x as b;`）で取り込む正当なケースの可視化を壊さないよう、可視化（エイリアス生成）と本体展開を分離して扱う必要がある。
- **診断の誤検出（M2）**: 同一ファイルの再import（方針2で本体は1回）を衝突と誤検出しないよう、衝突判定は「別canonical path由来の同名」に限定する。
- preprocessorは行単位のテキスト処理であり、複数行宣言の分類ミスが新たな抽出漏れを生む恐れがある。`code_portion`・`normalize_export_blocks`（src/internal/preprocessor/import_internal.hpp）の既存正規化を経由させ、リテラル・コメント内の誤検出を避ける。

## 関連

- 監査レポート: `docs/design/v0.17.0/large-scale-bottleneck-audit.md`（H7・M7・M2、および識別子/名前解決の領域別詳細）
- 関連所見C16（`Struct__method`マングリングと自由関数の名前空間衝突）は同じ「単一シンボルテーブルでの衝突検出」という第2段テーマに属し、本設計のM2防壁と共通基盤を持つ。
- 実コード: src/internal/preprocessor/export/extract.cpp, src/internal/preprocessor/import/expand.cpp, src/internal/preprocessor/import/setup.cpp, src/internal/types/scope.cpp, src/internal/types/checking/decl.cpp
