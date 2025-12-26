# Cm言語 モジュールシステム ユーザーガイド

## 概要

Cm言語のモジュールシステムは、コードを論理的な単位に分割し、再利用可能なライブラリを作成するための機能を提供します。

## 基本概念

### モジュール宣言

ファイルの先頭で `module` キーワードを使用してモジュール名を宣言します：

```cm
// math.cm
module math;

export int add(int a, int b) {
    return a + b;
}

export int multiply(int a, int b) {
    return a * b;
}
```

### インポート

他のモジュールを使用するには `import` キーワードを使用します：

```cm
// main.cm
import ./math;

int main() {
    int result = math::add(10, 20);
    println("Result: {result}");
    return 0;
}
```

## インポート構文

### 1. 相対パスインポート

現在のファイルからの相対パスでモジュールを指定します：

```cm
import ./math;           // 同じディレクトリのmath.cm
import ./utils/string;   // utils/string.cm
import ../common/types;  // 親ディレクトリのcommon/types.cm
```

### 2. 絶対パスインポート（標準ライブラリ）

標準ライブラリは `std::` プレフィックスでインポートします：

```cm
import std::io;          // 標準入出力
import std::collections; // コレクション
```

**注意**: 標準ライブラリのパスは `CM_STD_PATH` 環境変数で設定できます。

### 3. 選択的インポート

特定の関数や型のみをインポートする場合：

```cm
import ./math::{add, multiply};

int main() {
    // 名前空間なしで直接使用可能
    int sum = add(10, 20);
    int product = multiply(3, 4);
    return 0;
}
```

### 4. ワイルドカードインポート

ディレクトリ内のすべてのモジュールをインポート：

```cm
import ./utils/*;

int main() {
    // 各モジュールは名前空間として使用可能
    string::format("Hello");
    number::parse("42");
    return 0;
}
```

### 5. エイリアスインポート

長いモジュール名に短い別名を付ける：

```cm
import ./very_long_module_name as m;

int main() {
    m::some_function();
    return 0;
}
```

### 6. サブモジュールインポート

モジュール内のサブモジュールを直接インポート：

```cm
// std.cmがio サブモジュールを再エクスポートしている場合
import ./lib/std::io;

int main() {
    io::println("Hello");  // std名前空間をスキップ
    return 0;
}
```

### 7. サブモジュールの関数を直接インポート

```cm
import ./lib/std::io::println;

int main() {
    println("Hello");  // 直接呼び出し
    return 0;
}
```

### 8. サブモジュールのワイルドカード

```cm
import ./lib/std::io::*;

int main() {
    println("Hello");  // io のすべてのエクスポートを直接使用
    print("World");
    return 0;
}
```

## エクスポート構文

### 1. 宣言時エクスポート

関数、構造体、定数を定義時にエクスポート：

```cm
// 関数のエクスポート
export int add(int a, int b) {
    return a + b;
}

// 構造体のエクスポート
export struct Point {
    int x;
    int y;
}

// 定数のエクスポート
export const int MAX_SIZE = 100;
```

### 2. 再エクスポート

インポートしたモジュールを再エクスポート：

```cm
// std.cm
module std;

import ./io;
import ./collections;

// サブモジュールを再エクスポート
export { io, collections };
```

使用側：

```cm
import ./std;

int main() {
    std::io::println("Hello");
    return 0;
}
```

## ディレクトリ構造

### 推奨構造

```
project/
├── main.cm
├── lib/
│   ├── math/
│   │   ├── math.cm      # エントリーポイント (module math;)
│   │   ├── basic.cm     # サブモジュール
│   │   └── advanced.cm  # サブモジュール
│   └── io/
│       ├── mod.cm       # エントリーポイント (module io;)
│       ├── file.cm
│       └── stream.cm
└── tests/
    └── test_math.cm
```

### エントリーポイント検出

ディレクトリをモジュールとしてインポートする場合、以下の順序でエントリーポイントを検索します：

1. `dirname/dirname.cm` (例: `math/math.cm`)
2. `dirname/mod.cm` (例: `math/mod.cm`)

## implの可視性

### 暗黙的エクスポート

構造体がエクスポートされている場合、その `impl` は自動的にエクスポートされます：

```cm
// shapes.cm
module shapes;

interface Printable {
    void print();
}

export struct Point {
    int x;
    int y;
}

// Pointがエクスポートされているので、このimplも自動的にエクスポート
impl Point for Printable {
    void print() {
        println("Point");
    }
}
```

使用側：

```cm
import ./shapes;

int main() {
    shapes::Point p;
    p.x = 10;
    p.y = 20;
    p.print();  // 暗黙的にエクスポートされたメソッド
    return 0;
}
```

### 制約

- **impl上書き禁止**: 同じ型への同じインターフェース実装は1回のみ
- **メソッド名重複禁止**: 異なるインターフェースでも同名メソッドは禁止

## 環境変数

| 変数名 | 説明 | 例 |
|--------|------|-----|
| `CM_STD_PATH` | 標準ライブラリのパス | `/usr/local/lib/cm/std` |
| `CM_MODULE_PATH` | 追加のモジュール検索パス | `/home/user/cm_libs` |

## ベストプラクティス

### 1. 明確なモジュール境界

```cm
// ✅ 良い例: 関連する機能をまとめる
// math/math.cm
module math;

export int add(int a, int b) { return a + b; }
export int subtract(int a, int b) { return a - b; }
export int multiply(int a, int b) { return a * b; }
export int divide(int a, int b) { return a / b; }
```

### 2. 適切な名前空間

```cm
// ✅ 良い例: 意味のある名前空間
import ./networking/http;
import ./networking/websocket;

http::get("https://example.com");
websocket::connect("ws://example.com");
```

### 3. 循環依存の回避

```cm
// ❌ 悪い例: 循環依存
// a.cm
import ./b;  // bがaをインポートしている場合、エラー

// ✅ 良い例: 共通モジュールを抽出
// common.cm - 共通の型や関数
// a.cm - commonをインポート
// b.cm - commonをインポート
```

### 4. 選択的インポートの活用

```cm
// ✅ 良い例: 必要な関数のみインポート
import ./math::{add, multiply};

// ❌ 避けるべき: 大量のワイルドカードインポート
import ./huge_library/*;  // 名前空間汚染の原因
```

## トラブルシューティング

### モジュールが見つからない

```
エラー: Module not found: ./utils/string
```

**解決策**:
1. ファイルパスが正しいか確認
2. ファイル拡張子 `.cm` が付いているか確認
3. ディレクトリ構造が正しいか確認

### 循環依存エラー

```
エラー: Circular dependency detected: a -> b -> a
```

**解決策**:
1. 依存関係を見直す
2. 共通モジュールを抽出する
3. インターフェースを使用して依存を逆転させる

### 名前空間の競合

```
エラー: Duplicate method: Point already has method 'print'
```

**解決策**:
1. メソッド名を変更する
2. 異なるインターフェース名を使用する

## 参考情報

- [モジュールシステム設計文書](../design/MODULE_SYSTEM_FINAL.md)
- [実装状況](../design/MODULE_IMPLEMENTATION_STATUS.md)
- [ロードマップ](../../ROADMAP.md)

## 既知の制限

### 階層再構築エクスポート

`export { io::{file, stream} }` 構文は部分的にサポートされていますが、完全な階層再構築は複雑なケースでは動作しない場合があります。

**代替方法**: 個別にインポートして直接使用するか、ディレクトリベースの構造を使用してください。

```cm
// 推奨: 直接インポート
import ./lib/io/file;
import ./lib/io/stream;

file::read_file(5);
stream::open_stream(3);

// または: ワイルドカードインポート
import ./lib/io/*;

file::read_file(5);
stream::open_stream(3);
```

### 階層再構築エクスポート

`export { ns::{item1, item2} }` 構文を使用して、インポートしたモジュールを親名前空間内に再配置できます：

```cm
// std.cm
module std;

import ./io/file;
import ./io/stream;

// fileとstreamをio名前空間内に配置して再エクスポート
export { io::{file, stream} };
```

使用側：

```cm
import ./std;

int main() {
    std::io::file::read_file(5);
    std::io::stream::open_stream(3);
    return 0;
}
```
