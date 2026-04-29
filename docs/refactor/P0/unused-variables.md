# 未使用変数の削除

**優先度**: 高  
**影響範囲**: コード品質  
**対象ファイル**: 複数

---

## 問題

コンパイラ警告が出る未使用変数が存在する。

### 1. parser_expr.cpp:1121

```cpp
auto ident_pos = pos_;  // 未使用
```

### 2. sv/codegen.cpp:566

```cpp
bool has_non_edge_args = false;  // 設定されるが使用されない
```

### 3. sv/codegen.cpp:1984

```cpp
bool is_param = false;  // 設定されるが使用されない
```

---

## 修正案

### parser_expr.cpp

`ident_pos` は非SVプラットフォームの構造体リテラル解析で使用されていない。削除可能。

### sv/codegen.cpp

`has_non_edge_args` と `is_param` は将来の機能のために設定されている可能性がある。

- 使用予定がなければ削除
- 使用予定があれば `[[maybe_unused]]` を追加
- または実際のロジックを追加

---

## 影響

- コンパイラ警告の除去
- コードの可読性向上

