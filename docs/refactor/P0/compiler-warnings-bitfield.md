# コンパイラ警告の修正

**優先度**: 高  
**影響範囲**: ビルド品質  
**対象ファイル**: `src/frontend/ast/types.hpp`

---

## 問題

C++20拡張としてビットフィールドのデフォルト値初期化が警告される。

```cpp
// src/frontend/ast/types.hpp:117-119
bool is_const : 1 = false;
bool is_volatile : 1 = false;
bool is_mutable : 1 = false;
```

**警告メッセージ**:
```
warning: default member initializer for bit-field is a C++20 extension [-Wc++20-extensions]
```

---

## 修正案

### 方法A: コンストラクタでの初期化

```cpp
struct Type {
    bool is_const : 1;
    bool is_volatile : 1;
    bool is_mutable : 1;
    
    Type() : is_const(false), is_volatile(false), is_mutable(false) {}
};
```

### 方法B: C++20への移行

CMakeLists.txtで `-std=c++20` を指定。

---

## 影響

- ビルド時の警告除去
- C++17環境との互換性維持

