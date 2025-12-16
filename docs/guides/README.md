# Cm言語機能ガイド

このディレクトリには、Cm言語の各機能の詳細なガイドが含まれます。

## 📁 ディレクトリ構造

```
docs/guides/
├── README.md                    # このファイル
├── getting-started.md           # はじめてのCm言語
├── type-system.md               # 型システム
├── generics.md                  # ジェネリクス
├── interfaces.md                # インターフェース
├── memory.md                    # メモリ管理
├── pattern-matching.md          # パターンマッチング
├── operators.md                 # 演算子オーバーロード
└── llvm-backend.md              # LLVMバックエンド

docs/features/
├── README.md                    # 機能一覧
├── arrays.md                    # 配列
├── pointers.md                  # ポインタ
├── with-keyword.md              # with自動実装
├── match-expression.md          # match式
├── enums.md                     # Enum型
├── typedef.md                   # 型エイリアス
└── string-methods.md            # 文字列操作

docs/tutorials/
├── README.md                    # チュートリアル一覧
├── hello-world.md               # Hello, World!
├── calculator.md                # 電卓アプリ
├── data-structures.md           # データ構造
└── web-app-wasm.md              # WASMアプリ
```

## 🚀 学習パス

### 1. 初心者向け

1. **[はじめてのCm言語](getting-started.md)** - 環境構築と基本構文
2. **[Hello, World!](../tutorials/hello-world.md)** - 最初のプログラム（作成予定）
3. **[型システム](type-system.md)** - プリミティブ型と構造体
4. **[配列](../tutorials/arrays.md)** - 配列の使い方 ✅

### 2. 中級者向け

5. **[インターフェース](interfaces.md)** - interface/impl構文
6. **[ジェネリクス](generics.md)** - 型パラメータと制約
7. **[パターンマッチング](pattern-matching.md)** - match式
8. **[ポインタ](../tutorials/pointers.md)** - メモリアドレス操作 ✅

### 3. 上級者向け

9. **[演算子オーバーロード](operators.md)** - カスタム演算子
10. **[with自動実装](../tutorials/with-keyword.md)** - トレイト自動導出 ✅
11. **[メモリ管理](memory.md)** - 所有権とライフタイム
12. **[LLVMバックエンド](llvm-backend.md)** - ネイティブコンパイル

## 📖 クイックリファレンス

### 基本構文

```cm
// 変数宣言
int x = 10;
string name = "Cm";

// 関数定義
int add(int a, int b) {
    return a + b;
}

// 構造体
struct Point {
    int x;
    int y;
}
```

### インターフェースとimpl

```cm
interface Printable {
    void print();
}

impl Point for Printable {
    void print() {
        println("({}, {})", self.x, self.y);
    }
}
```

### ジェネリクス

```cm
<T> T identity(T value) {
    return value;
}

struct Box<T> {
    T value;
}
```

### match式

```cm
match (value) {
    0 => println("zero"),
    1 => println("one"),
    _ => println("other"),
}
```

## 🔗 関連リンク

- [正式言語仕様](../design/CANONICAL_SPEC.md)
- [サンプルコード](../../examples/)
- [API リファレンス](../spec/)

---

**更新日:** 2024-12-16
