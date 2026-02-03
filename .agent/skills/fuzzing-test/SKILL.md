---
name: fuzzing-test
description: ファジングテストと網羅的構文確認
---

# ファジングテストスキル

新機能追加時に、すべての構文パターンで正しく動作することを確認するためのスキルです。

## 目的

1. **BNF準拠の確認**: 実装がBNFで定義された構文を満たしているか
2. **網羅的テスト**: 基本型だけでなく、typedef / struct / enum などでも動作するか
3. **組み合わせテスト**: 複数の機能を組み合わせた場合に問題ないか

## BNF更新チェックリスト

新機能追加時にBNFを更新：

- [ ] `docs/design/cm_grammar.bnf` - EBNF形式の詳細文法
- [ ] `docs/spec/grammar.md` - Markdown形式の文法ドキュメント
- [ ] バージョン番号の更新（ファイル先頭のコメント）

## 構文網羅テストマトリクス

新機能が以下のすべてで動作することを確認：

### 型カテゴリ

| カテゴリ | 例 | チェック |
|---------|-----|---------|
| プリミティブ型 | `int`, `double`, `bool` | [ ] |
| typedef型 | `typedef MyInt = int;` | [ ] |
| 構造体 | `struct Point { int x; int y; }` | [ ] |
| 列挙型 | `enum Color { Red, Green, Blue }` | [ ] |
| 配列 | `int[10]`, `int[]` | [ ] |
| ポインタ | `int*`, `void*` | [ ] |
| 参照 | `int&` | [ ] |
| ジェネリック | `Vec<int>`, `Option<T>` | [ ] |

### 代入パターン

| パターン | 例 | チェック |
|---------|-----|---------|
| 単純代入 | `x = 10;` | [ ] |
| 複合代入 | `x += 10;` | [ ] |
| 配列要素代入 | `arr[0] = 10;` | [ ] |
| 構造体フィールド代入 | `p.x = 10;` | [ ] |
| ポインタ経由代入 | `*ptr = 10;` | [ ] |
| 初期化子 | `Point p = Point{ x: 1, y: 2 };` | [ ] |

### 式パターン

| パターン | 例 | チェック |
|---------|-----|---------|
| リテラル | `42`, `3.14`, `"hello"` | [ ] |
| 算術演算 | `a + b`, `a * b` | [ ] |
| 比較演算 | `a < b`, `a == b` | [ ] |
| 論理演算 | `a && b`, `!a` | [ ] |
| 関数呼出 | `foo(x, y)` | [ ] |
| メソッド呼出 | `obj.method()` | [ ] |
| ラムダ式 | `(x) => x * 2` | [ ] |
| 三項演算子 | `cond ? a : b` | [ ] |
| キャスト | `x as int` | [ ] |

## テスト作成手順

### 1. 対象機能の特定

```bash
# 最近の変更を確認
git diff HEAD~10 --name-only | grep -E "\.(cpp|hpp)$"
```

### 2. 関連するBNF規則の確認

```bash
# BNFファイルを確認
cat docs/design/cm_grammar.bnf | grep -A5 "該当キーワード"
```

### 3. テストケース生成

```
tests/test_programs/[機能名]/
├── [機能名]_basic.cm          # 基本的な動作
├── [機能名]_basic.expect
├── [機能名]_typedef.cm        # typedef型での動作
├── [機能name]_typedef.expect
├── [機能名]_struct.cm         # 構造体での動作
├── [機能名]_struct.expect
├── [機能名]_enum.cm           # enum での動作
├── [機能名]_enum.expect
├── [機能名]_array.cm          # 配列での動作
├── [機能名]_array.expect
├── [機能名]_combined.cm       # 組み合わせテスト
└── [機能名]_combined.expect
```

### 4. テストテンプレート

```cm
// [機能名]_typedef.cm - typedef型での[機能名]テスト

typedef MyInt = int;
typedef MyString = string;

int main() {
    // typedef型での動作確認
    MyInt x = 10;
    // [機能を使用するコード]
    
    println("PASS");
    return 0;
}
```

## v0.13.0機能のBNF追加項目

### mustブロック（新規）

```bnf
# must文（デッドコード防止）
must_statement ::= 'must' block
```

### マクロ宣言（更新）

```bnf
# 定数マクロ
macro_decl ::= 'macro' type identifier '=' literal ';'
             | 'macro' type '*(' type_list? ')' identifier '=' lambda_expr ';'
```

### スレッドAPI（標準ライブラリ）

```bnf
# std::thread モジュール（libcラップ）
# spawn, join, detach, sleep_ms
```

## クイックチェックコマンド

```bash
# 全テスト実行
make tip

# 特定カテゴリのテスト
./tests/unified_test_runner.sh -b llvm -c [カテゴリ名]

# 単一ファイルテスト
./cm run tests/test_programs/[パス].cm

# BNFから未テスト構文を抽出（手動確認）
grep -E "^[a-z_]+ ::=" docs/design/cm_grammar.bnf | wc -l
```

## ファジングテスト観点

### エッジケース

1. **空の入力**: 空配列、空構造体、空文字列
2. **境界値**: 整数の最大/最小値、配列の0番目/最後
3. **ネスト**: 深いネスト（if文、ループ、構造体）
4. **再帰**: 再帰関数、再帰的データ構造

### 組み合わせ爆発の制御

すべての組み合わせをテストするのは現実的でないため:

1. **ペアワイズテスト**: 2つの要素の組み合わせを網羅
2. **代表的パターン**: 各カテゴリから1つずつ選択
3. **回帰テスト**: 過去に問題があった組み合わせを優先

## 懸念される未テスト領域

| 領域 | 懸念点 | 優先度 |
|------|--------|--------|
| typedef + 複合代入 | typedef型で`+=`等が動作するか | 高 |
| struct + must | 構造体フィールド更新がmustで保護されるか | 高 |
| enum + match guard | ガード条件でenum変数が使えるか | 中 |
| ジェネリクス + マクロ | ジェネリック型でマクロが展開されるか | 中 |
| 配列 + スレッド | 配列をスレッド間で共有できるか | 低 |

## 関連ドキュメント

- [BNF文法](../../docs/design/cm_grammar.bnf)
- [文法仕様](../../docs/spec/grammar.md)
- [テストガイド](../../docs/tests/TESTING_GUIDE.md)
