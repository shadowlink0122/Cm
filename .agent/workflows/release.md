---
description: リリース準備の手順
---

# リリースワークフロー

## 1. リリース前チェック
// turbo-all
1. `make build` - クリーンビルド確認
2. `make tip` - インタプリタテスト全パス
3. `make tlp` - LLVMテスト全パス
4. `make tjp` - JSテスト確認（該当する場合）

## 2. ドキュメント更新
1. ROADMAP.md - 完了項目にチェック
2. CHANGELOG.md - 変更履歴追加
3. docs/PR.md - リリースノート作成

## 3. バージョン更新
1. バージョン番号更新
   - CMakeLists.txt
   - package.json（該当する場合）
2. タグ作成準備

## 4. 最終確認
// turbo
1. `git status` - 未コミット変更なし
2. `git log --oneline -10` - コミット履歴確認
3. CI全パス確認

## 5. リリース実行
1. `git tag v<version>`
2. `git push origin v<version>`
3. GitHub Release作成
