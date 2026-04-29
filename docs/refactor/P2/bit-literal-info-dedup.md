# BitLiteralInfoの共通化

**優先度**: 低  
**影響範囲**: コード整理  
**対象ファイル**: 複数

---

## 問題

`BitLiteralInfo` が複数箇所で重複定義されている。

---

## 現状の定義

### 1. token.hpp

```cpp
// src/frontend/lexer/token.hpp:183
struct BitLiteralInfo {
    int width;
    char base;
    std::string original;
    
    BitLiteralInfo(int w, char b, std::string orig)
        : width(w), base(b), original(std::move(orig)) {}
};
```

### 2. hir/nodes.hpp

```cpp
// src/hir/nodes.hpp
struct BitLiteralInfo {
    int width;
    char base;
    std::string original;
};
```

### 3. mir/nodes.hpp

```cpp
// src/mir/nodes.hpp
struct BitLiteralInfo {
    int width;
    char base;
    std::string original;
};
```

---

## 修正案

### 共通定義への移動

```cpp
// src/common/bit_literal.hpp

#pragma once
#include <string>

namespace cm {

struct BitLiteralInfo {
    int width;             // ビット幅 (例: 8)
    char base;             // ベース文字 ('d', 'b', 'h')
    std::string original;  // 元のリテラル文字列
    
    BitLiteralInfo() = default;
    BitLiteralInfo(int w, char b, std::string orig)
        : width(w), base(b), original(std::move(orig)) {}
    
    // ヘルパーメソッド
    bool is_binary() const { return base == 'b'; }
    bool is_hex() const { return base == 'h'; }
    bool is_decimal() const { return base == 'd'; }
};

} // namespace cm
```

### 各ファイルでの使用

```cpp
// token.hpp
#include "common/bit_literal.hpp"
using BitLiteralInfo = cm::BitLiteralInfo;

// hir/nodes.hpp
#include "common/bit_literal.hpp"
// cm::BitLiteralInfo を直接使用

// mir/nodes.hpp
#include "common/bit_literal.hpp"
// cm::BitLiteralInfo を直接使用
```

---

## 影響

- コード重複の削減
- 一貫した動作
- 将来の機能追加（メソッド等）の容易化

