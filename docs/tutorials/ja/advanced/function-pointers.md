---
title: 関数ポインタ
parent: Tutorials
---

[English](../../en/advanced/function-pointers.html)

# 高度な機能編 - 関数ポインタ

**難易度:** 🔴 上級  
**所要時間:** 25分

## 関数ポインタの宣言

```cm
int*(int, int) op;

int add(int a, int b) {
    return a + b;
}

int multiply(int a, int b) {
    return a * b;
}

int main() {
    op = add;
    int result1 = op(10, 20);
    
    op = multiply;
    int result2 = op(10, 20);
    
    return 0;
}
```

## 高階関数

```cm
int apply(int*(int, int) fn, int x, int y) {
    return fn(x, y);
}

int max(int a, int b) {
    return a > b ? a : b;
}

int min(int a, int b) {
    return a < b ? a : b;
}

int main() {
    int max_val = apply(max, 10, 5);
    int min_val = apply(min, 10, 5);
    return 0;
}
```

## void戻り値の関数ポインタ

```cm
void*(string) printer;

void print_upper(string s) {
    println(s.toUpperCase());
}

void print_lower(string s) {
    println(s.toLowerCase());
}

int main() {
    printer = print_upper;
    printer("Hello");
    
    printer = print_lower;
    printer("WORLD");
    
    return 0;
}
```

---

**前の章:** [演算子オーバーロード](operators.html)  
**次の章:** [文字列操作](strings.html)

---

**最終更新:** 2026-02-08

---

<!-- nav -->
← 前: [高度な機能編 - 演算子オーバーロード](operators.html) ｜ [目次](index.html) ｜ 次: [ラムダ式](lambda.html) →
