[English](interfaces.en.html)

# 言語ガイド - インターフェース

## 定義

```cm
interface Drawable {
    void draw();
}
```

## 実装 (impl)

```cm
struct Circle {
    int radius;
}

impl Circle for Drawable {
    void draw() {
        println("Drawing circle");
    }
}
```

## Self 型

`self` は現在のインスタンスへの参照を表します。