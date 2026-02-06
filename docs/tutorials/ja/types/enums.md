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
    println("North: {}", (int)Direction::North); // 0
    println("South: {}", (int)Direction::South); // 10
    return 0;
}
```

---

## 関連データ付きEnum（Tagged Union）

**v0.13.0以降**

Cmでは、各バリアントに関連データを持つ列挙型（Tagged Union）を定義できます。

### 基本的な定義

```cm
enum Message {
    Quit,                      // データなし
    Move { int x; int y; },    // 構造体風データ
    Write(string),             // タプル風データ
    ChangeColor(int, int, int) // 複数の値
}

int main() {
    Message m1 = Message::Quit;
    Message m2 = Message::Move { x: 10, y: 20 };
    Message m3 = Message::Write("Hello");
    Message m4 = Message::ChangeColor(255, 128, 0);
    return 0;
}
```

### matchでの分解

関連データ付きEnumは `match` 式でデータを取り出せます。

```cm
enum Shape {
    Circle(int),           // 半径
    Rectangle(int, int),   // 幅, 高さ
    Point
}

void describe_shape(Shape s) {
    match (s) {
        Shape::Circle(r) => println("Circle with radius {}", r),
        Shape::Rectangle(w, h) => println("Rectangle {}x{}", w, h),
        Shape::Point => println("A point"),
    }
}

int main() {
    Shape c = Shape::Circle(5);
    describe_shape(c);  // Circle with radius 5
    return 0;
}
```

---

## Result/Optionパターン

**v0.13.0以降**

Cmでは、エラーハンドリングや値の有無を表現するために、`Result<T, E>`と`Option<T>`パターンを使用できます。これらは言語組み込みではなく、ユーザーが明示的にenum定義します。

### Result型

処理の成功/失敗と値またはエラーを表現します。

```cm
import std::io::println;

enum Result<T, E> {
    Ok(T),
    Err(E)
}

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

enum Option<T> {
    Some(T),
    None
}

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

### なぜユーザー定義か？

`Result`と`Option`はユーザーが明示的にenum定義する必要があります。これにより：

- **明示性**: コードで使用している型が明確
- **カスタマイズ可能**: `impl`ブロックでメソッドを追加可能
- **一貫性**: 他のenumと同じ扱い

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