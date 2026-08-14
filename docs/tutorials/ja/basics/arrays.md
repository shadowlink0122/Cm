---
title: 配列
parent: Tutorials
---

[English](../../en/basics/arrays.html)

# 配列（Arrays）

Cm言語の配列は、C++スタイルの固定長配列をサポートします。
可変長のスライス型 `T[]` については[スライス型](../advanced/slices.html)を参照してください。

## 📋 目次

- [基本的な使い方](#basic-usage)
- [要素のアクセス](#element-access)
- [メソッド一覧](#method-reference)
- [検索メソッド](#search-methods)
- [高階関数メソッド](#higher-order-methods)
- [並べ替え・先頭末尾](#sorting-and-firstlast)
- [構造体配列](#arrays-of-structs)
- [要素の型検査](#element-type-checking)
- [ポインタへの変換](#pointer-decay)
- [for-in構文](#for-in-loops)
- [多次元配列](#multidimensional-arrays)

## 基本的な使い方 {#basic-usage}

```cm
// 基本的な配列宣言（ゼロ初期化）
int[5] numbers;

// 宣言と初期化
int[3] values = [1, 2, 3];

// 部分初期化（残りは0）
int[5] partial = [1, 2];  // [1, 2, 0, 0, 0]
```

配列サイズにはコンパイル時定数の整数式も書けます（コンパイル時に畳まれます）。const名や `sizeof` を含む式も使えます。

```cm
const int N = 2;
int[2 + 1] a = [1, 2, 3];        // int[3] と同じ
int[(1 + 2) * 2] b;              // int[6]
int[N + 1] c;                    // int[3]（const名を含む算術）
int[sizeof(int)] d;              // int[4]（プリミティブ・ポインタ・その固定長配列のsizeof）
```

実行時にしか決まらない値（非constの変数など）はコンパイルエラーになります。

## 要素のアクセス {#element-access}

```cm
int main() {
    int[5] numbers;
    int first = numbers[0];
    numbers[1] = 10;
    return 0;
}
```

> **v0.11.0の変更点**: 境界チェックが導入されました。範囲外のインデックスにアクセスすると、プログラムは安全に停止（パニック）します。

## メソッド一覧 {#method-reference}

固定長配列 `T[N]` で使用できるメソッドの一覧です（スライス `T[]` でも全て使用できます）。

| メソッド | 戻り値 | 説明 |
|---|---|---|
| `.size()` / `.len()` / `.length()` | `int` | 要素数 |
| `.dim()` | `int` | 次元数（多次元配列用） |
| `.indexOf(v)` | `int` | 最初に一致する位置（なければ-1） |
| `.includes(v)` / `.contains(v)` | `bool` | 要素が含まれるか |
| `.find(fn)` | `T` | 条件を満たす最初の要素 |
| `.findIndex(fn)` | `int` | 条件を満たす最初の位置（なければ-1） |
| `.some(fn)` | `bool` | いずれかが条件を満たすか |
| `.every(fn)` | `bool` | すべてが条件を満たすか |
| `.map(fn)` | `T[]` | 各要素を変換した新しいスライス |
| `.filter(fn)` | `T[]` | 条件を満たす要素の新しいスライス |
| `.reduce(fn, init)` | `T` | 畳み込み（引数順は関数, 初期値） |
| `.forEach(fn)` | `void` | 各要素に関数を適用 |
| `.sort()` | `T[]` | 昇順に並べた新しいスライス |
| `.sortBy(cmp)` | `T[]` | 比較関数で並べた新しいスライス |
| `.reverse()` | `T[]` | 逆順の新しいスライス |
| `.first()` / `.last()` | `T` | 先頭 / 末尾の要素 |
| `arr[a:b]` | `T[]` | 部分スライス（[スライス型](../advanced/slices.html)参照） |

関数を取るメソッドには、名前付き関数またはラムダ（引数の型注釈が必要）を渡します。

## 検索メソッド {#search-methods}

```cm
int main() {
    int[5] numbers = [1, 2, 3, 4, 5];

    int pos = numbers.indexOf(3);        // 2
    int not_found = numbers.indexOf(10); // -1

    bool has_3 = numbers.includes(3);    // true
    bool has_5 = numbers.contains(5);    // true（includesのエイリアス）
    return 0;
}
```

## 高階関数メソッド {#higher-order-methods}

```cm
import std::io::println;

bool is_even(int x) {
    return x % 2 == 0;
}

int add(int acc, int x) {
    return acc + x;
}

int main() {
    int[5] numbers = [1, 2, 3, 4, 5];

    // ラムダは引数に型注釈が必要
    bool has_even = numbers.some((int x) => { return x % 2 == 0; });  // true
    bool all_positive = numbers.every((int x) => { return x > 0; });  // true
    int idx = numbers.findIndex((int x) => { return x > 3; });        // 3（値4の位置）

    // 名前付き関数も渡せる
    int[] evens = numbers.filter(is_even);   // [2, 4]
    int[] doubled = numbers.map((int x) => { return x * 2; });  // [2, 4, 6, 8, 10]
    int total = numbers.reduce(add, 0);      // 15（引数順は 関数, 初期値）

    bool s = numbers.some(is_even);
    println("some={s} total={total}");
    return 0;
}
```

文字列補間の中で関数引数付きメソッドを直接呼び出すこともできます（`println("{numbers.some(is_even)}")` 等）。

高階関数はスカラー要素型の全幅（tiny/short/int/long/float/double とその符号なし版）で動作し、`reduce` の結果型はコールバックのアキュムレータ型に従います（`double` の合計は `double` で受けられます）。
構造体・文字列要素の高階関数は要素型に依存しない js/ts ターゲット専用で、native/jit/wasm ではコンパイルエラーになります（従来は無診断で誤った値を返していました）。

## 並べ替え・先頭末尾 {#sorting-and-firstlast}

```cm
int main() {
    int[5] nums = [3, 1, 4, 1, 5];

    int[] sorted = nums.sort();       // [1, 1, 3, 4, 5]（元の配列は不変）
    int[] rev = nums.reverse();       // [5, 1, 4, 1, 3]
    int[] desc = nums.sortBy((int a, int b) => { return b - a; });  // 降順

    int f = nums.first();  // 3
    int l = nums.last();   // 5
    return 0;
}
```

## 構造体配列 {#arrays-of-structs}

```cm
struct Point {
    int x;
    int y;
}

int main() {
    // 構造体配列の宣言
    Point[3] points;

    // フィールドへの代入
    points[0].x = 10;
    points[0].y = 20;

    // 構造体リテラルでの初期化
    Point[2] pts = [
        Point { x: 1, y: 2 },
        Point { x: 3, y: 4 }
    ];
    return 0;
}
```

## 要素の型検査 {#element-type-checking}

配列リテラルの各要素は、宣言した要素型と互換でなければなりません。変数宣言と同じ規則が適用されます。

```cm
int main() {
    // OK: 小さい整数型(tiny/short)から int への拡大変換は許可される
    tiny t = 1;
    short s = 2;
    int[3] a = [t, s, 3];

    // OK: 型名を明示しない構造体リテラルも、要素型に一致すれば許可される
    Point[2] pts = [{ x: 1, y: 2 }, { x: 3, y: 4 }];

    // エラー: 要素型と互換でない型を混在させるとコンパイルエラーになる
    // int[3] bad = [1, "hello", 3];   // 'int' に 'string' は代入できない
    return 0;
}
```

数値どうしの縮小変換（`int[] = [3.14]` 等）は、変数宣言と同じく警告の対象で、明示的な `as` キャストが推奨されます。

多次元配列（`int[2][2] = [[...], [...]]`）は、内側の要素まで再帰的に検査されます。

```cm
int main() {
    // OK: 内側も含めて要素型に一致
    int[2][2] m = [[1, 2], [3, 4]];

    // エラー: 内側要素の型不一致も捕捉される
    // int[2][2] bad = [[1, 2], ["a", 4]];   // 'int' に 'string' は代入できない
    return 0;
}
```

### 何でも格納する void* 配列

汎用ポインタ型 `void*` の配列は、要素型検査を免除される「エスケープハッチ」です。任意のポインタを格納でき、取り出しは `auto`、格納した実体の型判定は `typeof` で行うことを想定しています。

```cm
int main() {
    int n = 42;
    string s = "hi";
    Point p = Point { x: 1, y: 2 };

    // 異なる型のポインタを混在して格納できる
    void*[3] arr = [&n, &s, &p];

    // 取り出しは auto、型は typeof で判定し、適切な型へキャストして扱う
    auto e0 = arr[0];
    const string ty = typeof(e0);        // "*void"
    const int back = *(arr[0] as int*);  // 42
    return 0;
}
```

関数ポインタも `void*` 配列に格納できます。取り出して関数ポインタ型へ `as` キャストすれば呼び出せます。

```cm
int add(int a, int b) { return a + b; }

int main() {
    void*[1] fns = [&add];
    int*(int, int) op = fns[0] as int*(int, int);
    const int r = op(3, 2);  // 5
    return 0;
}
```

## ポインタへの変換 {#pointer-decay}

配列は自動的にポインタに変換されます（Array Decay）。

```cm
int main() {
    int[5] arr = [1, 2, 3, 4, 5];

    // 配列→ポインタ変換
    int* p = arr;  // arr[0]のアドレス

    // ポインタ経由でアクセス
    int first = *p;  // 1
    return 0;
}
```

## for-in構文 {#for-in-loops}

範囲ベースのforループで配列を反復処理できます。

```cm
import std::io::println;

struct Point {
    int x;
    int y;
}

int main() {
    int[5] numbers = [1, 2, 3, 4, 5];

    // 型指定あり
    for (int n in numbers) {
        println("{n}");
    }

    // 型推論
    for (n in numbers) {
        println("{n}");
    }
    return 0;
}
```

## 多次元配列 {#multidimensional-arrays}

配列サフィックスは左から積みます: `int[4][3]` は「`int[4]` が3個」、つまり3行×4列の2次元配列です。

```cm
int main() {
    // 2次元配列（int[4]の行が3個）
    int[4][3] matrix;

    // 初期化
    matrix[0][0] = 1;
    matrix[0][1] = 2;

    // ループでアクセス（外側の添字が行、内側が列）
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) {
            matrix[i][j] = i * 4 + j;
        }
    }

    int d = matrix.dim();  // 2（次元数）
    return 0;
}
```

### 低次元部分配列の取り出し（v0.17.0以降）

多次元配列に低次元の添字を与えると、部分配列がコピーで取り出せます。
`auto` による推論、関数引数・戻り値、任意の要素型（`double`・`string`・構造体等）とスライスにも対応します。

```cm
import std::io::println;

int main() {
    int[3][3][3] cube;
    cube[2][1][0] = 7;

    int[3] row = cube[2][1];   // 最内1次元をコピーで取り出す
    auto plane = cube[2];      // int[3][3] と推論される
    println(row[0]);           // 7

    row[0] = 99;               // コピーなので元のcubeへは波及しない
    println(cube[2][1][0]);    // 7

    // 要素数が合わない取り出しは型エラーになる
    // int[2] bad = cube[2][1];  // error: expected 'int[2]', got 'int[3]'
    return 0;
}
```

### 多次元スライスの要素操作（v0.17.0以降）

可変長スライスの要素（内側スライス）へは、添字レシーバで直接メソッドを呼び出せます。

```cm
int main() {
    int[][] rows = [];
    int[] r0 = [1];
    rows.push(r0);

    rows[0].push(42);          // 要素スライスへ直接push
    println(rows[0].len());    // 2
    println(rows[0][1]);       // 42（多重添字の直接読み）

    rows[0].pop();             // pop/delete/clear/len/capも同様
    return 0;
}
```

構造体フィールド経由の混合チェーン（`grid.cells[i].push(v)`）も解決されます。

### パフォーマンス最適化（v0.11.0以降）

**配列自動フラット化:** Cm v0.11.0は多次元配列を自動的に最適化し、キャッシュ性能を向上させます。

- キャッシュ局所性向上のため内部的に1次元配列へ変換（ユーザーに透過的）
- 大規模な行列演算で200-250倍の高速化・キャッシュミス率90%削減

## 実装状況

| バックエンド | 状態 |
|------------|------|
| JIT / LLVM Native | ✅ 完全対応 |
| WASM | ✅ 完全対応 |
| JS | ✅ 完全対応 |
| SV | ⚠️ 固定長配列はRAM/ROM推論（メソッドは合成可能な範囲） |

## 関連ドキュメント

- [スライス型](../advanced/slices.html) - 可変長スライスとpush/pop等の操作
- [ポインタ](pointers.html) - ポインタ操作
- [for-in構文](control-flow.html) - 範囲ベースループ

---

**更新日:** 2026-08-08

---

<!-- nav -->
← 前: [基本編 - 関数](functions.html) ｜ [目次](index.html) ｜ 次: [ポインタ（Pointers）](pointers.html) →
