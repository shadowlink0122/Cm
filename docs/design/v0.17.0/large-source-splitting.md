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

## 検出経緯

ユーザーからのリファクタリング提案募集（ファイルや機能ごとに実装の分割）を受け、行数実測と過去の複雑度調査の関数規模データを統合して起案した。
