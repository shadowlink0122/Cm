---
title: 内部アルゴリズム
parent: コンパイラ内部
---

[English](../../en/internals/algorithms.html)

# コンパイラ内部アルゴリズム

**学習目標:** Cmコンパイラで使用されているアルゴリズムを理解します。  
**所要時間:** 20分  
**難易度:** 🔴 上級

---

## 概要

Cmコンパイラはコンパイラ理論の標準的なアルゴリズムを実装しています。

---

## データフロー解析

### Work-listアルゴリズム

収束するまで繰り返し解析を行います。

**使用箇所:** SCCP, 定数伝播, 生存変数解析

```
Input:  CFG (Control Flow Graph)
Output: 各ポイントでのデータフロー情報

worklist = 全基本ブロック
while worklist is not empty:
    block = worklist.pop()
    old_out = out[block]
    in[block] = meet(predecessors(block))
    out[block] = transfer(block, in[block])
    if out[block] != old_out:
        add successors to worklist
```

---

## 制御フロー解析

### Tarjan's SCC（強連結成分分解）

循環依存を検出します。

**使用箇所:** モジュール循環依存検出

```
時間計算量: O(V + E)
空間計算量: O(V)
```

### 支配木（Dominator Tree）

ある地点を必ず通過するか判定します。

**使用箇所:** LICM（ループ検出）、SSA構築

---

## 中間表現

### SSA形式（Static Single Assignment）

各変数への代入を1回に限定した形式。

**利点:**
- Use-Defチェーンの効率化
- 定数伝播の簡易化
- デッドコード検出の容易化

```cm
// 通常のコード
x = 1;
x = 2;
y = x;

// SSA形式
x_1 = 1;
x_2 = 2;
y = x_2;
```

### φ関数（Phi Function）

複数の経路からの値を統合します。

```
if (cond) {
    x_1 = 10;
} else {
    x_2 = 20;
}
// 実行経路に応じた値を選択
x_3 = φ(x_1, x_2); 
```

---

## 最適化アルゴリズム

### Value Numbering

式の等価性をハッシュで判定します。

**使用箇所:** GVN（共通部分式除去）

```cm
// 同じ演算には同じ番号を割り当てる
int a = x + y; // v1 = Add(vx, vy)
int b = x + y; // v1 = Add(vx, vy) -> 再計算不要
```

### ループ解析

ループ構造を特定してLICMを適用します。

1. バックエッジ検出（後退辺）
2. ループヘッダ特定
3. ループ本体の収集
4. 不変式の特定

---

## 型システム

### Monomorphization（単相化）

ジェネリック関数を具象型ごとに生成します。

```cm
// ジェネリック定義
<T> T identity(T x) { return x; }

// 生成されるコード
int identity__int(int x) { return x; }
string identity__string(string x) { return x; }
```

---

## 参考資料

- [最適化パス](optimization.html) - 各パスの詳細
- [Engineering a Compiler](https://www.elsevier.com/books/engineering-a-compiler/cooper/978-0-12-815412-0) - Cooper & Torczon
- [SSA-based Compiler Design](https://link.springer.com/book/10.1007/978-3-030-80515-9) - SSAの詳細

---

<!-- nav -->
← 前: [コンパイラアーキテクチャ](architecture.html) ｜ [目次](index.html) ｜ 次: [最適化パス詳細](optimization.html) →
