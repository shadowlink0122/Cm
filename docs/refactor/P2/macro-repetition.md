# マクロシステムの完全実装

**優先度**: 低  
**影響範囲**: 言語機能  
**対象ファイル**: `src/macro/`  
**必要テスト**: `tests/macro/` ディレクトリに繰り返しパターンのテスト追加

---

## 現状

マクロシステムは部分的に実装されているが、繰り返しパターンが未実装。

```cpp
// src/macro/expander.cpp
// TODO: 繰り返しの展開実装

// src/macro/matcher.cpp
// TODO: iter_stateのバインディングをmatchesに追加
// TODO: matchesをstateのバインディングに追加
// TODO: より詳細な実装が必要
```

---

## 未実装機能

### 1. 繰り返しパターン

```cm
macro_rules! vec {
    ($($elem:expr),*) => {
        // $elem の繰り返し展開が未実装
    }
}
```

### 2. 繰り返し区切り

```cm
macro_rules! list {
    ($($item:ident),+ $(,)?) => {
        // カンマ区切り、オプショナル末尾カンマ
    }
}
```

### 3. ネストした繰り返し

```cm
macro_rules! nested {
    ($( $x:expr => { $($y:expr),* } )*) => {
        // ネストした繰り返し
    }
}
```

---

## 修正案

### Rustのmacro_rules!に準拠

```cpp
// expander.cpp

void MacroExpander::expand_repetition(
    const RepetitionPattern& rep,
    const MatchBindings& bindings,
    std::vector<Token>& output
) {
    // バインディングから繰り返し回数を決定
    size_t count = get_repetition_count(rep.pattern, bindings);
    
    // 各イテレーションで展開
    for (size_t i = 0; i < count; ++i) {
        auto iter_bindings = extract_iteration(bindings, i);
        expand_pattern(rep.pattern, iter_bindings, output);
        
        // 区切りトークンの挿入
        if (i + 1 < count && rep.separator) {
            output.push_back(*rep.separator);
        }
    }
}
```

---

## 影響

- DSL構築の強化
- コード生成マクロの実現
- ボイラープレート削減

