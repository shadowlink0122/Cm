---
layout: default
title: 文字列処理
parent: Tutorials
nav_order: 25
---

# 高度な機能編 - 文字列操作

**難易度:** 🟡 中級  
**所要時間:** 20分

## 文字列メソッド

```cm
string str = "Hello, World!";

int len = str.len();
char first = str.charAt(0);
string sub1 = str.substring(0, 5);
int pos = str.indexOf("World");
string upper = str.toUpperCase();
string lower = str.toLowerCase();
string trimmed = "  text  ".trim();
bool starts = str.startsWith("Hello");
bool ends = str.endsWith("!");
bool contains = str.contains("World");
string repeated = "Ha".repeat(3);
string replaced = str.replace("World", "Cm");
```

## 文字列スライス

```cm
string s = "Hello, World!";

string sub1 = s[0:5];
string sub2 = s[7:12];
string tail = s[7:];
string head = s[:5];
string copy = s[:];
string last3 = s[-3:];
```

---

**前の章:** [関数ポインタ](function-pointers.md)  
**次の章:** [コンパイラの使い方](../compiler/usage.md)
