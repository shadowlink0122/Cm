# const強化設計（コンパイル時定数）

**日付**: 2026-01-28
**バージョン**: v0.12.0

---

## 概要

`constexpr`キーワードを追加する代わりに、既存の`const`をRust/Go方式に強化し、
**コンパイル時定数としての機能**を持たせる。

---

## 設計決定

### Before（現在の動作）

```cm
const int MAX = 100;        // OK
const int SIZE = MAX * 2;   // OK（計算はできるが...）
int[MAX] buffer;            // ❌ エラー：配列サイズはリテラルのみ
```

### After（強化後）

```cm
const int MAX = 100;        // OK: コンパイル時定数
const int SIZE = MAX * 2;   // OK: コンパイル時に200と計算
int[SIZE] buffer;           // ✅ OK: int[200] buffer

const int INPUT = read();   // ❌ エラー: 実行時関数は使用不可
```

---

## 仕様

### 1. const変数の初期化制約

const変数の初期化子は**コンパイル時に評価可能**でなければならない：

```cm
// ✅ 許可される初期化
const int A = 100;                    // リテラル
const int B = A + 50;                 // const変数の演算
const int C = B * 2 + A;              // 複合式
const double PI = 3.14159;            // 浮動小数点リテラル
const string MSG = "Hello";           // 文字列リテラル
const bool FLAG = A > 50;             // 比較式

// ❌ 禁止される初期化
const int X = get_input();            // 関数呼び出し
const int Y = rand();                 // 実行時関数
const int Z = *ptr;                   // ポインタデリファレンス
const int W = arr[i];                 // 変数インデックス
```

### 2. 配列サイズでの使用

```cm
const int BUFFER_SIZE = 1024;
const int HEADER_SIZE = 16;
const int PAYLOAD_SIZE = BUFFER_SIZE - HEADER_SIZE;

char[BUFFER_SIZE] buffer;       // ✅ char[1024]
char[PAYLOAD_SIZE] payload;     // ✅ char[1008]
```

### 3. const関数（オプション：将来拡張）

コンパイル時に評価可能な関数を定義可能にする（将来拡張）：

```cm
const int factorial(int n) {
    return n <= 1 ? 1 : n * factorial(n - 1);
}

const int FIB_10 = factorial(10);  // コンパイル時に3628800
int[factorial(5)] cache;           // int[120]
```

---

## 実装方針

### Phase 1: 基本実装

1. **型チェッカー**: const初期化子がコンパイル時評価可能か検証
2. **MIR**: const値をコンパイル時に計算
3. **配列サイズ**: const変数を配列サイズとして使用可能に

### Phase 2: 演算対応

1. **算術演算**: `+`, `-`, `*`, `/`, `%`
2. **比較演算**: `==`, `!=`, `<`, `>`, `<=`, `>=`
3. **論理演算**: `&&`, `||`, `!`
4. **ビット演算**: `&`, `|`, `^`, `~`, `<<`, `>>`

### Phase 3: 関数対応（オプション）

1. **const関数**の定義
2. **再帰的const関数**のサポート
3. **条件分岐**（三項演算子）の評価

---

## 破壊的変更

### 影響を受けるコード

```cm
// Before: 動作していた
const int config = load_config();  // 実行時関数OK

// After: エラーになる
const int config = load_config();  // ❌ エラー
```

### 移行方法

実行時に決まる値には`const`を使わず、通常の変数（変更しない）を使用：

```cm
// Option 1: 通常変数（推奨）
int config = load_config();  // 変更しなければ実質const

// Option 2: 将来的にreadonly導入（検討中）
readonly int config = load_config();  // 実行時const
```

---

## テスト計画

1. `tests/test_programs/const/compile_time_const.cm` - 基本動作
2. `tests/test_programs/const/const_array_size.cm` - 配列サイズ
3. `tests/test_programs/const/const_expressions.cm` - 複合式
4. `tests/test_programs/errors/const_runtime_error.cm` - エラーケース

---

## 参考

- **Rust**: `const` = 必ずコンパイル時
- **Go**: `const` = 必ずコンパイル時  
- **C++**: `constexpr` = コンパイル時、`const` = 実行時もOK
- **Zig**: `comptime` = コンパイル時

Cmは**Rust/Go方式**を採用し、シンプルさを優先する。
