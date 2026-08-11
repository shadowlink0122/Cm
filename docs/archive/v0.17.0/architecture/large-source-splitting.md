# 巨大ソースファイル・巨大関数の機能分割（リファクタリング提案）

## 概要

コンパイラ本体には1,500行超のソースファイルと700〜1,100行の単一関数が残っており、変更時の影響範囲が読みにくく、並行開発でのコンフリクト単位も大きい。
リポジトリ規約「ファイル名にアンダースコアで階層を埋め込まない（ディレクトリ構成にする）」に沿って、責務ごとのディレクトリ分割と関数の段階抽出を提案する。

## 実測（2026-08-11時点）

ファイル規模の上位（src/internal・src/cmd、messages.cppはi18n表のため対象外）:

| ファイル | 行数 | 分割候補 |
|---|---|---|
| module/graph.cpp | 1,748 | 走査/解決/出力の3責務が同居（モジュールグラフAST化の設計文書と重複するため、そちらの完遂を優先） |
| codegen/sv/analyze.cpp | 1,621 | analyzeMIR本体（約870行）+レジスタ推論+FSM検出 |
| hir/lowering/expr.cpp | 1,618 | 式種別ごとのlower（call/literal/binary等が単一ファイル） |
| codegen/llvm/native/codegen.cpp | 1,604 | 命令選択/ランタイム宣言/最適化パイプライン起動 |
| codegen/sv/codegen.cpp | 1,579 | モジュール出力/ポート宣言/always生成 |
| types/checking/decl.cpp | 1,547 | struct/enum/typedef/interface宣言の検査が同居 |
| fmt/formatter.cpp | 1,302 | 宣言/文/式/コメント整形が同居 |

単一関数の規模上位（過去調査の実測値）:

| 関数 | 行数 | 所在 |
|---|---|---|
| lower_member | 約1,100 | hir/lowering/expr_member.cpp |
| lower_binary | 約970 | mir/lowering/expr/binary.cpp |
| analyzeMIR | 約870 | codegen/sv/analyze.cpp |
| convertAssignStatement | 約790 | codegen/sv系 |
| convertFunction | 約700 | codegen/sv系 |

## リファクタリング方針

- ファイル分割は「1ファイル=1責務」でディレクトリ化する（例: `types/checking/decl.cpp` → `types/checking/decl/{struct,enum,typedef,interface}.cpp`）。
- 関数分割は巨大switch/if連鎖の腕を種別ごとの静的関数へ抽出する（lower_memberならレシーバ種別、lower_binaryなら演算子カテゴリ）。
- 分割は挙動同一の純リファクタリングとし、1ファイル（または1関数）ずつ独立コミットで進める（並行セッションのgit add -A巻き込みを避ける）。
- 抽出時に共有が必要になった内部stateは、無名名前空間の共有からコンテキスト構造体の明示引き渡しへ置き換える。

## 段階分割

1. SVバックエンド（analyze.cpp・codegen.cpp・convert系関数）: 単一バックエンドで閉じており影響半径が最小。
2. 型検査decl.cppとフォーマッタformatter.cpp: 宣言種別ごとの独立性が高い。
3. HIR/MIRのlower系巨大関数（lower_member・lower_binary）: 全バックエンドの共通経路のため最後に実施し、regressionを厚くしてから着手する。
4. graph.cppはモジュールグラフAST化の設計文書（第2段=出力AST化）の完遂に分割を織り込む（独立の分割作業はしない）。

## リスク

- lower_member/lower_binaryは暗黙のフォールスルー（前段の腕が設定した変数を後段が参照する等）を含む可能性があり、機械的な抽出で挙動が変わりうる（抽出前に腕ごとの入出力をコメントで棚卸しする）。
- 分割によるビルド時間への影響は軽微だが、ヘッダ公開範囲が広がらないよう抽出関数は原則同ディレクトリ内部に留める。

## テスト計画

- 各段階の完了ごとに全13スイート（interpreter/llvm各O0-O3/js/ts/sv/unit/regression）で挙動同一を確認する。
- 巨大関数の分割前に、当該関数の主要分岐を通すregressionケースの網羅を確認し、不足分岐は先にテストを追加する。

## 実装記録（2026-08-11）

段階1〜3を実装した（各段階を独立コミットで実施）。

- **段階1（SVバックエンド）**: analyze.cppをオーケストレータ42行+analyze/配下5ファイル（globals/ports/clock/declarations/function）へ、codegen.cppをエントリ160行+codegen/配下5ファイル（types/emitter/module/expr/stmt）へ分割した。フェーズ間の共有ローカル状態はメンバ昇格せず引数で明示した。
- **段階2（checker/フォーマッタ）**: decl.cppを削除しdecl/配下7ファイル（driver/namespace/dispatch/attributes/impl/typedecl/function）へ、formatter.cppをエントリ97行+formatter/配下5ファイル（whitespace/braces/indent/spacing/wrap）へ分割した。旧新の正規化diff・関数本体のbyte一致・make formatのCmソース差分ゼロで挙動同一を機械的に確認した。
- **段階3（共通lower系の巨大関数）**: lower_member（約1100行→本体115行）とlower_binary（約970行→本体84行）を、腕ごとの静的ヘルパー抽出でディスパッチが見通せる形へ縮めた。全13スイート相当（unit・regression・interpreter/llvm/js/sv）で挙動同一を確認。
- **残件**: graph.cpp（1,748行）はモジュールグラフAST化の設計文書側で扱う方針のまま未着手。hir/lowering/expr.cpp（1,618行）・llvm/native/codegen.cpp（1,604行）・SVのconvert系関数（emit_control.cpp内）は未分割の継続候補。

## 実装記録（残件処置と配置整理・2026-08-12）

前記残件のうちgraph.cpp以外を全て処置し、あわせて規約「ファイル名にアンダースコアで階層を埋め込まない」の全体適用を実施した。

- **hir/lowering/expr.cpp（1,618行）**: expr.cppを削除しexpr/配下6ファイル（dispatch/operators/call/access/literal+移設のmatch/member/internal.hpp）へ関数移動で分割した。
- **llvm/native/codegen.cpp（1,604行）**: codegen.cppを削除しcodegen/配下4ファイル（driver/optimize/emit/link）へドライバ工程別に分割した。
- **SVのemitTerminator（約820行）**: 本体を32行の純ディスパッチへ縮め、SwitchInt系・Call系の腕別ヘルパーへ抽出した（3箇所重複のノンブロッキング代入判定も1本化）。computeForLoops（約280行）も3段へ分割した。
- **ファイル配置の整理**: mir/loweringのexpr_*→expr/・mono系→mono/・auto_impl系→auto_impl/の14ファイル、hir/loweringのexpr_*→expr/、SVのemit_control→codegen/control.cpp・sv_internal.hpp→internal.hpp、parser_decl/stmt/type→parser/直下名・mir_splitter→splitter・optimization_pipeline→pipelineの改名を実施し、リポジトリ全体の#includeとCMakeLists.txtを追従した。
- **検証**: 各コミット単位でunit・regression+関連バックエンドスイート、最終状態で全4スイート（interpreter/llvm/js/sv）。
- **最終残件**: graph.cpp（モジュールグラフAST化側で処置予定）のみ。本設計文書の提案範囲は完遂。

## 検出経緯

ユーザーからのリファクタリング提案募集（ファイルや機能ごとに実装の分割）を受け、行数実測と過去の複雑度調査の関数規模データを統合して起案した。
