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

### 2.1 条件付きコンパイルブロックのインデント（v0.16.0）

`#ifdef` / `#ifndef` / `#else` 〜 `#end` のブロック内容は1段インデントされます。ディレクティブ自体は外側のインデントレベルに置かれ、ネストやブレースとの組み合わせにも対応します：

```cm
// Before
#ifdef TEST
#[input] posedge clk;
const uint N = 1;
#end

// After
#ifdef TEST
    #[input] posedge clk;
    const uint N = 1;
#end
```

### 2.2 継続行のインデント（v0.16.0）

長い式を折り返した継続行は、文の開始行より1段深くインデントされます。1回目の折り返しで+1段、2回目以降も同じ深さに揃います：

```cm
// Before
uint n1 = (a & 1) + ((a >> 1) & 1)
+ ((a >> 2) & 1)
+ ((a >> 3) & 1);

// After
uint n1 = (a & 1) + ((a >> 1) & 1)
    + ((a >> 2) & 1)
    + ((a >> 3) & 1);
```

開き括弧を閉じずに折り返した継続行は、括弧の深さに従います：

```cm
out = (q0 | (q1 << 1) | (q2 << 2)
    | (q3 << 3)
    | (1 << 8)) as ushort;
```

### 2.3 行末コメントの位置（v0.16.0）

行末コメントの位置は手動調整が尊重されます。コードとコメントの間隔が2スペース未満の場合のみ、2スペースへ広げられます：

```cm
tmds_out = 852;   // 手動で揃えたコメントはそのまま
tmds_out = 171;   // （3スペースの位置調整が保持される）
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

---

<!-- nav -->← 前: [Linter (cm lint)](linter.html) ｜ [目次](../index.html) ｜ 次: [MIR最適化パス](optimization.html) →
