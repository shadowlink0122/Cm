# 変数エイリアシング最適化設計

## 概要
`move`後の変数が元の変数と同じメモリアドレスを参照するよう最適化し、真のゼロコストムーブを実現。

## 現状の問題

```cm
int x = 42;
int y = move x;
// 現在: y は x とは別のスタックスロット
// addr(x) = 0x7fff0010
// addr(y) = 0x7fff0030 ← 異なる
```

## 目標

```cm
int x = 42;
int y = move x;
// 目標: y は x と同じアドレス
// addr(x) = addr(y) = 0x7fff0010
```

---

## 実装方針

### Phase 1: MIR エイリアス追跡

```cpp
// MIRで変数エイリアスを追跡
struct LocalInfo {
    LocalId id;
    std::optional<LocalId> alias_of;  // エイリアス元
};

// move x の場合、y を x のエイリアスとして記録
// y.alias_of = x.id
```

### Phase 2: LLVM コード生成

```cpp
// エイリアス変数は同じ alloca を使用
if (local.alias_of.has_value()) {
    // 新しい alloca は作らず、元の変数の alloca を使用
    llvm_locals_[local.id] = llvm_locals_[local.alias_of.value()];
} else {
    llvm_locals_[local.id] = builder.CreateAlloca(...);
}
```

### Phase 3: 最適化パス

- **Dead Store Elimination**: エイリアス後の元変数への書き込みを削除
- **SSA変換**: エイリアスをφノードとして扱う

---

## 対応パターン

### 1. 単純なmove
```cm
int x = 42;
int y = move x;  // y は x のエイリアス
```

### 2. 関数引数へのmove
```cm
void consume(int val) { ... }
int x = 42;
consume(move x);  // x を直接渡す（コピーなし）
```

### 3. 戻り値のmove
```cm
int create() {
    int x = 42;
    return x;  // RVO適用
}
```

---

## 制約事項

### エイリアス不可のケース
1. **制御フロー分岐後のmove**
   ```cm
   int x = 42;
   if (cond) {
       int y = move x;  // 条件付きエイリアスは複雑
   }
   ```

2. **ループ内のmove**
   ```cm
   for (i in 0..10) {
       int y = move x;  // 最初のイテレーションのみ有効
   }
   ```

---

## 検証テスト

```cm
int main() {
    int x = 42;
    println("x addr: {&x:x}");
    int y = move x;
    println("y addr: {&y:x}");
    // 期待: 同じアドレス
    return 0;
}
```

---

## 優先度
🟠 中 - ゼロコスト保証のため重要だが、現在の実装でも安全性は確保
