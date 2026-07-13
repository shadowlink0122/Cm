---
title: パターンマッチング
parent: Tutorials
---

[English](../../en/advanced/match.html)

# 高度な機能編 - match式

**難易度:** 🔴 上級  
**所要時間:** 35分

## 📚 この章で学ぶこと

- match式の基本構文
- パターンマッチング
- パターンガード
- 網羅性チェック
- Enum値との組み合わせ

---

## 基本的なmatch式

### リテラルパターン

```cm
int main() {
    int value = 2;

    match (value) {
        0 => println("zero"),
        1 => println("one"),
        2 => println("two"),
        3 => println("three"),
        _ => println("other"),
    }
    return 0;
}
```

**構文:**
- `match (式) { パターン => 本体, ... }`
- `_` はワイルドカード（すべてにマッチ）
- 各パターンは `,` で区切る

### ブロック本体

```cm
int main() {
    int value = 2;
    match (value) {
        0 => {
            println("This is zero");
            println("Nothing here");
        },
        1 => {
            println("This is one");
            println("First number");
        },
        _ => {
            println("Other number");
        },
    }
    return 0;
}
```

### 戻り値を返す（式形式）

全アームが式本体（`パターン => 式`）のmatchは値を返す式として使える。

```cm
string name = match (value) {
    0 => "zero",
    1 => "one",
    2 => "two",
    _ => "other",
};
```

scrutineeに関数呼び出しを直接書いても1回だけ評価される。

```cm
int x = match (getv(21)) {
    Result::Ok(v) => v,
    Result::Err(e) => -1,
};
```

注意: アームの式本体の中にさらに式形式matchをネストして、そのscrutineeに関数呼び出しを書くことはできない（アーム本体をブロック形式にして変数へ受けてからmatchする）。

---

## Enum値パターン

### 基本的な使い方

```cm
enum Status {
    Ok = 0,
    Error = 1,
    Pending = 2
}

void handle_status(int s) {
    match (s) {
        Status::Ok => println("Success!"),
        Status::Error => println("Failed!"),
        Status::Pending => println("Waiting..."),
    }
}

int main() {
    int status = Status::Ok;
    handle_status(status);
    return 0;
}
```

### Enumとワイルドカード

```cm
enum Color {
    Red = 0,
    Green = 1,
    Blue = 2,
    Yellow = 3,
    Cyan = 4
}

void classify_color(int c) {
    match (c) {
        Color::Red => println("Primary: Red"),
        Color::Green => println("Primary: Green"),
        Color::Blue => println("Primary: Blue"),
        _ => println("Secondary color"),
    }
}
```

---

## パターンガード

### 基本的なガード

```cm
int classify(int n) {
    match (n) {
        n if n < 0 => println("Negative"),
        n if n == 0 => println("Zero"),
        n if n > 0 => println("Positive"),
    }
    return 0;
}
```

**構文:** `パターン if 条件式 => 本体`

### 複雑な条件

```cm
int analyze(int x) {
    match (x) {
        x if x % 2 == 0 && x > 0 => println("Positive even"),
        x if x % 2 == 0 && x < 0 => println("Negative even"),
        x if x % 2 != 0 && x > 0 => println("Positive odd"),
        x if x % 2 != 0 && x < 0 => println("Negative odd"),
        _ => println("Zero"),
    }
    return 0;
}
```

### 範囲チェック

```cm
void check_score(int score) {
    match (score) {
        s if s >= 90 => println("Grade: A"),
        s if s >= 80 => println("Grade: B"),
        s if s >= 70 => println("Grade: C"),
        s if s >= 60 => println("Grade: D"),
        _ => println("Grade: F"),
    }
}
```

---

## 変数束縛パターン

### 変数の再利用

```cm
int describe(int n) {
    match (n) {
        x if x % 2 == 0 => println("{} is even", x),
        x if x % 2 == 1 => println("{} is odd", x),
    }
    return 0;
}
```

### 計算に使用

```cm
void fizzbuzz(int n) {
    match (n) {
        x if x % 15 == 0 => println("FizzBuzz"),
        x if x % 3 == 0 => println("Fizz"),
        x if x % 5 == 0 => println("Buzz"),
        x => println("{}", x),
    }
}

int main() {
    for (int i = 1; i <= 15; i++) {
        fizzbuzz(i);
    }
    return 0;
}
```

---

## 網羅性チェック

### Enum型の網羅

```cm
enum TrafficLight {
    Red = 0,
    Yellow = 1,
    Green = 2
}

void handle_light(int light) {
    match (light) {
        TrafficLight::Red => println("Stop"),
        TrafficLight::Yellow => println("Caution"),
        TrafficLight::Green => println("Go"),
        // 全てのケースを網羅している
    }
}
```

### 網羅性エラー

```cm
// コンパイルエラー: すべてのケースが網羅されていない
/*
void incomplete(int light) {
    match (light) {
        TrafficLight::Red => println("Stop"),
        TrafficLight::Yellow => println("Caution"),
        // TrafficLight::Greenが欠けている！
    }
}
*/
```

### ワイルドカードで解決

```cm
void with_default(int light) {
    match (light) {
        TrafficLight::Red => println("Stop"),
        TrafficLight::Yellow => println("Caution"),
        _ => println("Go or other"),
        // ワイルドカードで残りをカバー
    }
}
```

---

## match vs switch

### switchの場合

```cm
int main() {
    int value = 2;

    switch (value) {
        case(0) {
            println("zero");
        }
        case(1) {
            println("one");
        }
        case(2) {
            println("two");
        }
        else {
            println("other");
        }
    }
    return 0;
}
```

### matchの場合

```cm
int main() {
    int value = 2;

    match (value) {
        0 => println("zero"),
        1 => println("one"),
        2 => println("two"),
        _ => println("other"),
    }
    return 0;
}
```

**matchの利点:**
- ✅ breakが不要
- ✅ 網羅性チェック
- ✅ パターンガード
- ✅ 変数束縛

---

## 実践例

### HTTPステータスコード

```cm
enum HttpStatus {
    Ok = 200,
    NotFound = 404,
    InternalError = 500
}

void handle_response(int status) {
    match (status) {
        HttpStatus::Ok => println("Success"),
        HttpStatus::NotFound => println("Page not found"),
        HttpStatus::InternalError => println("Server error"),
        s if s >= 400 && s < 500 => println("Client error: {}", s),
        s if s >= 500 => println("Server error: {}", s),
        _ => println("Other status: {}", status),
    }
}
```

### ゲームの状態管理

```cm
enum GameState {
    Menu = 0,
    Playing = 1,
    Paused = 2,
    GameOver = 3
}

void update_game(int state) {
    match (state) {
        GameState::Menu => println("Show menu"),
        GameState::Playing => println("Update game logic"),
        GameState::Paused => println("Show pause screen"),
        GameState::GameOver => println("Show game over"),
    }
}
```

### 数値の分類

```cm
void classify_number(int n) {
    match (n) {
        0 => println("Zero"),
        n if n > 0 && n < 10 => println("Small positive: {}", n),
        n if n >= 10 && n < 100 => println("Medium positive: {}", n),
        n if n >= 100 => println("Large positive: {}", n),
        n if n < 0 && n > -10 => println("Small negative: {}", n),
        n if n <= -10 => println("Large negative: {}", n),
    }
}
```

---

## よくある間違い

### ❌ ワイルドカードの順序

```cm
// 間違い: ワイルドカードが先にあると他のパターンに到達しない
/*
match (value) {
    _ => println("Any"),  // これが全てマッチしてしまう
    0 => println("Zero"), // 到達不能
    1 => println("One"),  // 到達不能
}
*/
```

### ❌ 網羅性の不足

```cm
enum Status { Ok = 0, Error = 1 }

// エラー: すべてのケースが網羅されていない
/*
match (status) {
    Status::Ok => println("OK"),
    // Status::Errorが欠けている
}
*/
```

### ❌ カンマの忘れ

```cm
/*
match (value) {
    0 => println("zero"),
    1 => println("one")  // カンマがない！
    2 => println("two"),
}
*/
```

---

## 練習問題

### 問題1: 曜日判定
数値（1-7）を曜日名に変換してください。

<details>
<summary>解答例</summary>

```cm
void print_day(int day) {
    match (day) {
        1 => println("Monday"),
        2 => println("Tuesday"),
        3 => println("Wednesday"),
        4 => println("Thursday"),
        5 => println("Friday"),
        6 => println("Saturday"),
        7 => println("Sunday"),
        _ => println("Invalid day"),
    }
}

int main() {
    for (int i = 1; i <= 7; i++) {
        print_day(i);
    }
    return 0;
}
```
</details>

### 問題2: 成績判定
点数を成績（A-F）に変換してください。

<details>
<summary>解答例</summary>

```cm
void grade(int score) {
    match (score) {
        s if s >= 90 => println("A: Excellent"),
        s if s >= 80 => println("B: Good"),
        s if s >= 70 => println("C: Average"),
        s if s >= 60 => println("D: Pass"),
        s if s >= 0 => println("F: Fail"),
        _ => println("Invalid score"),
    }
}

int main() {
    grade(95);  // A
    grade(85);  // B
    grade(75);  // C
    grade(65);  // D
    grade(55);  // F
    return 0;
}
```
</details>

---

## 次のステップ

✅ match式の基本構文を理解した  
✅ パターンガードが使える  
✅ 網羅性チェックがわかった  
⏭️ 次は [with自動実装](with-keyword.html) を学びましょう

## 関連リンク

- [Enum型](../types/enums.html) - matchと組み合わせる
- [switch文](../basics/control-flow.html) - 従来の分岐
- [演算子](../basics/operators.html) - ガード条件で使う

---

**前の章:** [型制約](../types/constraints.html)  
**次の章:** [with自動実装](with-keyword.html)
---

**最終更新:** 2026-02-08

---

<!-- nav -->
← 前: [高度な機能編](index.html) ｜ [目次](index.html) ｜ 次: [withキーワード（自動トレイト実装）](with-keyword.html) →
