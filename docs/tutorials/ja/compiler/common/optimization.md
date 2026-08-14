---
title: 最適化
parent: Compiler
---

[English](../../../en/compiler/common/optimization.html)

# MIR最適化パス

**学習目標:** Cmコンパイラに実装されている最適化パスを理解し、効率的なコードを生成する方法を学びます。  
**所要時間:** 20分  
**難易度:** 🟡 中級〜🔴 上級

---

## 概要

Cmコンパイラは中間表現（MIR）レベルで複数の最適化パスを実行し、効率的なコードを生成します。

---

## 最適化レベル

| レベル | オプション | 説明 |
|--------|-----------|------|
| O0 | `-O0` | 最適化なし（デバッグ向け） |
| O1 | `-O1` | 基本最適化 |
| O2 | `-O2` | 標準最適化（推奨） |
| O3 | `-O3` | 積極的最適化 |

```bash
# 最適化レベル2でコンパイル
cm build -O2 main.cm
```

---

## 実装済み最適化パス

### スカラー最適化

#### Constant Folding（定数畳み込み）

コンパイル時に定数式を評価します。

```cm
// 最適化前
int x = 2 * 3 + 4;

// 最適化後（コンパイル時に計算済み）
int x = 10;
```

**アルゴリズム:** 定数オペランドの演算を即座に評価

---

#### SCCP（Sparse Conditional Constant Propagation）

条件分岐を考慮した高度な定数伝播。

```cm
// 最適化前
int x = 10;
if (true) {
    x = 20;
}

// 最適化後（条件が常に真なので定数伝播で x = 20 に畳み込まれる）
int x = 20;
```

**アルゴリズム:** Dataflow解析 + Work-listアルゴリズム

---

### 冗長性除去

#### DCE（Dead Code Elimination）

使用されないコードを除去します。

```cm
// 最適化前
int unused = heavy_calc();  // 結果がどこでも使われない
int x = 1;
return x;

// 最適化後（未使用の計算を除去。heavy_calcに副作用がない場合）
int x = 1;
return x;
```

**アルゴリズム:** 使用-定義チェーンの逆追跡

---

#### DSE（Dead Store Elimination）

上書きされる代入を除去します。

```cm
// 最適化前
int x = 10;  // この代入は読まれる前に上書きされる
x = 20;
return x;

// 最適化後
int x = 20;
return x;
```

---

#### GVN（Global Value Numbering）

共通部分式を検出して再利用します。

```cm
// 最適化前
int a = x * y + 1;
int b = x * y + 2;  // x * y を再計算している

// 最適化後（共通部分式 x * y を1回だけ計算して再利用）
int t = x * y;
int a = t + 1;
int b = t + 2;
```

**アルゴリズム:** 式ハッシュによる等価性検出

---

### ループ最適化

#### LICM（Loop Invariant Code Motion）

ループ内で変化しない計算をループ外に移動します。

```cm
// 最適化前
for (int i = 0; i < n; i++) {
    arr[i] = base * scale + i;  // base * scale はループ内で不変
}

// 最適化後（不変式をループ外へ移動）
int inv = base * scale;
for (int i = 0; i < n; i++) {
    arr[i] = inv + i;
}
```

**アルゴリズム:** ループ検出 + 支配木解析

---

### 制御フロー最適化

#### CFG Simplify（制御フロー簡約化）

- 空ブロックの除去
- 到達不能ブロックの削除
- 無条件分岐の統合

---

### 関数間最適化

#### Inlining（インライン展開）

小規模な関数を呼び出し元に埋め込みます。

```cm
// 元の関数
int add(int a, int b) {
    return a + b;
}

// 呼び出し側
int r = add(x, y);

// インライン化後（呼び出しコストが消え、さらに他の最適化が効きやすくなる）
int r = x + y;
```

**制限:**
- 再帰関数は除外
- 大規模関数は除外（閾値あり）
- 呼び出し回数制限

---

#### Program DCE（プログラムレベルDCE）

未使用の関数やグローバル変数を削除します。

---

## 無限ループ防止機構

最適化パスが収束しない場合の安全機構：

| 機構 | 設定 |
|------|------|
| 最大反復回数 | 10回 |
| 最大パス実行数 | 200回 |
| タイムアウト | 30秒（パスあたり5秒） |
| 循環検出 | ハッシュベース（履歴8個） |

---

## 使用アルゴリズム

| アルゴリズム | パス | 説明 |
|-------------|------|------|
| Work-list | SCCP, DCE | 収束計算 |
| Dominator Tree | LICM | ループ検出 |
| SSA Form | 全般 | 静的単一代入形式 |
| Expression Hashing | GVN | 式の等価性検出 |
| Use-Def Chain | DCE, DSE | 使用-定義解析 |

---

## デバッグ

最適化の動作を確認：

```bash
# 最適化デバッグ出力
cm build --debug-opt main.cm

# MIRダンプ
cm build --dump-mir main.cm
```

---

## 次のステップ

- [LLVMバックエンド](../native/index.html) - LLVMによる追加最適化
- [WASMバックエンド](../wasm/index.html) - WebAssembly向け最適化
---

**最終更新:** 2026-02-08

---

<!-- nav -->
← 前: [設定ファイル（.cmconfig.yml）](config.html) ｜ [目次](../index.html) ｜ 次: [サニタイザ](sanitizer.html) →
