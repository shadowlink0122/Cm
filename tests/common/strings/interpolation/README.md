# Const変数の文字列補間テスト

## 現在の状態

### ✅ 動作するケース
- ローカルconst変数の補間: `{local_var}` → 正しく展開される

### ❌ 動作しないケース
- グローバル(export) const変数の補間: `{GLOBAL_VAR}` → `{}` になる

## テストファイル

1. **global_const.cm** - グローバルconst変数のテスト（失敗）
2. **mixed_const.cm** - グローバルとローカルの混在テスト（部分的に失敗）

## 修正が必要な箇所

グローバルconst変数の解決がMIR loweringで正しく行われていない。
`src/mir/lowering/expr_lowering_impl.cpp` の文字列補間処理で、
グローバルスコープのconst変数を解決する必要がある。

## 期待される動作

```cm
export const int VERSION = 1;
string msg = "Version: {VERSION}";
// msg == "Version: 1" になるべき
```
