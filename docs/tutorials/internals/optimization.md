---
layout: default
title: 最適化パス
nav_order: 2
parent: コンパイラ内部
---

# 最適化パス詳細

**学習目標:** Cmコンパイラの全最適化パスを理解します。  
**所要時間:** 30分  
**難易度:** 🔴 上級

---

## パス一覧

Cmコンパイラは以下の最適化パスを実装しています。

| パス名 | 略称 | レベル | 効果 |
|--------|------|--------|------|
| 定数畳み込み | CF | 全レベル | コンパイル時計算 |
| デッドコード除去 | DCE | 全レベル | 未使用コード削除 |
| 定数伝播 | CP | -O1以上 | 変数定数化 |
| 共通部分式除去 | CSE | -O1以上 | 重複計算削除 |
| SCCP | SCCP | -O2以上 | 高度な定数解析 |
| ループ不変式移動 | LICM | -O2以上 | ループ外への移動 |
| Goto Chain除去 | GC | 全レベル | 連続ジャンプ統合 |
| 無意味ブロック除去 | BB | 全レベル | 空ブロック削除 |

---

## 定数畳み込み（Constant Folding）

コンパイル時に計算可能な式を定数に置換します。

```cm
// 最適化前
int x = 10 + 20 * 3;

// 最適化後
int x = 70;
```

### アルゴリズム

```
for each instruction in block:
    if all operands are constants:
        result = evaluate(instruction)
        replace with constant(result)
```

### 対応演算

- 算術演算: `+`, `-`, `*`, `/`, `%`
- ビット演算: `&`, `|`, `^`, `<<`, `>>`
- 比較演算: `==`, `!=`, `<`, `>`, `<=`, `>=`
- 単項演算: `-`, `!`, `~`

---

## デッドコード除去（Dead Code Elimination）

使用されない値を定義するコードを削除します。

```cm
// 最適化前
int x = 10;      // デッド（使用されない）
int y = 20;
return y;

// 最適化後
int y = 20;
return y;
```

### アルゴリズム（生存変数解析）

```
1. 戻り値・副作用のある命令を「使用」としてマーク
2. 使用からDef-Useチェーンを辿り、必要な定義をマーク
3. マークされていない定義を削除
```

---

## 定数伝播（Constant Propagation）

変数が定数であることが判明した場合、その使用箇所を定数に置換します。

```cm
// 最適化前
int x = 10;
int y = x + 5;

// 最適化後
int x = 10;   // または削除
int y = 15;   // x を 10 に置換後、畳み込み
```

### Work-listアルゴリズム

```
worklist = all blocks
while worklist is not empty:
    block = worklist.pop()
    for each instruction in block:
        for each operand:
            if operand is constant:
                propagate constant
    if changes:
        add successors to worklist
```

---

## SCCP（Sparse Conditional Constant Propagation）

条件分岐の到達可能性を考慮した高度な定数伝播です。

```cm
// 最適化前
int x = 10;
if (x > 5) {
    return 100;  // 常に到達可能
} else {
    return 200;  // 到達不可能
}

// 最適化後
return 100;
```

### アルゴリズム

1. **SSA-CFGワークリスト** - SSAグラフとCFGを同時に解析
2. **格子値計算** - 各変数に⊤（未定）、定数、⊥（非定数）を割り当て
3. **条件分岐解析** - 条件が定数の場合、到達不可能な分岐を削除

---

## ループ不変式移動（LICM）

ループ内で不変な計算をループ外に移動します。

```cm
// 最適化前
for (int i = 0; i < n; i++) {
    int x = a + b;  // ループ不変
    arr[i] = x * i;
}

// 最適化後
int x = a + b;  // ループ外に移動
for (int i = 0; i < n; i++) {
    arr[i] = x * i;
}
```

### アルゴリズム

1. **ループ検出** - バックエッジからループ構造を特定
2. **不変式判定** - 命令の全オペランドがループ外で定義
3. **移動可能性判定** - 副作用なし、例外なし
4. **プレヘッダへ移動** - ループ入口前に配置

---

## Goto Chain除去

連続したジャンプを単一のジャンプに統合します。

```
// 最適化前
bb0:
    goto bb1
bb1:
    goto bb2
bb2:
    ...

// 最適化後
bb0:
    goto bb2
bb2:
    ...
```

### アルゴリズム

```
for each block:
    if block has only unconditional jump:
        target = block.target
        while target has only unconditional jump:
            target = target.target
        update all predecessors to jump to target
```

---

## 最適化レベル

```bash
./cm compile file.cm           # -O0: 最適化なし
./cm compile -O1 file.cm       # -O1: 基本最適化
./cm compile -O2 file.cm       # -O2: 全最適化
./cm compile --release file.cm # リリースモード（-O2相当）
```

| レベル | 有効なパス |
|--------|-----------|
| -O0 | Goto Chain, BB除去 |
| -O1 | -O0 + CP, CSE, DCE |
| -O2 | -O1 + SCCP, LICM |

---

## 参考実装

- [`src/mir/optimization/`](https://github.com/shadowlink0122/Cm/tree/main/src/mir/optimization) - すべての最適化パスの実装
- [`src/mir/optimization/sccp.cpp`](https://github.com/shadowlink0122/Cm/blob/main/src/mir/optimization/sccp.cpp) - SCCP実装
- [`src/mir/optimization/licm.cpp`](https://github.com/shadowlink0122/Cm/blob/main/src/mir/optimization/licm.cpp) - LICM実装
