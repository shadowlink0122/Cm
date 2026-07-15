---
title: 演算子
parent: Tutorials
---

[English](../../en/basics/operators.html)

# 基本編 - 演算子

**難易度:** 🟢 初級  
**所要時間:** 15分

## 📚 この章で学ぶこと

- 算術演算子
- 比較演算子
- 論理演算子
- ビット演算子
- 複合代入演算子
- インクリメント/デクリメント

---

## 算術演算子

### 基本演算

```cm
int main() {
    const int a = 10;  // 演算に使う値は不変
    const int b = 3;

    const int sum = a + b;      // 13 (加算)
    const int diff = a - b;     // 7  (減算)
    const int prod = a * b;     // 30 (乗算)
    const int quot = a / b;     // 3  (除算)
    const int rem = a % b;      // 1  (剰余)
    return 0;
}
```

### 浮動小数点演算

```cm
int main() {
    const double x = 10.0;  // constで不変に
    const double y = 3.0;

    const double sum = x + y;   // 13.0
    const double quot = x / y;  // 3.333...
    // double rem = x % y;  // エラー: %は整数のみ
    return 0;
}
```

### 単項演算子

```cm
int main() {
    const int x = 10;  // constで不変に
    const int neg = -x;         // -10 (符号反転)
    const int pos = +x;         // 10  (符号維持)
    return 0;
}
```

---

## 比較演算子

```cm
int main() {
    const int a = 10;  // constで不変に
    const int b = 20;

    const bool eq = (a == b);   // false (等しい)
    const bool ne = (a != b);   // true  (等しくない)
    const bool lt = (a < b);    // true  (より小さい)
    const bool le = (a <= b);   // true  (以下)
    const bool gt = (a > b);    // false (より大きい)
    const bool ge = (a >= b);   // false (以上)
    return 0;
}
```

### 文字列比較

```cm
int main() {
    string s1 = "abc";
    string s2 = "abc";
    string s3 = "def";

    bool same = (s1 == s2);     // true
    bool diff = (s1 != s3);     // true
    return 0;
}
```

---

## 論理演算子

### AND (&&)

```cm
int main() {
    bool a = true, b = false;
    bool result = a && b;  // false
    
    int x = 5;
    if (x > 0 && x < 10) {
        println("xは1から9の間");
    }
    return 0;
}
```

### OR (||)

```cm
int main() {
    bool a = true, b = false;
    bool result = a || b;  // true
    
    int x = 150;
    if (x < 0 || x > 100) {
        println("範囲外");
    }
    return 0;
}
```

### NOT (!)

```cm
int main() {
    bool flag = true;
    bool negated = !flag;  // false
    
    bool is_empty = false;
    if (!is_empty) {
        println("空ではない");
    }
    return 0;
}
```

---

## 複合代入演算子

```cm
int main() {
    int x = 10;

    x += 5;   // x = x + 5;  → 15
    x -= 3;   // x = x - 3;  → 12
    x *= 2;   // x = x * 2;  → 24
    x /= 4;   // x = x / 4;  → 6
    x %= 4;   // x = x % 4;  → 2
    return 0;
}
```

---

## ビット演算子

ビット単位で整数を操作します。フラグ管理やハードウェア制御で頻繁に使用されます。

### 基本演算

```cm
int main() {
    int a = 0b1100;  // 12 (2進数)
    int b = 0b1010;  // 10

    int and_r = a & b;   // 0b1000 = 8  (論理積: 両方が1の時のみ1)
    int or_r  = a | b;   // 0b1110 = 14 (論理和: どちらかが1なら1)
    int xor_r = a ^ b;   // 0b0110 = 6  (排他的論理和: 異なる時のみ1)
    int not_r = ~a;      // ビット反転
    return 0;
}
```

### シフト演算

```cm
int main() {
    int x = 1;

    int left  = x << 3;  // 1 * 2^3 = 8  (左シフト)
    int right = 16 >> 2;  // 16 / 2^2 = 4 (右シフト)

    // フラグ操作
    int flags = 0;
    flags = flags | (1 << 0);   // ビット0をセット
    flags = flags | (1 << 2);   // ビット2をセット
    flags = flags & ~(1 << 0);  // ビット0をクリア

    // ビットチェック
    bool bit2_set = (flags & (1 << 2)) != 0;  // true
    return 0;
}
```

### ビット複合代入

```cm
int main() {
    int x = 0xFF;

    x &= 0x0F;   // x = x & 0x0F  (下位4ビットを抽出)
    x |= 0x30;   // x = x | 0x30  (ビットをセット)
    x ^= 0xFF;   // x = x ^ 0xFF  (ビットを反転)
    x <<= 2;     // x = x << 2    (左シフト)
    x >>= 1;     // x = x >> 1    (右シフト)
    return 0;
}
```

---

## インクリメント/デクリメント

### 後置演算子

```cm
int main() {
    int i = 5;
    int a = i++;  // a = 5, i = 6 (使用後に加算)
    int b = i--;  // b = 6, i = 5 (使用後に減算)
    return 0;
}
```

### 前置演算子

```cm
int main() {
    int i = 5;
    int a = ++i;  // a = 6, i = 6 (加算後に使用)
    int b = --i;  // b = 5, i = 5 (減算後に使用)
    return 0;
}
```

### ループでの使用

```cm
int main() {
    // 後置（一般的）
    for (int i = 0; i < 10; i++) {
        println("{}", i);
    }

    // 前置（同じ結果）
    for (int i = 0; i < 10; ++i) {
        println("{}", i);
    }
    return 0;
}
```

---

## 演算子の優先順位

優先度（高→低）:

1. `()` - 括弧
2. `++`, `--`, `!`, `~`, `+`(単項), `-`(単項) - 単項演算子
3. `*`, `/`, `%` - 乗除
4. `+`, `-` - 加減
5. `<<`, `>>` - シフト
6. `<`, `<=`, `>`, `>=` - 比較
7. `==`, `!=` - 等価
8. `&` - ビットAND
9. `^` - ビットXOR
10. `|` - ビットOR
11. `&&` - 論理AND
12. `||` - 論理OR
13. `? :` - 三項演算子
14. `=`, `+=`, `-=`, `&=`, `|=`, etc. - 代入

### 例

```cm
int main() {
    int result = 2 + 3 * 4;     // 14 (3*4が先)
    int result2 = (2 + 3) * 4;   // 20 (括弧が優先)

    bool b = 5 > 3 && 10 < 20;  // true
    bool b2 = 5 > 3 || false && true;  // true (&&が先)
    return 0;
}
```

---

## 三項演算子

条件式 `?` 真の値 `:` 偽の値

```cm
int main() {
    int a = 10, b = 20;
    int max = (a > b) ? a : b;

    int age = 20;
    string status = (age >= 20) ? "Adult" : "Minor";
    
    // ネスト可能（非推奨）
    int x = 5;
    int sign = (x > 0) ? 1 : (x < 0) ? -1 : 0;
    return 0;
}
```

---

## よくある間違い

### ❌ 代入と比較の混同

```cm
int main() {
    int x = 10;
    // if (x = 5) {  // 警告: 代入している
        // xは5になる
    // }

    // 正しい
    if (x == 5) {  // 比較
        // ...
    }
    return 0;
}
```

### ❌ 整数除算の精度

```cm
int main() {
    int result = 5 / 2;  // 2 (整数除算)

    // 正しい
    double d_result = 5.0 / 2.0;  // 2.5
    return 0;
}
```

---

## 練習問題

### 問題1: 計算機
2つの数値の四則演算を行う関数を書いてください。

<details>
<summary>解答例</summary>

```cm
int main() {
    int a = 15, b = 4;
    
    println("{} + {} = {}", a, b, a + b);
    println("{} - {} = {}", a, b, a - b);
    println("{} * {} = {}", a, b, a * b);
    println("{} / {} = {}", a, b, a / b);
    println("{} % {} = {}", a, b, a % b);
    
    return 0;
}
```
</details>

### 問題2: 閏年判定
年が閏年かどうかを判定してください。

<details>
<summary>解答例</summary>

```cm
bool is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int main() {
    int year = 2025;
    if (is_leap_year(year)) {
        println("{} is a leap year", year);
    } else {
        println("{} is not a leap year", year);
    }
    return 0;
}
```
</details>

---

## 次のステップ

✅ 演算子の種類を理解した  
✅ 優先順位がわかった  
⏭️ 次は [制御構文](control-flow.html) を学びましょう

## 関連リンク

- [演算子オーバーロード](../advanced/operators.html)

---

**前の章:** [変数と型](variables.html)  
**次の章:** [制御構文](control-flow.html)

---

**最終更新:** 2026-02-10

## ビットスライス（v0.16.0）

整数・`bit[N]` の部分ビットを範囲で読み書きできます（詳細は[SVバックエンド - データ構造](../compiler/sv/data.html)参照）:

```cm
uint x = 0xABCD;
uint hi = (x[15:8]) as uint;  // 0xAB
x[3:0] = 0;                   // 下位4bitをクリア
```


---

<!-- nav -->
← 前: [基本編 - 変数と型](variables.html) ｜ [目次](index.html) ｜ 次: [基本編 - 制御構文](control-flow.html) →
