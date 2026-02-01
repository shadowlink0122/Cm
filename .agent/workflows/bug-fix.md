---
description: バグ修正の手順
---

# バグ修正ワークフロー

## 1. 問題の特定
1. バグの再現手順を確認
2. 最小再現コードを作成
// turbo
3. `./cm compile --debug <file>` でデバッグ出力確認

## 2. 原因調査
// turbo
1. `--mir-opt` でMIR出力確認
2. `--lir-opt` でLLVM IR出力確認
3. 関連コードの確認

## 3. 修正実装
1. 修正コード作成
2. 最小再現コードで動作確認
// turbo
3. `make build` でビルド

## 4. 回帰テスト
// turbo-all
1. `make tip` - 全インタプリタテスト
2. `make tlp` - 全LLVMテスト
3. 回帰がないことを確認

## 5. コミット
1. 修正内容をコミット
2. 関連Issueがあればリンク
