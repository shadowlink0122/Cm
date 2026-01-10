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
    int x = 42;
    println("The answer is {}", x);

    string name = "Alice";
    int age = 25;
    println("{} is {} years old", name, age);
    return 0;
}
```

---

**前の章:** [環境構築](setup.html)  
**次の章:** [変数と型](variables.html)
