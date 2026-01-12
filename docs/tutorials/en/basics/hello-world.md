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
    const int x = 42;  // Use const for immutable values
    println("The answer is {}", x);

    const string name = "Alice";  // String is also const
    const int age = 25;           // Age won't change
    println("{} is {} years old", name, age);
    return 0;
}
```

**Important (v0.11.0+)**: Always use `const` for variables that won't be modified. This helps the compiler optimize better and makes your code intent clearer.

---

**Previous:** [Environment Setup](setup.html)  
**Next:** [Variables and Types](variables.html)
