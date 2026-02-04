# ジェネリック型制約構文の整理設計

作成日: 2026-01-11

## 背景

現在のCm言語では以下の構文が混在し、構文的曖昧さが存在：

| 機能 | 現行構文 | 問題点 |
|-----|---------|--------|
| トレイト境界 | `<T: Eq>` | `:` がインターフェース制約 |
| const generics | `<N: const int>` | `:` が型指定に見える |
| インターフェースOR | `<T: I \| J>` | `\|` がユニオン型と類似 |

## 構文的曖昧さの例

```cm
// トレイト境界（TはEqを実装）
<T: Eq> bool eq(T a, T b) { ... }

// const generics（Nはintの定数）
<N: const int> T[N] create() { ... }  // `:` の意味が異なる

// インターフェースOR
<T: Ord | Debug> void f(T x) { ... }  // ユニオン型との混同

// ユニオン型定義
typedef Number = int | double;  // これも `|` を使用
```

## 提案する構文整理

### 案1: キーワードで明確化

```cm
// const genericsは `const` を型に付与（既存構文維持）
<N: const int> T[N] create() { ... }

// トレイト境界は従来通り
<T: Eq> bool eq(T a, T b) { ... }

// デフォルト型パラメータ（新規）
<T = int> struct Box { T value; }  // `=` でデフォルト値

// 型制約（許容する具体型のリスト）
<T in int | double> T add(T a, T b) { ... }  // `in` + ユニオン型
```

### 案2: ユニオン型との統合

```cm
// 型パラメータにユニオン型を使用
typedef Numeric = int | double | float;
<T: Numeric> T add(T a, T b) { ... }  // Tはint, double, floatのいずれか

// 直接記述
<T: (int | double)> T add(T a, T b) { ... }  // 括弧で明示
```

### 案3: 完全分離（推奨）

```cm
// インターフェース境界: `impl` または `where` で意図を明確化
<T impl Eq> bool eq(T a, T b) { ... }  // Tがインターフェース実装

// 型制約: `is` で具体型を指定
<T is int | double> T add(T a, T b) { ... }  // Tは列挙された型のいずれか

// const generics: `const` キーワード必須
<const N: int> T[N] create() { ... }  // N は定数int

// デフォルト型: `=` で指定
<T = int> struct Container { T value; }  // デフォルトはint
```

## 機能別構文まとめ（案3採用時）

| 機能 | 構文 | 意味 |
|-----|------|------|
| インターフェース境界 | `<T impl Eq>` または `<T: Eq>` | TはEqを実装 |
| 複合境界（AND） | `<T impl Eq + Clone>` または `<T: Eq + Clone>` | TはEqとCloneを実装 |
| 型制約（OR） | `<T is int \| double>` | Tはint または double |
| const generics | `<const N: int>` | Nはint型の定数 |
| デフォルト型 | `<T = int>` | 省略時はint |
| 組み合わせ | `<T impl Eq = int>` | EqかつデフォルトはInt |

## ユニオン型との関係

```cm
// ユニオン型定義（既存）
typedef Number = int | double;

// 型制約でユニオン型を使用
<T is Number> T square(T x) { return x * x; }

// または直接記述
<T is int | double | float> T triple(T x) { return x * x * x; }
```

## 実装優先度

| 項目 | 複雑度 | 優先度 | 依存関係 |
|-----|-------|--------|---------|
| `<N: const int>` 構文維持 | 低 | 高 | なし（既存） |
| デフォルト型パラメータ `<T = int>` | 中 | 高 | なし |
| 型制約 `<T is int \| double>` | 中 | 中 | ユニオン型 |
| ユニオン型の完全実装 | 高 | 中 | なし |
| `impl` キーワード導入 | 低 | 低 | 破壊的変更 |

## 現行との互換性

- **トレイト境界** `<T: Eq>` は維持（後方互換）
- **const generics** は `<const N: int>` に変更推奨
- **デフォルト型** は新規追加

## 次のステップ

1. デフォルト型パラメータ `<T = int>` の実装
2. `<const N: int>` 構文への統一
3. 型制約構文 `<T is int | double>` の検討

## 検証

手動でパースエラーが発生しないことと、新構文が意図通り動作することを確認。
