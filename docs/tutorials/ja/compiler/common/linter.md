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

# 命名規則チェック（L001）を有効にしてチェック
./cm lint --strict src/main.cm
./cm check --strict src/main.cm
```

## チェック項目

### 1. 命名規則（L001。--strict時のみ）

`cm check --strict` / `cm lint --strict` は、全ての宣言の名前が世界標準（C/C++/Rust）の命名規則に従っているかを検査します：

| 宣言 | 許容ケース | 例 |
|-----|------|-----|
| struct / enum / interface / typedef型名 | PascalCase | `AdderIo`, `HttpClient` |
| ジェネリック型パラメータ | PascalCase | `T`, `TKey` |
| 関数・メソッド名 | snake_case | `calc_sum` |
| 変数・パラメータ・フィールド | snake_case | `total_count` |
| グローバル定数（const） | UPPER_SNAKE_CASE | `MAX_SIZE`, `CLK_FREQ` |
| ローカル定数（const） | snake_case / UPPER_SNAKE_CASE | `base`, `MAX_N` |
| enumバリアント | PascalCase / UPPER_SNAKE_CASE | `North`, `CTRL_00` |
| モジュール名 | snake_case | `hdmi_out` |

camelCase はどの宣言でも許容されません。先頭のアンダースコアは判定前に除去されます（`_unused` は snake_case として扱われます）。

```cm
// ⚠️ L001: 構造体名 'bad_struct' は PascalCase 命名規則に従っていません
struct bad_struct {
    int badField;  // ⚠️ L001: フィールド名は snake_case
}

// ✅ OK
struct GoodStruct {
    int good_field;
}
```

チェック対象外（外部で名前が固定されるもの等）：

- `extern struct` とそのフィールド（ベンダプリミティブ: `OSC`, `TLVDS_D2` 等）
- `extern "C"` ブロック内の関数（Cシンボル名）
- `self()` コンストラクタ / `~self()` デストラクタ / 演算子オーバーロード / `main`
- importでインライン展開される標準ライブラリ名前空間（`std` / `native` 等）

### 2. 未使用変数（W001）

使用されていない変数を検出します：

```cm
int main() {
    int x = 10;  // ⚠️ W001: 変数 'x' が未使用
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

## 設定ファイル (.cmconfig.yml)

プロジェクトルート（またはその親ディレクトリ）に `.cmconfig.yml` を配置してルールごとのレベルを設定できます：

```yaml
lint:
  rules:
    L001: disabled   # 命名規則チェックを無効化
    W001: error      # 未使用変数をエラーに昇格
```

行単位で無効化するコメントも使用できます：

```cm
// @cm-disable-next-line L001
struct legacy_struct {  // この行のL001は報告されない
    int id;
}
```

## 出力例

```
src/main.cm:5:9: warning: 変数名 'myValue' は snake_case 命名規則に従っていません [L001]
src/main.cm:8:9: warning: Variable 'x' is never used [W001]
```

## 関連項目

- [Formatter](formatter.md) - コードフォーマッター

---

<!-- nav -->
← 前: [コンパイラ編 - プリプロセッサ（条件付きコンパイル）](preprocessor.html) ｜ [目次](../index.html) ｜ 次: [Formatter (cm fmt)](formatter.html) →
