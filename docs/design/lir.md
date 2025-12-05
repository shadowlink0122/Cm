# LIR (Low-level Intermediate Representation) 設計

## 概要

LIR（Low-level IR）は、MIRとバックエンド（LLVM、WASM）の間に位置する、ターゲット寄りの中間表現です。レジスタ割り当てと命令選択の準備を行います。

## LIRの特徴

### 1. 仮想レジスタ

無限の仮想レジスタを使用し、後でターゲット固有のレジスタに割り当てます。

```
// MIR
_1 = 10
_2 = 20
_3 = _1 + _2

// LIR
v0 = const 10
v1 = const 20
v2 = add v0, v1
```

### 2. マシンレベルの型

```cpp
enum MachineType {
    I8, I16, I32, I64,   // 整数
    F32, F64,            // 浮動小数点
    Ptr,                 // ポインタ
    Vec128, Vec256,      // SIMD
};
```

### 3. 明示的なスタックフレーム

```
// 関数のスタックフレーム
┌──────────────────┐
│ 戻りアドレス      │
├──────────────────┤
│ 前のフレームポインタ │
├──────────────────┤
│ ローカル変数      │
│   slot0 (8 bytes)│
│   slot1 (4 bytes)│
├──────────────────┤
│ スピル領域       │
└──────────────────┘
```

## LIRノードの設計

### 命令

```cpp
struct LirInst {
    enum OpCode {
        // 算術
        Add, Sub, Mul, Div, Mod,
        // 論理
        And, Or, Xor, Shl, Shr,
        // メモリ
        Load, Store, StackAlloc,
        // 制御
        Jump, Branch, Call, Ret,
        // 比較
        Cmp, Test,
        // 変換
        Cast, Extend, Truncate,
    };
    
    OpCode opcode;
    MachineType type;
    std::vector<Operand> operands;
    std::optional<VReg> dest;
};
```

### オペランド

```cpp
struct Operand {
    enum Kind {
        VReg,      // 仮想レジスタ
        Imm,       // 即値
        StackSlot, // スタックスロット
        Label,     // ラベル（分岐先）
        Global,    // グローバル変数/関数
    };
    Kind kind;
    union {
        uint32_t vreg_id;
        int64_t imm_value;
        uint32_t slot_id;
        const char* label;
    };
};
```

## レジスタ割り当て

### 線形スキャン法

```cpp
struct LiveInterval {
    VReg vreg;
    uint32_t start;  // 開始位置
    uint32_t end;    // 終了位置
    std::optional<PhysReg> assigned;
};

// アルゴリズム概要
1. 各仮想レジスタの生存区間を計算
2. 開始位置でソート
3. 順番にレジスタを割り当て
4. 空きがなければスピル
```

### スピル処理

```
// レジスタが足りない場合
v5 = add v3, v4   // v5を使いたいが空きなし

// スピル後
store [stack_slot_0], v2  // v2をスタックに退避
v5 = add v3, v4           // v2のレジスタをv5に再利用
v2 = load [stack_slot_0]  // 必要時にv2を復元
```

## MIR → LIR 変換

### 変換例

```cpp
// MIR
_1 = Aggregate(Point, [10, 20])
_2 = _1.x + _1.y

// LIR
slot0 = stack_alloc 16          // Point用のスタック確保
store [slot0+0], const 10       // xフィールド
store [slot0+8], const 20       // yフィールド
v0 = load.i64 [slot0+0]         // x取得
v1 = load.i64 [slot0+8]         // y取得
v2 = add.i64 v0, v1             // 加算
```

## ターゲット固有の変換

### LLVM IR出力

```cpp
// LIR
v0 = const.i64 10
v1 = const.i64 20
v2 = add.i64 v0, v1

// LLVM IR
%0 = i64 10
%1 = i64 20
%2 = add i64 %0, %1
```

### WASM出力

```cpp
// LIR
v0 = const.i32 10
v1 = const.i32 20
v2 = add.i32 v0, v1

// WASM (テキスト形式)
(i32.const 10)
(i32.const 20)
(i32.add)
```

## 最適化パス

| パス | 説明 |
|------|------|
| **命令選択** | MIR操作をターゲット命令にマッピング |
| **命令スケジューリング** | パイプラインハザードを回避 |
| **ピープホール最適化** | 小規模なパターンの最適化 |
| **冗長ロード除去** | 連続する同一ロードを削除 |
| **エンディアン変換** | ターゲットエンディアンへの変換 |

## LIRの位置づけ

```
HIR (高レベル、型付き)
 │
 ▼
MIR (SSA形式、CFG、最適化)
 │
 ▼
LIR (仮想レジスタ、マシン命令)
 │
 ├──▶ LLVM IR → ネイティブ (x86/ARM/RISC-V)
 │
 └──▶ WASM → ブラウザ/Node.js
```

## 参考資料

- [LLVM Machine IR](https://llvm.org/docs/MIR/LangRef.html) - LLVMのマシンIR
- [Cranelift IR](https://cranelift.dev/) - 軽量コードジェネレータ
- "Engineering a Compiler" Ch.13 - レジスタ割り当て
