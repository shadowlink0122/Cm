---
title: Linter
parent: Compiler
nav_order: 1
---

# Linter (cm lint)

Cmには静的解析ツール（Linter）が組み込まれています。コードの品質問題を検出し、改善のヒントを提供します。

## 基本的な使い方

```bash
# 単一ファイルをチェック
./cm lint src/main.cm

# ディレクトリ内を再帰的にチェック
./cm lint src/
```

## チェック項目

### 1. 命名規則

Cmでは以下の命名規則を推奨しています：

| 対象 | 規則 | 例 |
|-----|------|-----|
| 変数・関数 | snake_case | `my_variable`, `calc_sum` |
| 型・構造体 | PascalCase | `MyStruct`, `HttpClient` |
| 定数 | SCREAMING_SNAKE_CASE | `MAX_SIZE`, `PI` |

```cm
// ⚠️ 警告: 変数名はsnake_caseを推奨
int myVariable = 10;  // → my_variable

// ✅ OK
int my_variable = 10;
```

### 2. 未使用変数

使用されていない変数を検出します：

```cm
int main() {
    int x = 10;  // ⚠️ 警告: 変数 'x' が未使用
    return 0;
}
```

### 3. const推奨

変更されない変数に`const`を推奨します：

```cm
int main() {
    int size = 100;  // ⚠️ 警告: 'size'はconstにできます
    println("Size: {size}");
    return 0;
}
```

## 設定ファイル (.cmlint.yml)

プロジェクトルートに`.cmlint.yml`を配置してルールを設定できます：

```yaml
# 命名規則チェックを有効化
naming:
  enabled: true
  variables: snake_case
  types: PascalCase

# 未使用変数チェックを有効化
unused:
  enabled: true
  warn_unused_params: false  # 未使用引数は警告しない

# const推奨を無効化
const_suggestion:
  enabled: false
```

## 出力例

```
warning: variable 'myValue' should use snake_case naming
  --> src/main.cm:5:9
  |
5 |     int myValue = 10;
  |         ^^^^^^^
  |
  = help: consider renaming to 'my_value'

warning: unused variable 'x'
  --> src/main.cm:8:9
  |
8 |     int x = 42;
  |         ^
  |
```

## 関連項目

- [Formatter](formatter.md) - コードフォーマッター

---

**最終更新:** 2026-02-08
