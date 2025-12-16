# Cm言語 開発優先順位とテスト計画

## 開発順序

### 🎯 Phase 0: 最小動作 (1週間)

**目標**: Hello Worldが動く

| 機能 | テストファイル | 状態 |
|------|--------------|------|
| println (string) | p0_hello_world.cm | 🔧 |
| println (int) | p0_print_int.cm | 🔧 |
| main関数 | p0_main_function.cm | ✅ |
| return文 | p0_return.cm | ✅ |

### 🔴 Phase 1: 基本機能 (2週間)

**目標**: 変数と演算が動く

| 機能 | テストファイル | 状態 |
|------|--------------|------|
| int変数宣言 | p1_int_variable.cm | 🔧 |
| 代入 | p1_assignment.cm | 🔧 |
| 算術演算 (+, -, *, /, %) | p1_arithmetic.cm | 🔧 |
| 比較演算 | p1_comparison.cm | 🔧 |
| 論理演算 | p1_logical.cm | 🔧 |
| 型変換 | p1_type_cast.cm | 🔧 |

### 🟠 Phase 2: 制御構造 (2週間)

**目標**: 制御フローが動く

| 機能 | テストファイル | 状態 |
|------|--------------|------|
| if文 | p2_if.cm | 🔧 |
| if-else | p2_if_else.cm | 🔧 |
| else if | p2_else_if.cm | 🔧 |
| while | p2_while.cm | 🔧 |
| for | p2_for.cm | ❌ |
| break/continue | p2_break_continue.cm | ❌ |
| switch | p2_switch.cm | ❌ |

### 🟡 Phase 3: 関数 (3週間)

**目標**: ユーザー定義関数が動く

| 機能 | テストファイル | 状態 |
|------|--------------|------|
| 関数定義 | p3_function.cm | ❌ |
| 引数渡し | p3_parameters.cm | ❌ |
| 戻り値 | p3_return_value.cm | ❌ |
| 再帰 | p3_recursion.cm | ❌ |
| スコープ | p3_scope.cm | ❌ |

### 🟢 Phase 4: データ構造 (1ヶ月)

**目標**: 構造体と配列が動く

| 機能 | テストファイル | 状態 |
|------|--------------|------|
| 配列 | p4_array.cm | ❌ |
| 構造体 | p4_struct.cm | ❌ |
| ポインタ | p4_pointer.cm | ❌ |
| 文字列操作 | p4_string.cm | ❌ |

## テストファイル命名規則

```
p[優先度]_[機能名].cm
p[優先度]_[機能名].expect
```

- p0: 最小動作
- p1: 基本機能
- p2: 制御構造
- p3: 関数
- p4: データ構造
- err: エラーケース

## 実装方針

1. **インタープリタ優先**: まずMIRインタープリタで動作確認
2. **段階的実装**: 各Phaseを完全に動作させてから次へ
3. **テスト駆動**: テストを先に書き、それが通るまで実装
4. **エラー処理**: 各機能にエラーケースも追加

## 成功基準

各Phaseの完了条件：
- すべてのテストがパス
- エラーケースも適切にハンドリング
- ドキュメント更新完了