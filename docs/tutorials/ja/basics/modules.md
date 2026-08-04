---
title: モジュール
parent: Basics
---

[English](../../en/basics/modules.html)

# モジュールシステム

**学習目標:** Cm言語のモジュールシステムを使って、コードを整理し再利用する方法を学びます。  
**所要時間:** 30分  
**難易度:** 🟢 初級〜🟡 中級

---

## 概要

Cm言語は階層的なモジュールシステムを持ち、コードの整理と再利用を容易にします。

---

## 基本的なインポート

### 標準ライブラリのインポート

```cm
// std::io から println をインポート
import std::io::println;

int main() {
    println("Hello, World!");
    return 0;
}
```

### 複数の関数をインポート

```cm
// 複数のシンボルをインポート
import std::io::println;
import std::io::print;

int main() {
    print("Hello, ");
    println("World!");
    return 0;
}
```

---

## ワイルドカードインポート

モジュール内のすべてをインポートできます：

```cm
// std::io のすべてをインポート
import std::io::*;

int main() {
    println("これで println が使えます");
    return 0;
}
```

### 相対パスのワイルドカード

```cm
// 同じディレクトリの utils モジュールをすべてインポート
import ./utils/*;

int main() {
    helper_function();  // utils 内の関数
    return 0;
}
```

---

## エイリアスインポート

長いモジュール名に別名を付けられます：

```cm
// std::io を io という名前でインポート
import std::io as io;

int main() {
    io::println("エイリアス経由で呼び出し");
    return 0;
}
```

---

## 選択的インポート

特定のシンボルのみをインポート：

```cm
// println と print のみをインポート
import std::io::{println, print};

int main() {
    print("選択的");
    println("インポート");
    return 0;
}
```

---

## エクスポート

他のモジュールから使えるようにするには `export` を使います：

### 関数のエクスポート

```cm
// utils.cm
export int add(int a, int b) {
    return a + b;
}

// プライベート関数（エクスポートなし）
int internal_helper() {
    return 42;
}
```

### 構造体のエクスポート

```cm
// models.cm
export struct Point {
    int x;
    int y;
}

export Point new_point(int x, int y) {
    return Point { x: x, y: y };
}
```

### 再エクスポート

インポートしたモジュールを再エクスポート：

```cm
// lib.cm
import ./utils;
import ./models;

// 再エクスポート
export { utils, models };
```

---

## モジュール構造の例

```
my_project/
├── main.cm          # エントリーポイント
├── utils/
│   ├── math.cm      # 数学関数
│   └── strings.cm   # 文字列操作
└── models/
    └── user.cm      # User構造体
```

```cm
// main.cm
import ./utils/math::*;
import ./models/user::User;

int main() {
    int result = add(1, 2);
    User u = User { name: "Alice", age: 30 };
    return 0;
}
```

---

## 循環依存の回避

Cmコンパイラは循環依存を検出してエラーを報告します：

```
// ❌ エラー: a.cm と b.cm が互いにインポートし合う
// a.cm: import ./b;
// b.cm: import ./a;
```

**解決策:** 共通モジュールを作成して依存関係を整理します。

---

## よくある間違い

### 1. パスの間違い

```cm
// ❌ 間違い: std/io ではなく std::io
import std/io::println;

// ✅ 正しい
import std::io::println;
```

### 2. 未エクスポートのシンボル

```cm
// ❌ エラー: internal_helper は export されていない
import ./utils::internal_helper;
// → function 'internal_helper' selected by import is not exported from './utils.cm'

// ✅ 正しい: export されたシンボルのみ使用可能
import ./utils::add;
```

**v0.17.0から非export関数の選択importはコンパイルエラーになりました**（従来は警告のみ）。
公開したい関数には定義に `export` を付けるか、exportリスト（`export { fn1, fn2 };`）へ追加してください。
struct / enum / const / typedef などの型定義は従来どおり透過的に参照できます。

### 3. 同名シンボルの多重import

異なるモジュールから同名シンボルを取り込むと曖昧なためエラーになります（v0.17.0から。従来は先勝ちで黙って解決されていました）。

```cm
// ❌ エラー: 'compute' が2つのモジュールから来て曖昧
import ./a::{compute};
import ./b::{compute};
// → symbol 'compute' is imported from both './a.cm' and './b.cm' and is ambiguous

// ✅ 正しい: エイリアスで公開名を分ける
import ./a::{compute};
import ./b::{compute as compute_b};
```

---

## まとめ

| 構文 | 用途 |
|------|------|
| `import mod::func;` | 単一シンボル |
| `import mod::*;` | 全シンボル |
| `import mod as alias;` | エイリアス |
| `import mod::{a, b};` | 選択的 |
| `export func() {}` | エクスポート |
| `export { M };` | 再エクスポート |

---

## 次のステップ

- [FFI](../advanced/ffi.html) - 外部関数インターフェース
- [コンパイラの使い方](../compiler/common/usage.html) - ビルドオプション
---

**最終更新:** 2026-02-08

---

<!-- nav -->
← 前: [ポインタ（Pointers）](pointers.html) ｜ [目次](index.html) ｜ 次: [基本編 - 文字列補間](string-interpolation.html) →
