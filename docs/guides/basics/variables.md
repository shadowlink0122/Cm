# 言語ガイド - 変数と型

## プリミティブ型

Cm言語は、以下の基本型をサポートしています。

| 型 | 説明 | サイズ |
|---|---|---|
| `int` | 符号付き整数 | 32bit |
| `uint` | 符号なし整数 | 32bit |
| `long` | 符号付き長整数 | 64bit |
| `ulong` | 符号なし長整数 | 64bit |
| `float` | 単精度浮動小数点 | 32bit |
| `double` | 倍精度浮動小数点 | 64bit |
| `bool` | 論理型 (`true`/`false`) | 8bit |
| `char` | 文字型 | 8bit |
| `string` | 文字列型 | ポインタサイズ |

## 変数宣言

```cm
int x = 10;
const double PI = 3.14159;
static int counter = 0;
```

## 構造体 (Struct)

```cm
struct Point {
    int x;
    int y;
}
```
