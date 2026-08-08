---
title: typedef型エイリアス
parent: Tutorials
---

[English](../../en/types/typedef.html)

# 型システム編 - typedef型エイリアス

**難易度:** 🟡 中級  
**所要時間:** 15分

## 基本的な型エイリアス

```cm
typedef Integer = int;
typedef Real = double;
typedef Text = string;

Integer x = 42;
Real pi = 3.14159;
Text name = "Alice";
```

## 構造体のエイリアス

```cm
struct Point {
    int x;
    int y;
}

typedef Position = Point;

int main() {
    Position pos;
    pos.x = 10;
    pos.y = 20;
    return 0;
}
```

## ユニオン型

`typedef` で複数の型を `|` で結合すると、**ユニオン型**を定義できます。  
ユニオン型は、異なる型の値を一つの変数に格納できる型です。

```cm
import std::io::println;

// 複数の型を持てるユニオン型を定義
typedef Value = string | int | bool;

int main() {
    // ユニオン型の変数に異なる型の値を格納
    Value v1 = "hello" as Value;
    Value v2 = 42 as Value;
    Value v3 = true as Value;

    // as で元の型に戻す
    string s = v1 as string;
    int n = v2 as int;
    bool b = v3 as bool;

    println("s={s}, n={n}, b={b}");
    // 出力: s=hello, n=42, b=true
    return 0;
}
```

> **ポイント:** 値をユニオン型に格納するには `as Value`、取り出すには `as string` のようにキャストします。取り出し時はタグ検査が行われ、アクティブな変種と異なる型で取り出すと実行時パニックになります。
> スライス変種の判別・取り出し（`v is int[]`・`v as int[]`）もv0.17.0から書けます（従来は`int[]`が型として構文エラーでした）。
> ユニオン型の関数引数へは変種の値を直接渡せます（`take_union(9)`。v0.17.0で`as`不要に）。typedef別名を対象にしたas/is（`v as IntSlice`）も実体解決で照合されます。

### 実行時型判別: `is` 演算子とmatch型パターン（v0.16.0）

`is` 演算子で、ユニオン値のアクティブな変種を実行時に安全に判別できます。判別してから `as` で取り出せばパニックしません。

```cm
import std::io::println;

typedef Value = int | string;

int main() {
    Value v = 42;

    if (v is int) {
        int n = v as int;
        println("int: {n}");
    }

    v = "hello";
    bool s = v is string;
    println("is string: {s}");
    // 出力: int: 42 / is string: true
    return 0;
}
```

matchの**型パターン**（`型名 束縛名`）を使うと、判別と取り出しを一度に書けます。

```cm
import std::io::println;

typedef Value = int | string | bool;

int main() {
    Value v = 42;
    match (v) {
        int i => println("int: {i}"),
        string s => println("str: {s}"),
        bool b => println("bool: {b}"),
        _ => println("other"),
    }
    // 出力: int: 42
    return 0;
}
```

> **ポイント:** `is` の対象型はユニオンの変種のいずれかである必要があります（変種にない型はコンパイルエラー）。非ユニオン値への `is` もコンパイルエラーです。

### インラインユニオン型（v0.14.0）

typedefなしで、型宣言の中に直接ユニオン型を記述できます。

```cm
import std::io::println;

int main() {
    // null許容型（intまたはnull）
    int | null a = null;
    int | null b = 42 as int | null;
    
    // 3型以上のユニオン
    int | string | null c = null;
    
    // 構造体フィールドでも使用可能
    // struct Config {
    //     int | null timeout;
    //     string | null host;
    // }
    
    println("インラインユニオン型のテスト完了");
    return 0;
}
```

> **注意:** `operator` 戻り値型では `|` がビットOR演算子と競合するため、インラインユニオンは使用できません。`typedef` を使用してください。

### ユニオン型の配列（タプル的な使い方）

ユニオン型の配列を使うと、**異なる型の値を一つの配列にまとめて**扱えます。  
これはタプル（複数の異なる型の値の組）のような使い方ができます。

```cm
import std::io::println;

typedef Value = string | int | bool;

int main() {
    // 異なる型の値を一つの配列にまとめる（タプル的な使い方）
    Value[3] data = [
        "test" as Value,
        999 as Value,
        true as Value
    ];

    // インデックスで取り出してキャスト
    string s = data[0] as string;
    int n = data[1] as int;
    bool b = data[2] as bool;

    println("s={s}, n={n}, b={b}");
    // 出力: s=test, n=999, b=true
    return 0;
}
```

### 関数の引数・戻り値でのユニオン型

ユニオン型の配列は関数間で受け渡しできます。

```cm
import std::io::println;

typedef Value = string | int | bool;

// ユニオン型配列を返す関数
Value[3] make_values() {
    Value[3] arr = [
        "hello" as Value,
        42 as Value,
        true as Value
    ];
    return arr;
}

// ユニオン型配列を受け取る関数
void print_values(Value[3] vals) {
    string s = vals[0] as string;
    int n = vals[1] as int;
    bool b = vals[2] as bool;
    println("s={s}, n={n}, b={b}");
}

int main() {
    Value[3] v = make_values();
    print_values(v);
    // 出力: s=hello, n=42, b=true
    return 0;
}
```

## リテラル型

リテラル型は、取りうる値を**特定のリテラル値に制限**する型です。  
コンパイル時に不正な値の代入を検出できるため、型安全性が向上します。

```cm
typedef HttpMethod = "GET" | "POST" | "PUT" | "DELETE";
typedef Digit = 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9;
```

### 関数引数・戻り値でのリテラル型

リテラル型は関数の引数や戻り値として使用できます。  
文字列リテラル型は `string` として、整数リテラル型は `int` として扱われます。

```cm
import std::io::println;

typedef HttpMethod = "GET" | "POST" | "PUT" | "DELETE";
typedef StatusCode = 200 | 400 | 404 | 500;

// リテラル型を引数に取る関数
void handle_request(HttpMethod method) {
    println("Method: {method}");
}

// リテラル型を返す関数
HttpMethod get_method() {
    return "GET";
}

// 整数リテラル型
StatusCode get_status() {
    return 200;
}

int main() {
    handle_request("POST");
    // 出力: Method: POST

    HttpMethod m = get_method();
    println("m={m}");
    // 出力: m=GET

    StatusCode s = get_status();
    println("s={s}");
    // 出力: s=200
    return 0;
}
```

---

**前の章:** [Enum型](enums.html)  
**次の章:** [ジェネリクス](generics.html)

---

**最終更新:** 2026-02-12

---

<!-- nav -->
← 前: [型システム編 - Enum型](enums.html) ｜ [目次](index.html) ｜ 次: [型システム編 - ジェネリクス](generics.html) →
