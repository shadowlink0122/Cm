# モジュールシステム実装ログ

## セッション: 2024-12-20

### 目標
Phase 1の実装を開始し、基本的な再エクスポート機能を動作させる

### 実装順序
1. 式中の`::`演算子のサポート（最優先）
2. 相対パスのサポート
3. `export { M };` の処理
4. プリプロセッサでの再エクスポート

---

## ステップ1: 式中の`::`演算子のサポート

### 問題
```cm
std::io::println("Hello");  // パースエラー
```

### 調査項目
- [ ] パーサーが`::`をどう扱っているか
- [ ] 式の中での名前解決
- [ ] 関数呼び出しのパース

### 実装開始

## 実装完了

### ✅ ステップ1-A: 複数レベルの`::`演算子サポート

**修正ファイル**: `src/frontend/parser/parser_expr.cpp`

**変更内容**:
```cpp
// 修正前: 1レベルのみ
if (consume_if(TokenKind::ColonColon)) {
    std::string member = expect_ident();
    // ...
}

// 修正後: 複数レベル対応
if (consume_if(TokenKind::ColonColon)) {
    std::string qualified_name = name;
    do {
        std::string member = expect_ident();
        qualified_name += "::" + member;
    } while (consume_if(TokenKind::ColonColon));
    // ...
}
```

**テスト結果**:
- ✅ `Color::Red` - 動作
- ✅ パースエラーなし

**影響範囲**:
- 式の中での名前解決
- matchパターン

---

### ✅ ステップ1-B: 相対パスのサポート

**修正ファイル**: `src/frontend/parser/parser_module.cpp`

**変更内容**:
- `./` プレフィックスの認識
- `../` プレフィックスの認識
- `/` 区切りの深い階層パス（`./io/file`）

**構文**:
```cm
import ./sibling;     // ✅ パース成功
import ../parent;     // ✅ パース成功
import ./sub/module;  // ✅ パース成功
```

**テスト結果**:
- ✅ パースは成功
- ❌ プリプロセッサが未対応のため実行時エラー

---

## 次のステップ

### 🔄 ステップ2: プリプロセッサの改修

**現状の問題**:
- 既存のプリプロセッサは単純なインライン展開のみ
- 相対パスの解決が未実装
- 再エクスポートの追跡が未実装
- `namespace` の生成が未実装

**必要な作業**:

#### 2.1 モジュール解決の強化
**ファイル**: `src/module/module_resolver.cpp`

**タスク**:
- [ ] 相対パスの解決（`./`, `../`）
- [ ] ディレクトリ探索（`io/io.cm` or `io/mod.cm`）
- [ ] 深い階層のパス処理（`./io/file`）

#### 2.2 プリプロセッサの再設計
**ファイル**: `src/preprocessor/import_preprocessor.cpp`

**タスク**:
- [ ] モジュール情報の管理構造
- [ ] `export { M };` の検出と追跡
- [ ] 階層的インポートの解決（`import std::io;`）
- [ ] `namespace` の自動生成

**設計方針**:
```cpp
struct ModuleInfo {
    std::string name;
    std::filesystem::path file_path;
    std::string source_code;
    std::vector<std::string> exports;  // 再エクスポートリスト
};

class ImportPreprocessor {
    std::unordered_map<std::string, ModuleInfo> loaded_modules;
    
    // import std::io; を処理
    std::string process_hierarchical_import(const std::string& path);
    
    // namespace 生成
    std::string generate_namespace(const std::vector<std::string>& segments, 
                                    const std::string& code);
};
```

---

## 実装の優先順位（更新）

### 優先度1: モジュール解決器の実装
**推定時間**: 1日

相対パスを正しく解決し、ファイルを見つけることが最優先です。

### 優先度2: 基本的な再エクスポート処理
**推定時間**: 2日

`export { M };` を検出し、シンプルなケースで動作させます。

### 優先度3: namespace生成
**推定時間**: 1日

階層的なnamespaceを自動生成します。

### 優先度4: 統合テスト
**推定時間**: 1日

全体が連携して動作することを確認します。

---

## 現在の進捗

| タスク | 状態 | 完了日 |
|--------|------|--------|
| 複数レベル`::`演算子 | ✅ 完了 | 2024-12-20 |
| 相対パスのパース | ✅ 完了 | 2024-12-20 |
| モジュール解決器 | ⬜ 未着手 | - |
| プリプロセッサ再設計 | ⬜ 未着手 | - |
| namespace生成 | ⬜ 未着手 | - |
| 統合テスト | ⬜ 未着手 | - |

**全体進捗**: 約15%

---

## 推奨される実装アプローチ

### アプローチA: 段階的実装（推奨）
1. モジュール解決器を完成させる
2. シンプルなケース（`import ./helper;`）で動作確認
3. 再エクスポート機能を追加
4. 階層的インポート（`import std::io;`）を実装

**メリット**: 
- 各ステップでテスト可能
- 問題の早期発見
- リスク低減

### アプローチB: 一括実装
すべての機能を一度に実装する

**デメリット**:
- デバッグが困難
- 既存機能を壊すリスク
- テストが遅れる

**推奨**: アプローチAを採用

---

## 次回セッションの計画

1. **モジュール解決器の実装** (2-3時間)
   - 相対パス解決ロジック
   - ディレクトリ探索
   - テストケース

2. **プリプロセッサの基本部分** (2-3時間)
   - モジュール情報の管理
   - シンプルなインライン展開の修正

3. **動作確認** (1時間)
   - `import ./helper;` が動作するか
   - 基本的なテストの実行

**合計推定時間**: 5-7時間（1日）

