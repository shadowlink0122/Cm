---
title: 最適化パス
parent: コンパイラ内部
---

[English](../../en/internals/optimization.html)

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
int x = 2 + 3 * 4;
bool b = !true;

// 最適化後
int x = 14;
bool b = false;
```

### 対応演算

- 算術演算: `+`, `-`, `*`, `/`, `%`
- ビット演算: `&`, `|`, `^`, `<<`, `>>`
- 比較演算: `==`, `!=`, `<`, `>`, `<=`, `>=`
- 単項演算: `-`, `!`, `~`

---

## デッドコード除去（Dead Code Elimination）

使用されない値を定義するコード（副作用のないもの）を削除します。

```cm
// 最適化前
int main() {
    int x = 10;
    int y = 20; // y はこの後使われない
    return x;
}

// 最適化後
int main() {
    int x = 10;
    // int y = 20; は削除される
    return x;
}
```

---

## 定数伝播（Constant Propagation）

変数が定数であることが判明した場合、その使用箇所を定数に置換します。

```cm
// 最適化前
int x = 10;
int y = x + 5;

// 最適化後 (定数伝播 + 定数畳み込み)
int x = 10;
int y = 15; // x が 10 に置き換えられ、10 + 5 が計算される
```

---

## SCCP（Sparse Conditional Constant Propagation）

条件分岐の到達可能性を考慮した高度な定数伝播です。

```cm
// 最適化前
bool cond = true;
if (cond) {
    return 100;
} else {
    return 200; // この分岐は到達不可能
}

// 最適化後
return 100; // 条件分岐が削除される
```

---

## ループ不変式移動（LICM）

ループ内で不変な計算をループ外に移動します。

```cm
// 最適化前
for (int i = 0; i < n; i++) {
    int val = x * y; // xとyがループ内で不変なら
    arr[i] = val + i;
}

// 最適化後
int val = x * y; // ループの前（プレヘッダ）に移動
for (int i = 0; i < n; i++) {
    arr[i] = val + i;
}
```

---

## 共通部分式除去（CSE）

同じ計算を繰り返している箇所を、一度の計算結果の再利用に置き換えます。

```cm
// 最適化前
int a = x * y + z;
int b = x * y + w;

// 最適化後
int tmp = x * y; // 共通部分式を一時変数に
int a = tmp + z;
int b = tmp + w;
```

---

## Goto Chain除去

連続したジャンプ命令を単一のジャンプに統合し、不要なブロックをスキップします。

```
// 最適化前 (MIR)
bb0:
    goto bb1
bb1:
    goto bb2
bb2:
    return

// 最適化後
bb0:
    goto bb2 // bb1 をスキップして直接 bb2 へ
bb2:
    return
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
| -O1 | -O0 + CP, CSE, DCE, CF |
| -O2 | -O1 + SCCP, LICM |

---

## 参考実装

- [`src/mir/optimization/`](https://github.com/shadowlink0122/Cm/tree/main/src/mir/optimization) - すべての最適化パスの実装
- [`src/mir/optimization/sccp.cpp`](https://github.com/shadowlink0122/Cm/blob/main/src/mir/optimization/sccp.cpp) - SCCP実装
- [`src/mir/optimization/licm.cpp`](https://github.com/shadowlink0122/Cm/blob/main/src/mir/optimization/licm.cpp) - LICM実装