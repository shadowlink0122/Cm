[English](mir.en.html)

# MIR (Mid-level Intermediate Representation) 設計

## 概要

MIR（Mid-level IR）は、HIRとLIRの間に位置する中間表現です。SSA（Static Single Assignment）形式を採用し、制御フローグラフ（CFG）ベースの最適化を可能にします。

## MIRの特徴

### 1. SSA形式

すべての変数は一度だけ代入されます。

```
// HIR
let x: Int = 1;
x = x + 2;
x = x * 3;

// MIR (SSA)
_1 = 1
_2 = _1 + 2
_3 = _2 * 3
```

### 2. 制御フローグラフ (CFG)

```
// ソースコード
if (cond) { a(); } else { b(); }
c();

// MIR CFG
┌─────────┐
│ bb0     │
│ _1=cond │
│ br _1   │─────┐
└────┬────┘     │
     │ true     │ false
     ▼          ▼
┌─────────┐  ┌─────────┐
│ bb1     │  │ bb2     │
│ call a  │  │ call b  │
└────┬────┘  └────┬────┘
     │            │
     └─────┬──────┘
           ▼
     ┌─────────┐
     │ bb3     │
     │ call c  │
     └─────────┘
```

### 3. 明示的なメモリ操作

```
// HIR
let mut x = Point { x: 1, y: 2 };
x.x = 10;

// MIR
_1 = alloc Point           // スタック割り当て
store _1.0, 1              // フィールド0に格納
store _1.1, 2              // フィールド1に格納
store _1.0, 10             // フィールド0を更新
```

## MIRノードの設計

### 基本ブロック

```cpp
struct BasicBlock {
    BlockId id;
    std::vector<Statement> statements;
    Terminator terminator;
};
```

### ステートメント

```cpp
struct Statement {
    enum Kind {
        Assign,      // _1 = rvalue
        StorageLive, // 変数の有効範囲開始
        StorageDead, // 変数の有効範囲終了
    };
    Place place;
    Rvalue rvalue;
};
```

### 終端命令 (Terminator)

```cpp
struct Terminator {
    enum Kind {
        Goto,        // 無条件ジャンプ
        Branch,      // 条件分岐
        Switch,      // 多分岐
        Call,        // 関数呼び出し
        Return,      // 戻り
        Unreachable, // 到達不能
    };
    // 分岐先の基本ブロックID
    std::vector<BlockId> successors;
};
```

### Rvalue (右辺値)

```cpp
struct Rvalue {
    enum Kind {
        Use,           // 変数の使用
        BinaryOp,      // 二項演算
        UnaryOp,       // 単項演算
        Aggregate,     // 構造体/配列の構築
        Ref,           // 参照取得
        Deref,         // 参照解決
    };
};
```

## 最適化パス

| パス | 説明 | 例 |
|------|------|-----|
| **定数伝播** | 定数値を伝播 | `_2 = _1 + 0` → `_2 = _1` |
| **デッドコード除去 (DCE)** | 使用されない計算を削除 | 未使用変数の除去 |
| **インライン展開** | 小さな関数を展開 | `call f()` → 関数本体 |
| **コピー伝播** | 冗長なコピーを除去 | `_2 = _1; use _2` → `use _1` |
| **ループ不変式移動** | ループ外に移動可能な計算を移動 | ループ内の定数計算 |
| **エスケープ解析** | ヒープ→スタック変換 | ローカルで完結するオブジェクト |

## HIR → MIR 変換

### 変換例: if式

```cpp
// HIR
HirIf {
    cond: _cond,
    then_block: { a(); },
    else_block: { b(); },
}

// MIR
bb0:
    _1 = eval(_cond)
    branch _1 -> [true: bb1, false: bb2]

bb1:
    call a()
    goto -> bb3

bb2:
    call b()
    goto -> bb3

bb3:
    ...
```

### 変換例: ループ

```cpp
// HIR
HirLoop {
    body: {
        if !cond { break; }
        stmt;
    }
}

// MIR
bb0:                        // ループヘッダ
    _1 = eval(cond)
    branch _1 -> [true: bb1, false: bb2]

bb1:                        // ループ本体
    eval(stmt)
    goto -> bb0

bb2:                        // ループ出口
    ...
```

## MIRの用途

1. **最適化**: CFGベースの各種最適化
2. **借用検査**: Rust風のライフタイム検査（将来）
3. **コード生成**: LIR/LLVM IRへの変換の入力
4. **デバッグ**: 実行パスの可視化

## 参考資料

- [Rust MIR](https://rustc-dev-guide.rust-lang.org/mir/) - RustコンパイラのMIR設計
- [LLVM SSA](https://llvm.org/docs/LangRef.html) - SSA形式の参考
- "Static Single Assignment Book" - SSAの理論と実践