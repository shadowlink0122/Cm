# ライフタイムと借用チェック強化設計

## 概要
ダングリングポインタを防ぐため、コンパイル時にライフタイム解析を実装する。

## 現状の問題

### 問題1: ローカル変数への参照を返す
```cm
int* get_ptr() {
    int x = 42;
    return &x;  // 危険！xは関数終了で消滅
}
// → *p はゴミ値
```

### 問題2: スコープ外への参照漏れ
```cm
int* p;
{
    int x = 42;
    p = &x;  // xはスコープ終了で消滅
}
*p = 10;  // ダングリングポインタ
```

### 問題3: 借用が元変数より長生き
```cm
int* outer;
{
    int x = 42;
    outer = &x;  // xより長く生きる参照
}
```

---

## 実装方針

### Phase 1: スコープベースのライフタイム追跡

各変数にスコープレベル（深さ）を割り当てる：

```cpp
struct Symbol {
    TypePtr type;
    bool is_const;
    bool is_moved;
    int borrow_count;
    int scope_level;      // スコープの深さ（0=グローバル, 1=main, 2=内側ブロック...）
};
```

### Phase 2: 借用時のライフタイムチェック

`&x`（AddrOf演算子）処理時：
1. 対象変数のスコープレベルを取得
2. 結果のポインタにライフタイム情報を付加

### Phase 3: 代入時のライフタイム検証

ポインタ代入`p = &x`時：
1. `p`のスコープレベルと`x`のスコープレベルを比較
2. `p`が`x`より長生きする場合（p.level < x.level）エラー

```
error: Cannot store reference to 'x' in 'p': 
       'x' (scope level 2) may be dropped while 'p' (scope level 1) is still alive
```

### Phase 4: 関数戻り値のライフタイムチェック

`return &x`時：
1. `x`がローカル変数かチェック
2. ローカル変数への参照を返そうとした場合エラー

```
error: Cannot return reference to local variable 'x':
       'x' will be dropped when function returns
```

---

## 実装詳細

### 1. Scopeクラス拡張 (`scope.hpp`)

```cpp
class Scope {
    int level_;  // スコープレベル
    
    // 変数登録時にレベルも記録
    void insert(const std::string& name, Symbol sym) {
        sym.scope_level = level_;
        symbols_[name] = sym;
    }
    
    // スコープレベル取得
    int get_scope_level(const std::string& name) const;
};

class ScopeStack {
    int current_level_ = 0;
    
    void push() { current_level_++; scopes_.push_back(Scope(current_level_)); }
    void pop() { current_level_--; scopes_.pop_back(); }
    int current_level() const { return current_level_; }
};
```

### 2. 型チェッカー拡張 (`expr.cpp`)

```cpp
// AddrOf演算子: &x
case ast::UnaryOp::AddrOf: {
    int var_level = get_variable_scope_level(var_name);
    // ポインタ型にライフタイム情報を付加（将来拡張用）
    result_type->source_scope_level = var_level;
    break;
}

// 代入演算子チェック
if (is_pointer_assignment) {
    int lhs_level = get_variable_scope_level(lhs_name);
    int rhs_source_level = rhs_type->source_scope_level;
    
    if (lhs_level < rhs_source_level) {
        error("Cannot store reference: lifetime mismatch");
    }
}

// return文チェック
if (returning_pointer && is_local_variable_reference) {
    error("Cannot return reference to local variable");
}
```

---

## エラーメッセージ例

```
error: Cannot return reference to local variable 'x'
   --> test.cm:3:12
   |
 2 |     int x = 42;
 3 |     return &x;
   |            ^^ reference to local variable
   |
   = note: 'x' will be dropped when function returns
```

```
error: Reference to 'x' would outlive 'x'
   --> test.cm:5:9
   |
 3 | {
 4 |     int x = 42;
 5 |     p = &x;
   |         ^^ 'x' is defined in inner scope
 6 | }
   | - 'x' dropped here
   |
   = note: 'p' (scope 1) lives longer than 'x' (scope 2)
```

---

## テストケース

### エラーテスト（コンパイルエラー期待）
- `errors/dangling_return.cm` - ローカル変数への参照を返す
- `errors/dangling_scope.cm` - スコープ外への参照漏れ

### 正常テスト
- 同一スコープ内での借用
- グローバル変数への借用
- 関数引数としての借用

---

## 実装順序

1. [x] `scope.hpp`: `scope_level`フィールド追加
2. [ ] `scope.hpp`: `ScopeStack`にレベル管理追加
3. [ ] `expr.cpp`: return文でのローカル参照検出
4. [ ] `expr.cpp`: 代入時のスコープレベル比較
5. [ ] エラーテスト追加
6. [ ] 回帰テスト確認
