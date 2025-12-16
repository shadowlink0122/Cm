# 型システム編 - 構造体

**難易度:** 🟡 中級  
**所要時間:** 25分

## 構造体定義

```cm
struct Point {
    int x;
    int y;
}

struct Rectangle {
    int width;
    int height;
    string color;
}
```

## 構造体の初期化

```cm
Point p1;
p1.x = 10;
p1.y = 20;

Rectangle rect = Rectangle{100, 50, "blue"};
```

## コンストラクタ

```cm
impl Point {
    self() {
        this.x = 0;
        this.y = 0;
    }
    
    overload self(int x, int y) {
        this.x = x;
        this.y = y;
    }
}

int main() {
    Point p1;
    Point p2(10, 20);
    return 0;
}
```

## ネストした構造体

```cm
struct Line {
    Point start;
    Point end;
}

int main() {
    Line line;
    line.start.x = 0;
    line.start.y = 0;
    return 0;
}
```

---

**前の章:** [関数](../basics/functions.md)  
**次の章:** [Enum型](enums.md)
STRUCTS
# 型システム編 - Enum型

**難易度:** 🟡 中級  
**所要時間:** 20分

## 基本的なEnum

```cm
enum Status {
    Ok = 0,
    Error = 1,
    Pending = 2
}

int main() {
    int s = Status::Ok;
    
    if (s == Status::Ok) {
        println("Success!");
    }
    return 0;
}
```

## 負の値とオートインクリメント

```cm
enum Direction {
    North = 0,
    East,      // 1
    South,     // 2
    West       // 3
}

enum ErrorCode {
    Success = 0,
    NotFound = -1,
    PermissionDenied = -2
}
```

## switchで使用

**注意:** Cmのswitchは`case()`構文を使います。

```cm
enum Color {
    Red = 0,
    Green = 1,
    Blue = 2
}

void print_color(int c) {
    switch (c) {
        case(Color::Red) {
            println("Red");
        }
        case(Color::Green) {
            println("Green");
        }
        case(Color::Blue) {
            println("Blue");
        }
    }
}
```

---

**前の章:** [構造体](structs.md)  
**次の章:** [typedef](typedef.md)
