# 基本編 - 制御構文

**難易度:** 🟢 初級  
**所要時間:** 25分

## if文

```cm
int score = 85;

if (score >= 90) {
    println("Grade: A");
} else if (score >= 80) {
    println("Grade: B");
} else if (score >= 70) {
    println("Grade: C");
} else {
    println("Grade: F");
}
```

## while文

```cm
int i = 0;
while (i < 5) {
    println("{}", i);
    i++;
}
```

## for文

```cm
for (int i = 0; i < 5; i++) {
    println("{}", i);
}

// 範囲ベースfor
int[5] arr = {1, 2, 3, 4, 5};
for (int n in arr) {
    println("{}", n);
}
```

## switch文

**注意:** Cmのswitchは`case()`構文を使い、自動的にbreakします。

```cm
int day = 3;

switch (day) {
    case(1) {
        println("Monday");
    }
    case(2) {
        println("Tuesday");
    }
    else {
        println("Other day");
    }
}
```

## defer文

```cm
void example() {
    defer println("3rd");
    defer println("2nd");
    defer println("1st");
    println("Start");
}
// 出力: Start, 1st, 2nd, 3rd
```

---

**前の章:** [演算子](operators.md)  
**次の章:** [関数](functions.md)
CONTROL_FLOW
# 基本編 - 関数

**難易度:** 🟢 初級  
**所要時間:** 20分

## 基本的な関数定義

```cm
int add(int a, int b) {
    return a + b;
}

void greet(string name) {
    println("Hello, {}!", name);
}

int main() {
    int result = add(10, 20);
    greet("Alice");
    return 0;
}
```

## 関数オーバーロード

```cm
overload int max(int a, int b) {
    return a > b ? a : b;
}

overload double max(double a, double b) {
    return a > b ? a : b;
}
```

## デフォルト引数

```cm
void log(string message, int level = 1) {
    println("[Level {}] {}", level, message);
}

int main() {
    log("Error occurred");
    log("Warning", 2);
    return 0;
}
```

---

**前の章:** [制御構文](control-flow.md)  
**次の章:** [配列](arrays.md)
