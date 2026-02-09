---
title: std::math
---

# std::math — 数学関数

三角関数、指数・対数、丸め、整数関数など数学機能を包括的に提供します。

> **対応バックエンド:** Native (LLVM) のみ

**最終更新:** 2026-02-08

---

## 定数

```cm
import std::math::{PI, E, INFINITY, NAN};
```

| 定数 | 型 | 値 |
|------|-----|-----|
| `PI` | `double` | 3.14159265358979... |
| `PI_F` | `float` | 3.14159265f |
| `E` | `double` | 2.71828182845904... |
| `INFINITY` | `double` | ∞ |
| `NEG_INFINITY` | `double` | -∞ |
| `NAN` | `double` | NaN |

---

## 三角関数

```cm
import std::math::{sin, cos, tan, asin, acos, atan, atan2, to_radians};

double angle = to_radians(45.0);
double s = sin(angle);
double c = cos(angle);
```

| 関数 | 説明 | double | float |
|------|------|--------|-------|
| `sin(x)` | 正弦 | ✅ | ✅ |
| `cos(x)` | 余弦 | ✅ | ✅ |
| `tan(x)` | 正接 | ✅ | ✅ |
| `asin(x)` | 逆正弦 | ✅ | - |
| `acos(x)` | 逆余弦 | ✅ | - |
| `atan(x)` | 逆正接 | ✅ | - |
| `atan2(y,x)` | 2引数逆正接 | ✅ | - |
| `sinh(x)` | 双曲正弦 | ✅ | - |
| `cosh(x)` | 双曲余弦 | ✅ | - |
| `tanh(x)` | 双曲正接 | ✅ | - |

---

## 指数・対数・累乗

```cm
import std::math::{exp, ln, log10, log2, pow, sqrt, cbrt, hypot};

double x = sqrt(2.0);      // 1.41421...
double y = pow(2.0, 10.0);  // 1024.0
double z = ln(E);           // 1.0
```

| 関数 | 説明 |
|------|------|
| `exp(x)` | e^x |
| `ln(x)` | 自然対数 |
| `log10(x)` | 常用対数 |
| `log2(x)` | 二進対数 |
| `pow(base, exp)` | 累乗 (double/float/int) |
| `sqrt(x)` | 平方根 |
| `cbrt(x)` | 立方根 |
| `hypot(x, y)` | 斜辺 √(x²+y²) |

---

## 丸め

```cm
import std::math::{ceil, floor, round, trunc, abs};

ceil(1.2);    // 2.0
floor(1.8);   // 1.0
round(1.5);   // 2.0
trunc(1.9);   // 1.0
abs(-42.0);   // 42.0
```

---

## 整数関数

```cm
import std::math::{gcd, lcm, factorial, fibonacci, is_prime, pow};

int g = gcd(12, 8);         // 4
int l = lcm(4, 6);          // 12
long f = factorial(10);     // 3628800
long fib = fibonacci(10);  // 55
bool p = is_prime(17);      // true
int pw = pow(2, 10);        // 1024
```

| 関数 | 戻り値 | 説明 |
|------|--------|------|
| `gcd(a, b)` | `int` | 最大公約数 |
| `lcm(a, b)` | `int` | 最小公倍数 |
| `factorial(n)` | `long` | 階乗 |
| `fibonacci(n)` | `long` | フィボナッチ数 |
| `is_prime(n)` | `bool` | 素数判定 |

---

## 角度変換

```cm
import std::math::{to_radians, to_degrees};

double rad = to_radians(180.0);   // PI
double deg = to_degrees(PI);      // 180.0
```

---

**関連:** [入出力](io.html) · [メモリ管理](mem.html) · [コアユーティリティ](core-utils.html)
