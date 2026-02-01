---
description: リリース準備の手順
---

# リリースワークフロー

## 1. コンパイラ検証
// turbo-all
1. `rm -rf build && make build` - クリーンビルド
2. `make tip` - インタプリタテスト全パス
3. `make tlp` - LLVMテスト全パス
4. `make tjp` - JSテスト確認（該当する場合）

## 2. ドキュメント整備

### 2.1 完了ドキュメントのアーカイブ
実装済みのドキュメントを`docs/archive/`に移動：
```bash
mv docs/design/implemented_feature.md docs/archive/
```

### 2.2 リリースノート作成
`docs/PR.md`にリリースノートを記載：
- 新機能の説明
- 破壊的変更の有無
- マイグレーション手順
- 既知の問題

### 2.3 README.md更新
- 機能一覧の更新
- インストール手順の確認
- 使用例の追加

### 2.4 tutorials/更新
機能変更・追加に対応するチュートリアルを更新または作成

## 3. バージョン更新
// turbo
1. `VERSION`ファイルを更新
2. `CMakeLists.txt`のバージョン番号確認
3. `package.json`（該当する場合）

## 4. lint/format確認
// turbo
1. `cm lint`で警告がないこと
2. `cm fmt`でフォーマット統一
3. コード品質チェック

## 5. 最終確認
// turbo
1. `git status` - 未コミット変更なし
2. `git log --oneline -10` - コミット履歴確認
3. 一貫性確認チェック：
   - [ ] コンパイラ機能
   - [ ] テスト
   - [ ] lint/format
   - [ ] tutorials
   - [ ] README
   - [ ] VERSION

## 6. リリース実行
```bash
git tag v<version>
git push origin v<version>
```
GitHub Releaseを作成
