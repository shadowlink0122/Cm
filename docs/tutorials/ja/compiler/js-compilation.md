---
title: JSコンパイル
parent: コンパイラ
---

# CmからJavaScriptへのコンパイル

Cmコンパイラは、CmソースコードをJavaScriptに変換するバックエンドを提供しています。
生成されたJSコードはNode.jsやブラウザで実行できます。

## 基本的な使い方

### コンパイル

```bash
# デフォルト出力（output.js）
./cm compile --target=js hello.cm

# 出力ファイル名を指定
./cm compile --target=js hello.cm -o app.js
```

### 実行

```bash
node output.js
```

## 対応機能

CmのJSバックエンドは以下の機能に対応しています:

| カテゴリ | 対応状況 | 例 |
|---------|---------|-----|
| 基本型 | ✅ | int, float, double, string, bool, char |
| 配列・スライス | ✅ | `int[3]`, `int[]` |
| 構造体 | ✅ | struct, impl, メソッド |
| Enum / Match | ✅ | Associated Data、パターンマッチ |
| ジェネリクス | ✅ | `Vector<T>`, `Pair<A, B>` |
| インターフェース | ✅ | interface, Display, Debug等 |
| クロージャ / Lambda | ✅ | キャプチャ付きクロージャ |
| 文字列補間 | ✅ | `"x={x}"` |
| イテレータ | ✅ | for-in, range, map, filter |
| モジュール | ✅ | import, namespace |
| 所有権・Move | ✅ | move セマンティクス |

## JS非対応機能

以下の機能はJSターゲットでは対応していません（スキップされます）:

| カテゴリ | 理由 |
|---------|------|
| ポインタ型 (`int*`, `void*`) | JSにポインタ概念なし |
| インラインアセンブリ (`__asm__`) | JSでは実行不可 |
| スレッド (`spawn`, `join`) | シングルスレッドモデル |
| Mutex / Channel / Atomic | 同上 |
| GPU演算 (Metal API) | ネイティブ専用 |
| ファイルI/O (`std::io`) | Node.js APIとの統合は今後 |
| TCP/HTTP (`std::net`) | 同上 |
| 低レベルメモリ (`malloc`, `free`) | GC管理 |

## サンプル

### Hello World

```cm
import std::io::println;

int main() {
    println("Hello from Cm → JavaScript!");
    return 0;
}
```

```bash
./cm compile --target=js hello.cm
node output.js
# 出力: Hello from Cm → JavaScript!
```

### Enum と Match

```cm
import std::io::println;

enum Shape {
    Circle(int),
    Rectangle(int, int)
}

int main() {
    Shape s = Shape::Circle(5);
    match (s) {
        Shape::Circle(r) => {
            println("Circle: radius={r}");
        }
        Shape::Rectangle(w, h) => {
            println("Rectangle: {w}x{h}");
        }
    }
    return 0;
}
```

### ジェネリクス

```cm
import std::io::println;

struct Pair<A, B> {
    A first;
    B second;
}

int main() {
    Pair<int, string> p = Pair<int, string> { first: 42, second: "hello" };
    println("first={p.first}, second={p.second}");
    return 0;
}
```

### インターフェース

```cm
import std::io::println;

interface Greet {
    void greet();
}

struct Dog {
    string name;
}

impl Greet for Dog {
    void greet() {
        println("Woof! I'm {this.name}");
    }
}

int main() {
    Dog d = Dog { name: "Buddy" };
    d.greet();
    return 0;
}
```

## テスト

JSバックエンドのテストは `make tjp` で実行できます:

```bash
# JS全テスト（並列実行）
make tjp

# 特定のテストのみ
./cm compile --target=js tests/test_programs/enum/associated_data.cm
node output.js
```

### v0.14.0 テスト結果

| 項目 | 値 |
|------|-----|
| テスト総数 | 372 |
| パス | 285 (77%) |
| 失敗 | 0 |
| スキップ | 87 |

## 生成JSの構造

CmのJSバックエンドは、各Cm関数をJavaScript関数に変換します:

```javascript
// Cmの main() → JSの main()
function main() {
    cm_println_string("Hello!");
    return 0;
}

// 自動生成されるランタイムヘルパー
function cm_println_string(s) { console.log(s); }
function cm_println_int(n) { console.log(n); }

// エントリポイント
main();
```

構造体はJavaScriptオブジェクトに、enumはタグ付きオブジェクトに変換されます。

## 注意事項

- 生成されるJSコードはNode.js v16以降で動作します
- ブラウザで実行する場合、`console.log`が利用可能な環境が必要です
- パフォーマンスはネイティブコンパイルより低下しますが、portable性が向上します
- 数値型はすべてJavaScriptのNumber型に変換されます（64bit floatの精度制限あり）
