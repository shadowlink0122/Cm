# MIRローダリング再設計仕様書

## 1. 現状の問題点

### 1.1 ハードコーディングされた処理
現在のMIRローダリング実装は、特定のパターン（printlnなど）のみをハードコードで処理しており、汎用的な文の処理ができません。

### 1.2 不完全なブロック処理
- if/elseブロック内でprintln以外の文が処理できない
- ネストされた制御構造が動作しない
- else ifがサポートされていない

### 1.3 未実装の機能
- 文字列リテラル
- break/continue文
- switch文の完全なサポート
- defer文の実行

## 2. 新設計の方針

### 2.1 再帰的な文処理
すべての文タイプを統一的に処理できる再帰的なローダリング関数を実装します。

```cpp
// 汎用的な文ローダリング関数
void lower_statement(
    const hir::HirStmt* stmt,
    MirFunction* mir_func,
    BasicBlock*& current_block,
    std::unordered_map<std::string, LocalId>& variables,
    std::vector<hir::HirStmt*>& defer_statements
);
```

### 2.2 ブロック管理の改善
- ブロックの作成と接続を統一的に管理
- 各文タイプごとに適切なCFG（制御フローグラフ）を構築

### 2.3 文タイプ別の処理

#### 2.3.1 変数宣言（HirLet）
```cpp
case HirLet:
    // ローカル変数の登録
    // 初期化式の評価
    // 必要に応じて関数呼び出しの処理
```

#### 2.3.2 代入文（HirAssign）
```cpp
case HirAssign:
    // 左辺値の解決
    // 右辺値の評価
    // 代入操作の生成
```

#### 2.3.3 式文（HirExprStmt）
```cpp
case HirExprStmt:
    // 式の評価（副作用のため）
    // 関数呼び出しの処理
```

#### 2.3.4 if文（HirIf）
```cpp
case HirIf:
    // 条件式の評価
    // thenブロックの処理（再帰的）
    // elseブロックの処理（再帰的、else ifを含む）
    // 終了ブロックへの接続
```

#### 2.3.5 whileループ（HirWhile）
```cpp
case HirWhile:
    // ループヘッダブロック
    // 条件評価
    // ループ本体の処理（再帰的）
    // ループ終了ブロック
```

#### 2.3.6 forループ（HirFor）
```cpp
case HirFor:
    // 初期化部の処理
    // ループ条件の評価
    // ループ本体の処理（再帰的）
    // 更新部の処理
```

#### 2.3.7 switch文（HirSwitch）
```cpp
case HirSwitch:
    // 判定式の評価
    // 各caseブロックの処理（再帰的）
    // defaultブロックの処理
```

#### 2.3.8 return文（HirReturn）
```cpp
case HirReturn:
    // 戻り値の評価
    // defer文の実行
    // return terminatorの設定
```

#### 2.3.9 defer文（HirDefer）
```cpp
case HirDefer:
    // defer文リストへの追加
    // 関数終了時またはreturn時に実行
```

#### 2.3.10 break/continue文
```cpp
case HirBreak:
    // ループ終了ブロックへのジャンプ
case HirContinue:
    // ループ更新部へのジャンプ
```

## 3. 実装手順

### Phase 1: 基盤整備
1. `lower_statement`関数の実装
2. ブロック管理ユーティリティの作成
3. 既存コードのリファクタリング

### Phase 2: 基本文の実装
1. 変数宣言と代入文
2. 式文（関数呼び出しを含む）
3. return文

### Phase 3: 制御構造の実装
1. if/else文の完全サポート
2. while/forループの改善
3. switch文の実装

### Phase 4: 高度な機能
1. break/continue文
2. defer文
3. 文字列リテラルサポート

## 4. ブロック管理戦略

### 4.1 ブロックビルダーパターン
```cpp
class BlockBuilder {
    MirFunction* func;
    BasicBlock* current_block;
    std::vector<BasicBlockPtr> pending_blocks;

public:
    // 新しいブロックを作成し、現在のブロックから接続
    BasicBlock* create_and_switch(BlockId id);

    // 現在のブロックを終了し、次のブロックへ切り替え
    void finalize_and_switch(BasicBlock* next);

    // すべての保留ブロックをMirFunctionに追加
    void commit_blocks();
};
```

### 4.2 ループコンテキスト管理
```cpp
struct LoopContext {
    BlockId continue_block;  // continueのジャンプ先
    BlockId break_block;     // breakのジャンプ先
};

// ネストされたループを管理
std::stack<LoopContext> loop_stack;
```

## 5. テスト戦略

### 5.1 単体テスト
各文タイプごとに独立したテストケースを作成：
- 基本的な動作確認
- エッジケース
- エラーハンドリング

### 5.2 統合テスト
実際のプログラムパターンでのテスト：
- ネストされた制御構造
- 複雑な式の評価
- 実用的なプログラム例

### 5.3 回帰テスト
既存のテストスイートの継続的な実行：
- 機能の退行を防ぐ
- パフォーマンスの監視

## 6. 期待される成果

### 6.1 機能改善
- すべての基本的な文タイプのサポート
- ネストされた制御構造の完全な動作
- else ifの正しい処理

### 6.2 保守性向上
- コードの重複削減
- 拡張性の向上
- デバッグの容易化

### 6.3 テスト合格率
- 目標: 80/80テスト合格（現在: 39/80）
- 段階的な改善により、各フェーズで合格率を向上

## 7. リスクと対策

### 7.1 互換性の維持
- 既存の動作するコードを壊さないよう、段階的に移行
- 十分なテストカバレッジの確保

### 7.2 パフォーマンス
- 再帰的処理による性能への影響を監視
- 必要に応じて最適化

### 7.3 複雑性の管理
- 明確な責任分離
- 適切なドキュメント化
- コードレビューの実施