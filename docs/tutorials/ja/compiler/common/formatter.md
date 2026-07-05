---
title: Formatter
parent: Compiler
nav_order: 2
---

# Formatter (cm fmt)

Cmにはコードフォーマッターが組み込まれています。一貫したコードスタイルを維持できます。

## 基本的な使い方

```bash
# フォーマット結果を表示
./cm fmt src/main.cm

# ファイルに書き込み
./cm fmt -w src/main.cm

# ディレクトリ内を再帰的にフォーマット
./cm fmt -w src/
```

## フォーマットルール

### 1. ブレーススタイル（K&Rスタイル）

開き括弧は同じ行に配置されます：

```cm
// Before
int main()
{
    if (x > 0)
    {
        return 1;
    }
}

// After
int main() {
    if (x > 0) {
        return 1;
    }
}
```

### 2. インデント（4スペース）

インデントは4スペースで統一されます：

```cm
// Before
int main() {
  int x = 10;
      int y = 20;
}

// After
int main() {
    int x = 10;
    int y = 20;
}
```

### 3. 単一行ブロックの保持

短いブロックは単一行のまま保持されます：

```cm
// 単一行のラムダは保持
arr.map(|x| => x * 2);

// 単一行のif文も保持可能
if (x > 0) { return true; }
```

### 4. 空行の正規化

連続する空行は1行に減らされます：

```cm
// Before
int main() {
    int x = 10;



    return x;
}

// After
int main() {
    int x = 10;

    return x;
}
```

## 使用例

### Before

```cm
int main(){
int x=10;
  int y   =  20;
if(x>y){
return x;
}else{
return y;
}
}
```

### After

```cm
int main() {
    int x = 10;
    int y = 20;
    if (x > y) {
        return x;
    } else {
        return y;
    }
}
```

## CIでの使用

コードスタイルをCIでチェックする場合：

```bash
# フォーマットが必要かチェック（差分があれば失敗）
./cm fmt --check src/
```

## 関連項目

- [Linter](linter.md) - 静的解析ツール

---

**最終更新:** 2026-02-08
