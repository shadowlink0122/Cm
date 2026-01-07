---
title: Hello World
parent: Tutorials
---

[日本語](../../ja/basics/hello-world.html)

# Basics - Hello, World!

**Difficulty:** 🟢 Beginner  
**Time:** 10 minutes

## First Program

```cm
int main() {
    println("Hello, World!");
    return 0;
}
```

## How to Run

```bash
# Run with interpreter
./build/bin/cm run hello.cm

# Compile and run
./build/bin/cm compile hello.cm -o hello
./hello
```

## Using println

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

**Previous:** [Environment Setup](setup.html)  
**Next:** [Variables and Types](variables.html)
