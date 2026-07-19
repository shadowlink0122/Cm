---
title: Enum型
parent: Tutorials
---

[English](../../en/types/enums.html)

# 型システム編 - Enum型

**難易度:** 🟡 中級  
**所要時間:** 20分

## 📚 この章で学ぶこと

- Enum型の定義方法
- メンバへのアクセス
- 整数値の割り当て
- switch/match文での活用

---

## Enum型の定義

Enum（列挙型）は、関連する定数の集合を定義するための型です。

```cm
enum Status {
    Ok,
    Error,
    Pending
}

int main() {
    Status s = Status::Ok;
    
    if (s == Status::Ok) {
        println("Success!");
    }
    return 0;
}
```

### 値の指定とオートインクリメント

メンバには整数値を割り当てることができます。省略した場合は 0 から始まり、前の値に +1 された値が自動的に割り当てられます。

```cm
enum Color {
    Red = 1,
    Green = 2,
    Blue = 4
}

enum Direction {
    North,      // 0
    East,       // 1
    South = 10,
    West        // 11 (10 + 1)
}

int main() {
    // 値の確認
    println("North: {Direction::North as int}"); // 0
    println("South: {Direction::South as int}"); // 10
    return 0;
}
```

---

## 関連データ付きEnum（Tagged Union）

**v0.13.0以降**

Cmでは、各バリアントに関連データを持つ列挙型（Tagged Union）を定義できます。

> **重要:** 各バリアントが持てるフィールドは **1つだけ** です。複数の値を持たせたい場合は構造体を使ってください。

### 基本的な定義

```cm
enum Message {
    Quit,                  // データなし
    Write(string),         // 1つの値を持つ
    Code(int)              // 1つの値を持つ
}

int main() {
    Message m1 = Message::Quit;
    Message m2 = Message::Write("Hello");
    Message m3 = Message::Code(404);
    return 0;
}
```

### ⚠️ 複数フィールドが必要な場合 → 構造体を使う

```cm
// ❌ 不正: バリアントは複数フィールドを持てない
// enum Shape {
//     Rectangle(int, int),  // コンパイルエラー
// }

// ✅ 正しい: 構造体でラップ
struct Rect { int w; int h; }
struct Color { int r; int g; int b; }

enum Shape {
    Circle(int),         // 半径
    Rectangle(Rect),     // 構造体で複数値を格納
    Colored(Color),      // RGB値を構造体で
    Point                // データなし
}
```

### matchでの分解

関連データ付きEnumは `match` 式でデータを取り出せます。

```cm
struct Rect { int w; int h; }

enum Shape {
    Circle(int),
    Rectangle(Rect),
    Point
}

void describe_shape(Shape s) {
    match (s) {
        Shape::Circle(r) => println("Circle with radius {r}"),
        Shape::Rectangle(rect) => println("Rectangle {rect.w}x{rect.h}"),
        Shape::Point => println("A point"),
    }
}

int main() {
    Shape c = Shape::Circle(5);
    describe_shape(c);  // Circle with radius 5

    Rect r = Rect { w: 10, h: 20 };
    Shape rect = Shape::Rectangle(r);
    describe_shape(rect);  // Rectangle 10x20
    return 0;
}
```

---

## ユニオン型配列によるタプル風パターン

Cmには `typedef` で定義するユニオン型があります。ユニオン型の配列を使うと、異なる型の値をまとめて返す「タプル」のような使い方ができます。

### ユニオン型の定義

```cm
// typedef でユニオン型を定義
typedef Value = int | long;
typedef Number = int | double;
typedef Data = int | string;
```

### ユニオン型配列で複数の値を返す

```cm
import std::io::println;

typedef Value = int | long;

// 商と余りを返す（ペア風）
Value[2] divide_with_remainder(int a, int b) {
    Value[2] pair;
    pair[0] = (a / b) as Value;
    pair[1] = (a % b) as Value;
    return pair;
}

// 座標を返す（3要素タプル風）
Value[3] get_point() {
    Value[3] point;
    point[0] = 10 as Value;
    point[1] = 20 as Value;
    point[2] = 30 as Value;
    return point;
}

int main() {
    // ユニオン型配列を受け取り、キャストで値を取り出す
    Value[2] dr = divide_with_remainder(17, 5);
    int quotient = dr[0] as int;
    int remainder = dr[1] as int;
    println("17 / 5 = {quotient} remainder {remainder}");

    Value[3] pt = get_point();
    int x = pt[0] as int;
    int y = pt[1] as int;
    int z = pt[2] as int;
    println("Point: ({x}, {y}, {z})");
    return 0;
}
```

> **注意:** ユニオン型の値は `as` キャストで代入・取り出しを行います。Tagged Union (enum) の `match` とは別の仕組みです。

---

## Result/Option（組み込みエラーハンドリング型）

**v0.16.0以降は言語組み込み**

エラーハンドリングは `Result<T, E>`、値の有無は `Option<T>` で表現します。v0.16.0からRust同様の**組み込み型**になり、enum定義なしでそのまま使えます（同名のenumを自分で定義した場合はそちらが優先されます）。

### Result型

処理の成功/失敗と値またはエラーを表現します。

```cm
import std::io::println;

Result<int, string> safe_divide(int a, int b) {
    if (b == 0) {
        return Result::Err("Division by zero");
    }
    return Result::Ok(a / b);
}

int main() {
    Result<int, string> r = safe_divide(10, 2);
    match (r) {
        Result::Ok(v) => { println("Result: {v}"); }
        Result::Err(e) => { println("Error: {e}"); }
    }
    return 0;
}
```

### Option型

値があるかないかを表現します。

```cm
import std::io::println;

Option<int> find_value(int[] arr, int target) {
    for (int i = 0; i < arr.len(); i++) {
        if (arr[i] == target) {
            return Option::Some(i);
        }
    }
    return Option::None;
}

int main() {
    int[] data = [1, 2, 3, 4, 5];
    Option<int> idx = find_value(data, 3);
    match (idx) {
        Option::Some(i) => { println("Found at index {i}"); }
        Option::None => { println("Not found"); }
    }
    return 0;
}
```

### メソッド（Rust準拠）

matchを書かずに判別・取り出しができます。`unwrap()` はErr/Noneのとき「panic: <メッセージ>」を出力して異常終了します（Rustのpanic!相当）。

```cm
Result<int, string> r = safe_divide(10, 2);
bool ok = r.is_ok();          // 成功か
bool err = r.is_err();        // 失敗か
int v = r.unwrap();           // 成功値（Errならパニック）
int v2 = r.unwrap_or(-1);     // 成功値またはデフォルト
int v3 = r.expect("must divide");  // 成功値（Errならメッセージ付きパニック）
// string e = r.unwrap_err(); // エラー値（Okならパニック）

Option<int> o = find_value([1, 2, 3], 2);
bool s = o.is_some();
bool n = o.is_none();
int i = o.unwrap_or(0);
```

### ?演算子（エラー伝播）

`expr?` はOk/Someならペイロードを返し、Err/Noneなら**現在の関数からそのまま早期return**します。Resultの`?`はResultを返す関数の中で、Optionの`?`はOptionを返す関数の中でのみ使用できます。

```cm
Result<int, string> calc(int a, int b) {
    int q = safe_divide(a, b)?;   // Errならここで呼び出し元へ伝播
    int q2 = safe_divide(q, 2)?;
    return Result::Ok(q2 * 10);
}
```

### must_use（未使用Resultの静的チェック）

Result型の値を使わずに文として捨てると、コンパイル時に `[must_use]` 警告が出ます。エラーの取りこぼしを防ぐため、matchやis_ok()等で必ず処理してください。

```cm
safe_divide(1, 0);   // warning: 未使用のResult値です [must_use]
```

### ランタイムエラーと安全APIの対応表

パニック（実行時異常終了）する操作には、Result/Optionで事前にハンドリングできる安全APIが用意されています。パニックはRust同様に回復不能（catchできない）ため、回復したい場合は安全API側を使ってください。

| パニックする操作 | 安全なハンドリング |
|------|------|
| `a / b`・`a % b`（除数0） | `std::math::checked_div(a, b)` / `checked_mod(a, b)` → `Option<int>` |
| `arr[i]`（範囲外。固定長=未定義値・スライス=0） | `arr.get(i)` → `Option<T>` |
| `v as T`（ユニオンの変種不一致） | `v is T` で事前判別、またはmatch型パターン |
| `r.unwrap()` / `o.unwrap()` | `unwrap_or(default)`・`is_ok()`/`is_some()`・match |
| ファイルI/O失敗 | `std::fs` のResult API（`read_to_string` 等） |
| `panic(msg)` / `assert` | 意図的な回復不能エラー（対応不要） |

---

## 制御構文での利用

### switch文での利用

Cmの `switch` 文は Enum と非常に相性が良く、`case(Enum::Member)` の形式で記述します。

```cm
enum Status { Ok, Error, Pending }

void handle_status(Status s) {
    switch (s) {
        case(Status::Ok) {
            println("All good.");
        }
        case(Status::Error) {
            println("Something went wrong.");
        }
        else {
            println("Still waiting...");
        }
    }
}

int main() {
    handle_status(Status::Error);
    return 0;
}
```

### match式での利用（推奨）

`match` 式を使用すると、全てのパターンを網羅しているかコンパイラがチェックするため、より安全です。

```cm
enum Status { Ok, Error, Pending }

void handle_status_safe(Status s) {
    match (s) {
        Status::Ok => println("OK"),
        Status::Error => println("Error"),
        Status::Pending => println("Pending"),
    }
}

int main() {
    handle_status_safe(Status::Pending);
    return 0;
}
```

---

## よくある間違い

### ❌ スコープ解決演算子の忘れ

Enumのメンバにアクセスするには、必ず `Enum名::メンバ名` と記述する必要があります。

```cm
enum Status { Ok }

int main() {
    // Status s = Ok;  // エラー: Ok が見つかりません
    Status s = Status::Ok; // 正解
    return 0;
}
```

### ❌ 異なるEnum型間の代入

名前が異なる Enum は別の型として扱われるため、直接代入することはできません。

```cm
enum A { X }
enum B { X }

int main() {
    A val_a = A::X;
    // B val_b = val_a; // エラー: 型が一致しません
    return 0;
}
```

---

## 練習問題

### 問題1: 曜日Enum
月曜日から日曜日までを表す Enum `Day` を作成し、引数で受け取った `Day` が週末（土日）かどうかを判定する関数 `is_weekend` を実装してください。

<details>
<summary>解答例</summary>

```cm
enum Day {
    Monday, Tuesday, Wednesday, Thursday, Friday, Saturday, Sunday
}

bool is_weekend(Day d) {
    match (d) {
        Day::Saturday | Day::Sunday => true,
        _ => false,
    }
}

int main() {
    Day today = Day::Saturday;
    if (is_weekend(today)) {
        println("It's weekend!");
    } else {
        println("Work day...");
    }
    return 0;
}
```
</details>

---

## 次のステップ

✅ Enumの定義と使い方がわかった  
✅ switch/matchでの活用方法を理解した  
⏭️ 次は [typedef型エイリアス](typedef.html) を学びましょう

## 関連リンク

- [switch文](../basics/control-flow.html)
- [match式](../advanced/match.html)
- [構造体](structs.html)

---

**前の章:** [構造体](structs.html)  
**次の章:** [typedef型エイリアス](typedef.html)
---

**最終更新:** 2026-02-08

---

<!-- nav -->
← 前: [型システム編 - 構造体](structs.html) ｜ [目次](index.html) ｜ 次: [型システム編 - typedef型エイリアス](typedef.html) →
