# アーカイブドキュメント

過去の設計文書や参考資料のアーカイブです。これらのドキュメントは古いか、現在のプロジェクト状態と一致しない可能性があります。

## ⚠️ 注意

このディレクトリのドキュメントは**参考資料**です。最新の情報は以下を参照してください：

- **正式言語仕様**: [../design/CANONICAL_SPEC.md](../design/CANONICAL_SPEC.md)
- **現在の実装状況**: [../implementation/implementation_status.md](../implementation/implementation_status.md)
- **プロジェクト構造**: [../PROJECT_STRUCTURE.md](../PROJECT_STRUCTURE.md)

## 📚 アーカイブ一覧

### クリーンアップ関連

| ドキュメント | 説明 |
|------------|------|
| [CLEANUP_SUMMARY.md](CLEANUP_SUMMARY.md) | 過去のクリーンアップサマリー |

### 構造体実装（v0.4.0以前）

| ドキュメント | 説明 |
|------------|------|
| [STRUCT_EXECUTIVE_SUMMARY.txt](STRUCT_EXECUTIVE_SUMMARY.txt) | 構造体実装のエグゼクティブサマリー |
| [STRUCT_IMPLEMENTATION_STATUS.md](STRUCT_IMPLEMENTATION_STATUS.md) | 構造体実装状況（旧） |
| [STRUCT_INVESTIGATION_INDEX.md](STRUCT_INVESTIGATION_INDEX.md) | 構造体調査インデックス |
| [STRUCT_QUICK_REFERENCE.md](STRUCT_QUICK_REFERENCE.md) | 構造体クイックリファレンス |

### 文字列補間（v0.5.0以前）

| ドキュメント | 説明 |
|------------|------|
| [STRING_INTERPOLATION_ARCHITECTURE.md](STRING_INTERPOLATION_ARCHITECTURE.md) | 文字列補間アーキテクチャ |
| [STRING_INTERPOLATION_EXAMPLES.md](STRING_INTERPOLATION_EXAMPLES.md) | 文字列補間の例 |
| [STRING_INTERPOLATION_LLVM.md](STRING_INTERPOLATION_LLVM.md) | LLVM文字列補間実装 |
| [STRING_INTERPOLATION_README.md](STRING_INTERPOLATION_README.md) | 文字列補間README |

### バックエンド比較

| ドキュメント | 説明 |
|------------|------|
| [backend_comparison.md](backend_comparison.md) | 各バックエンドの比較（旧） |
| [wasm_execution.md](wasm_execution.md) | WASM実行に関する文書（旧） |

## 📅 アーカイブされた理由

これらのドキュメントは以下の理由でアーカイブされました：

1. **実装完了**: 機能が既に実装され、正式仕様に統合された
2. **設計変更**: アーキテクチャの変更により無効になった
3. **統合**: 他のドキュメントに統合された
4. **廃止**: 機能が廃止された（例: Rustトランスパイラ）

## 🔍 過去のドキュメントを読む際の注意

1. **日付を確認**: ドキュメントの作成日・更新日を確認
2. **現行版と比較**: 正式仕様と照らし合わせる
3. **実装状況確認**: 記載内容が実装されているか確認
4. **質問する**: 不明な点はイシューで質問

## 🗂️ アーカイブポリシー

以下の基準でドキュメントをアーカイブします：

- 6ヶ月以上更新されていない
- 現在の設計と大きく乖離している
- 他のドキュメントに統合済み
- 廃止された機能に関する文書

## 📝 歴史的価値

アーカイブされたドキュメントは、プロジェクトの進化を理解するために有用です：

- **設計の変遷**: なぜ現在の設計になったか
- **試行錯誤**: 過去に試みられたアプローチ
- **学び**: 何がうまくいき、何がうまくいかなかったか

---

**アーカイブ開始日:** 2025-12-15
