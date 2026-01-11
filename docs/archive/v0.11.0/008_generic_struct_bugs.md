# 008 ジェネリック構造体バグ分析レポート

## 概要

ジェネリック型`T`が**構造体**の場合に、LLVM IR生成で型不一致エラーが発生する問題を発見。`T`がプリミティブ型（int等）の場合は正常に動作する。

## 確認済みエラー

### エラー1: Store命令の型不一致
```
Stored value type does not match pointer operand type!
  store i32 %1, %Item* %local_16, align 4
 i32
```

**発生箇所**: `T data = node->data;` のような構造体コピー処理

**原因仮説**: ジェネリック関数のLoad/Store処理で、`T`が構造体の場合に型情報が正しく伝播されていない

### エラー2: GEPアサーション（関連問題）
```
Assertion failed: (Ty && "Invalid GetElementPtrInst indices for type!")
```

**発生箇所**: ジェネリック構造体のフィールドアクセス

## 修正済みの問題

| 問題 | 修正内容 | 状態 |
|------|----------|------|
| `T=int` でのGEPエラー | マングリング済み構造体検出 + 二重マングリング防止 | ✅ 解決 |
| ポインタとnull比較のICmpエラー | IntToPtr変換を追加 | ✅ 解決 |

## 未解決の問題

| 問題 | 影響範囲 | 優先度 |
|------|----------|--------|
| `T`が構造体の場合のLoad/Store型不一致 | PriorityQueue等のジェネリックデータ構造 | **高** |
| ネストしたジェネリック構造体のフィールドアクセス | 同上 | **高** |

## 再現テストケース

```cm
struct Item { int value; }
struct Node<T> { T data; }

<T> T get_data(Node<T>* node) {
    return node->data;  // ← ここでエラー
}

int main() {
    Node<Item>* node = ...;
    Item item = get_data(node);  // T=Item で呼び出し
    return 0;
}
```

## 調査が必要な箇所

1. **MIR lowering** (`hir_to_mir.cpp`)
   - ジェネリック型`T`の具体化時の型情報伝播

2. **モノモーフィック化** (`monomorphization_impl.cpp`)
   - `substitute_type_in_type()` でのLoad/Storeオペランド型処理

3. **LLVM codegen** (`mir_to_llvm.cpp`)
   - `convertPlaceToAddress()` でのフィールドアクセス処理
   - `convertOperand()` でのLoad処理

## 推奨される次のステップ

1. **001-007のリファクタリングを先に実施**
   - コードベースの整理により、バグの原因箇所が見えやすくなる可能性

2. **MIR dumping機能を活用**
   - `T=int` と `T=Item` のMIR出力を比較

3. **LLVM IR出力の比較**
   - 正常動作ケースと失敗ケースの差分を分析

---

*作成日: 2026-01-10*
