# v0.13.0 構文拡張 - 破壊的変更分析

## 分析日: 2026-01-30

---

## 1. 関数ポインタ型構文

### 現在の構文（維持）
```cm
int*(int, int) op = &add;     // 戻り値*型(引数...)
void*(int) printer = &print;
bool*(int) is_even = (int x) => { return x % 2 == 0; };
```

### 決定
**現在の構文を維持** - 破壊的変更なし

### 理由
- 既存の15件のテストファイルを変更する必要がない
- `*` が関数ポインタを明示
- クロージャ構文 `(params) => { }` との整合性

---

## 2. クロージャ構文

### 現在の構文
```cm
(int x) => { return x * 2; }
(int a, int b) => { return a + b; }
```

### 提案する構文
```cm
|int x| { return x * 2; }
|int a, int b| { return a + b; }
```

### 潜在的な衝突

**`||` 演算子との衝突**:
- 現在 `||` は論理OR
- `|| { }` をクロージャとして使う場合、パーサーで区別必要

**解決策**:
- `||` の後に `{` が来たらクロージャと判定
- それ以外は論理OR

### 対応方針

**推奨**: 現在の構文 `(params) => { }` を維持
- 既存コードと互換
- `=>` がクロージャを明確に示す
- 後で `|params|` も追加可能

---

## 3. Result型とenum拡張

### 現在の状態
```cm
// struct + enum tag で擬似的に実装
enum ResultTag { Ok = 0, Err = 1 }
struct IntResult {
    ResultTag tag;
    int value;
    string error;
}
```

### 提案する構文
```cm
enum Result<T, E> {
    Ok(T),
    Err(E)
}
```

### 影響を受けるファイル

| ファイル | 現在の実装 |
|---------|-----------|
| `match/match_result.cm` | struct Result<T, E> + is_ok フィールド |
| `match/match_option.cm` | struct Option<T> + has_value フィールド |
| `types/result_pattern.cm` | struct IntResult + ResultTag enum |

### 対応方針

**非破壊的**: 新しいenum構文を追加
- 既存のstruct実装は動作し続ける
- 新しい `enum Result<T, E> { Ok(T), Err(E) }` を追加
- 既存テストはそのまま、新テストを追加

---

## 4. 型付きマクロ

### 現在の構文
```cm
macro VERSION = 13;
macro MSG = "hello";
```

### 提案する構文
```cm
macro int VERSION = 13;
macro string MSG = "hello";
```

### 影響を受けるファイル

| ファイル | 現在のコード |
|---------|-------------|
| `macro/macro_basic.cm` | `macro VERSION = 13;` |

### 対応方針

**後方互換**: 型なしマクロを void として扱う
- `macro NAME = value;` → void型
- `macro int NAME = value;` → int型

---

## 5. 必要なテスト追加

### 新規テストファイル

1. **`macro/typed_macro.cm`** - 型付きマクロのテスト
2. **`enum/enum_associated_data.cm`** - データ付きenum
3. **`result/result_enum.cm`** - enum版Result型
4. **`closure/closure_pipe_syntax.cm`** - パイプ構文クロージャ（オプション）

---

## 6. 移行計画

### フェーズ 1: 非破壊的変更
1. 型付きマクロ（後方互換）
2. enum拡張（新機能）
3. Result型（新機能）

### フェーズ 2: 破壊的変更
1. 関数ポインタ構文変更
2. テストファイル更新（15件）
3. ドキュメント更新

### フェーズ 3: オプション機能
1. パイプ構文クロージャ `|x| { }`
2. 型推論強化
