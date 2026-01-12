# ユニオン型スライス設計

## 概要

ユニオン型（`typedef Value = int | float | string`）のスライス `Value[]` をサポートする。

## 現状

- v0.11.0で基盤実装済み（`cm_slice_push_blob`）
- ただし、push時に明示的なタグ付けが必要

## 将来の改善

### 暗黙的タグ付けの導入

```cm
typedef Value = int | float | string;
Value[] values = [];

// 理想的な構文
values.push(42);        // int → 自動でタグ0
values.push(3.14);      // float → 自動でタグ1
values.push("hello");   // string → 自動でタグ2
```

### 必要な実装

1. **型チェッカー**: push引数がユニオンバリアントに一致するか確認
2. **MIR lowering**: 自動タグ付きデータ構築

### メモリレイアウト

```
Union型: tag(4B) + data(最大バリアントサイズ)

スライス U[]:
┌────────────────────────────────────────────┐
│ Union0 (コピー) │ Union1 (コピー) │ ...    │
└────────────────────────────────────────────┘
```

## 優先度

中（v0.12.0以降で検討）
