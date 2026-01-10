---
title: 制御構文
parent: Tutorials
---

[English](../../en/basics/control-flow.html)

# 基本編 - 制御構文

**難易度:** 🟢 初級  
**所要時間:** 30分

## 📚 この章で学ぶこと

- if文による条件分岐
- while/forループ
- switch文
- defer文
- break/continue

---

## if文

### 基本的なif文

```cm

    println("Grade: A");
} else if (score >= 80) {
    println("Grade: B");
} else if (score >= 70) {
    println("Grade: C");
} else if (score >= 60) {
    println("Grade: D");
} else {
    println("Grade: F");
}
```

### 条件式

```cm

    println("Positive");
}

    println("Even");
}

    println("Between 5 and 15");
}
```

### ネストしたif

```cm
bool has_license = true;

        println("Can drive");
    } else {
        println("Need license");
    }
} else {
    println("Too young");
}
```

---

## 三項演算子

```cm

// ネスト可能（ただし可読性に注意）
```

---

## while文

### 基本的なwhile

```cm
while (i < 5) {
    println("{}", i);
}
// 出力: 0, 1, 2, 3, 4
```

### 無限ループ

```cm
while (true) {
    println("{}", count);
    count++;
    
        break;  // ループを抜ける
    }
}
```

### 条件ループ

```cm

while (sum < 100) {
}

println("Sum: {}", sum);
```

---

## for文

### C風のforループ

```cm
// 基本形
for (int i = 0; i < 5; i++) {
    println("{}", i);
}

// 初期化式なし
for (; j < 5; j++) {
    println("{}", j);
}

// 複数の変数
for (int i = 0, j = 10; i < 5; i++, j--) {
    println("i={}, j={}", i, j);
}
```

### 範囲ベースfor（for-in）

```cm

// 型指定あり
for (int n in arr) {
    println("{}", n);
}

// 型推論
for (n in arr) {
    println("{}", n);
}
```

### 構造体配列のループ

```cm
}

    Point[3] points;
    points[0] = Point(10, 20);
    points[1] = Point(30, 40);
    points[2] = Point(50, 60);
    
    for (p in points) {
        println("({}, {})", p.x, p.y);
    }
    
    return 0;
}
```

---

## switch文

**重要:** Cmのswitch文はcase()構文を使い、自動的にbreakします。

### 基本的なswitch

```cm

    case(1) {
        println("Monday");
    }
    case(2) {
        println("Tuesday");
    }
    case(3) {
        println("Wednesday");
    }
    case(4) {
        println("Thursday");
    }
    case(5) {
        println("Friday");
    }
    case(6 | 7) {
        println("Weekend");
    }
    else {
        println("Invalid day");
    }
}
```

**構文の特徴:**
- `case(値)` - 括弧で値を囲む
- `{ }` - 各caseはブロックで囲む
- `break不要` - 自動的にbreak（フォールスルーなし）
- `else` - デフォルトケース（defaultの代わり）

### Enum値とswitch

```cm
enum Status {
    Ok = 0,
    Pending = 2
}

    
        case(Status::Ok) {
            println("Success");
        }
        case(Status::Error) {
            println("Failed");
        }
        case(Status::Pending) {
            println("Waiting");
        }
    }
    
    return 0;
}
```

### ORパターン（複数値）

```cm

    case(1 | 2 | 3) {
        println("1, 2, or 3");
    }
    case(4 | 5) {
        println("4 or 5");
    }
    else {
        println("Other");
    }
}
```

### 範囲パターン

```cm

    case(90...100) {
        println("Grade: A");
    }
    case(80...89) {
        println("Grade: B");
    }
    case(70...79) {
        println("Grade: C");
    }
    case(60...69) {
        println("Grade: D");
    }
    else {
        println("Grade: F");
    }
}
```

### 複合パターン

```cm

    case(1...5 | 10 | 20...30) {
        // (1 <= x <= 5) || x == 10 || (20 <= x <= 30)
        println("Matched!");
    }
    else {
        println("Not matched");
    }
}
```

---

## break/continue

### break - ループを抜ける

```cm
// while文
while (true) {
        break;
    }
    println("{}", i);
}

// for文
for (int j = 0; j < 10; j++) {
        break;
    }
    println("{}", j);
}
```

### continue - 次の繰り返しへ

```cm
// 偶数のみ出力
for (int i = 0; i < 10; i++) {
        continue;  // 奇数はスキップ
    }
    println("{}", i);
}

// while文
while (n < 10) {
        continue;
    }
    println("{}", n);
}
```

---

## defer文

関数終了時に実行される処理を登録します。

### 基本的な使い方

```cm
void example() {
    defer println("3rd");
    defer println("2nd");
    defer println("1st");
    println("Start");
}
// 出力: Start, 1st, 2nd, 3rd
```

### リソース管理

```cm
void process_file() {
    // ファイルを開く（仮想的な例）
    println("Opening file");
    defer println("Closing file");
    
    println("Processing...");
    
    // deferは関数終了時に自動実行される
}
```

### エラーハンドリング

```cm
    defer println("divide() finished");
    
        println("Error: division by zero");
        return -1;
    }
    
    return a / b;
}
```

---

## ループのネスト

### 2重ループ

```cm
// 九九の表
for (int i = 1; i <= 9; i++) {
    for (int j = 1; j <= 9; j++) {
        // 改行なしで出力（各自実装が必要）
        println("{}", i * j);
    }
}
```

**注意:** Cmには改行なしの`print()`関数は標準ライブラリにありません。

### breakでネストを抜ける

```cm
bool found = false;
for (int i = 0; i < 10 && !found; i++) {
    for (int j = 0; j < 10; j++) {
            println("Found: {}*{} = 42", i, j);
            found = true;
            break;  // 内側のループを抜ける
        }
    }
}
```

---

## よくある間違い

### ❌ breakを使おうとする

**Cmではbreak不要です！**

```cm
    case(1) {
        println("One");
        // breakは不要！自動的に次のcaseに進まない
    }
    case(2) {
        println("Two");
    }
}
// 出力: "One"のみ
```

### ❌ C++風の構文を使う

```cm
// 間違い: C++風の構文
    case 1:  // エラー: case()を使う必要がある
        println("One");
        break;
}

// 正しい: Cm構文
    case(1) {
        println("One");
    }
}
```

### ❌ default を使う

```cm
// 間違い
    case(1) { println("One"); }
    default:  // エラー: elseを使う
        println("Other");
}

// 正しい
    case(1) { println("One"); }
    else {
        println("Other");
    }
}
```

### ❌ 無限ループ

```cm
// ループ条件が常にtrue
while (i < 10) {
    println("{}", i);
    // i++を忘れている！
}
```

### ❌ セミコロンの位置

```cm
while (i < 5);  // セミコロンがある！
{
    println("{}", i);
}
// 無限ループになる
```

---

## 練習問題

### 問題1: FizzBuzz
1から100までの数について、3の倍数なら"Fizz"、5の倍数なら"Buzz"、両方の倍数なら"FizzBuzz"、それ以外は数値を出力してください。

<details>
<summary>解答例</summary>

```cm
    for (int i = 1; i <= 100; i++) {
            println("FizzBuzz");
        } else if (i % 3 == 0) {
            println("Fizz");
        } else if (i % 5 == 0) {
            println("Buzz");
        } else {
            println("{}", i);
        }
    }
    return 0;
}
```
</details>

### 問題2: 階乗計算
整数nの階乗を計算する関数を実装してください。

<details>
<summary>解答例</summary>

```cm
    for (int i = 1; i <= n; i++) {
        result *= i;
    }
    return result;
}

    println("5! = {}", factorial(5));  // 120
    println("10! = {}", factorial(10)); // 3628800
    return 0;
}
```
</details>

### 問題3: 素数判定
与えられた数が素数かどうか判定してください。

<details>
<summary>解答例</summary>

```cm
bool is_prime(int n) {
        return false;
    }
    
    for (int i = 2; i * i <= n; i++) {
            return false;
        }
    }
    
    return true;
}

    for (int i = 2; i <= 20; i++) {
            println("{} is prime", i);
        }
    }
    return 0;
}
```
</details>

---

## 次のステップ

✅ if文で条件分岐ができる  
✅ while/forでループが書ける  
✅ switchで多分岐ができる  
✅ defer文でリソース管理ができる  
⏭️ 次は [関数](functions.html) を学びましょう

## 関連リンク

- [match式](../advanced/match.html) - 高度なパターンマッチング
- [演算子](operators.html) - 条件式で使う演算子

---

**前の章:** [演算子](operators.html)  
**次の章:** [関数](functions.html)