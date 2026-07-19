---
title: Hello World
parent: Tutorials
---

[English](../../en/basics/hello-world.html)

# 基本編 - Hello, World!

**難易度:** 🟢 初級  
**所要時間:** 10分

## 最初のプログラム

```cm
int main() {
    println("Hello, World!");
    return 0;
}
```

## 実行方法

```bash
# インタプリタで実行
./build/bin/cm run hello.cm

# コンパイルして実行
./build/bin/cm compile hello.cm -o hello
./hello
```

## printlnの使い方

```cm
int main() {
    const int x = 42;  // 変更しない値はconstを使用
    println("The answer is {x}");

    const string name = "Alice";  // 文字列もconstで不変に
    const int age = 25;           // 年齢も変更しない
    println("{name} is {age} years old");
    return 0;
}
```

**重要（v0.11.0以降）**: 変更しない変数には必ず`const`をつけます。これによりコンパイラが最適化を行いやすくなり、コードの意図も明確になります。

---

**前の章:** [環境構築](setup.html)  
**次の章:** [変数と型](variables.html)

---

**最終更新:** 2026-02-08

---

<!-- nav -->
← 前: [環境構築](setup.html) ｜ [目次](index.html) ｜ 次: [基本編 - 変数と型](variables.html) →
