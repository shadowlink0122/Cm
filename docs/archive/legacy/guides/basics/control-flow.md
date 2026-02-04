[English](control-flow.en.html)

# 言語ガイド - 制御構文

## 条件分岐 (if)

```cm
if (x > 0) {
    println("Positive");
} else if (x < 0) {
    println("Negative");
} else {
    println("Zero");
}
```

## ループ (while, for)

```cm
while (i < 10) {
    i++;
}

for (int j = 0; j < 10; j++) {
    println(j);
}
```

## match 式

```cm
match (value) {
    0 => println("zero"),
    _ => println("other"),
}
```