---
title: 構文網羅検証で検出したバグの修正（索引）
parent: v0.17.0 Design
---

# 構文網羅検証で検出したバグの修正（索引）

構文→LLVM IR対訳リファレンス（docs/architecture/codegen/）の執筆時に、全構文をビルド済みcmで実機コンパイル・実行して検証した結果、既知の監査所見に含まれない誤コンパイル・クラッシュ・型検査の誤りを検出した。
バグごとの詳細・再現コード・修正方針・テスト計画は個別文書に分割し、修正完了したものから順にdocs/archive/v0.17.0/へ移動する。
対象はnative/jit（LLVM経路）で、再現はv0.17.0のリポジトリビルドcmによる。

## バグ一覧と個別文書

| # | 個別文書 | 領域 | 概要 | 重大度 | 状態 |
|---|---------|------|------|--------|------|
| B1 | [const-global-aggregate-init.md](../memory/const-global-aggregate-init.md) | グローバル | const集約グローバルがrodata定数へのstoreになりO0でSIGBUS・最適化時に誤値 | Critical | 修正済み |
| B2 | [int-literal-to-float-conversion.md](../numeric/int-literal-to-float-conversion.md) | 数値変換 | 整数値→浮動小数文脈がビット再解釈になり誤値（native/jit共通） | Critical | 修正済み |
| B3 | [must-block-field-assignment.md](../syntax-parse/must-block-field-assignment.md) | must文 | must{}内の構造体フィールド代入が別一時へ書かれ読み出しが0になる | Critical | 修正済み |
| B4 | [nested-member-slice-chain.md](../arrays-slices/nested-member-slice-chain.md) | スライス | ネストメンバスライスのチェーン変異がnativeでSIGSEGV | Critical | 修正済み |
| B5 | [cast-null-pointer-comparison.md](../numeric/cast-null-pointer-comparison.md) | 比較 | キャスト付きnull比較が文字列比較（cm_strcmp）に落ちる | High | 修正済み |
| B6 | [interface-bound-method-return-type.md](../interfaces-derive/interface-bound-method-return-type.md) | ジェネリクス | 境界経由の非voidメソッド呼び出しが型検査で誤拒否 | High | 修正済み |
| B7 | [interface-method-interpolation-type.md](../interfaces-derive/interface-method-interpolation-type.md) | 補間 | インターフェイスメソッド戻り値の直接補間で型取り違え | High | 修正済み |
| B8 | [typedef-struct-literal-resolution.md](../type-system/typedef-struct-literal-resolution.md) | typedef | 構造体typedef別名のリテラル使用がUnknown struct type | Medium | 修正済み |
| B9 | [defer-implicit-function-end.md](../syntax-parse/defer-implicit-function-end.md) | defer | 暗黙の関数終端でdeferが発火しない | Medium | 修正済み |

## 修正の進め方

影響領域が重ならない単位で並行修正する。

- 第1波: B1（グローバル発行）・B2（数値変換）・B4（スライス参照経路）・B6/B7（型検査の戻り値型解決）
- 第2波: B3（must lowering）・B5（比較lowering）・B8/B9（typedef解決・defer終端）

各修正は再現テストを先に固定し、修正後にnative/jit両方で回帰スイートを通す。
全修正の統合後にmake test-unit・make test-regression・native/jitスイートを完走させ、完了した個別文書をarchiveへ移動してリリースノートへ記録する。
