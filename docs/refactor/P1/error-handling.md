# 例外処理の統一

**優先度**: 中  
**影響範囲**: エラーハンドリング  
**対象ファイル**: 複数  
**必要テスト**: エラーハンドリングのユニットテスト（エラー伝播、エラー集約）

---

## 問題

エラー処理のパターンが統一されていない。

---

## 現状

| パターン | 件数 | 例 |
|---------|------|-----|
| `catch` ブロック | 40 | 各種例外処理 |
| `std::exit(1)` | 5+ | 強制終了 |
| `error()` 関数 | 多数 | パーサー等 |
| 直接 `std::cerr` | 多数 | SVコード生成等 |

---

## 問題のあるパターン

### 1. 強制終了

```cpp
// main.cpp
std::exit(1);  // スタックアンワインドなし
```

### 2. 統一されていないエラー型

```cpp
// パーサー
error("Expected identifier");

// 型チェック
error(span, "Type mismatch");

// コード生成
std::cerr << "error[SV002]: ...\n";
has_error = true;
```

---

## 修正案

### 1. 統一エラー型の導入

```cpp
namespace cm {

enum class ErrorKind {
    Parse,
    Type,
    Codegen,
    IO
};

struct Error {
    ErrorKind kind;
    std::string code;     // "E001", "SV002" 等
    std::string message;
    Span span;
    
    static Error parse(const std::string& msg, Span s);
    static Error type(const std::string& msg, Span s);
    static Error codegen(const std::string& code, const std::string& msg);
};

template<typename T>
using Result = std::variant<T, Error>;

} // namespace cm
```

### 2. エラー集約

```cpp
class ErrorCollector {
    std::vector<Error> errors_;
    std::vector<Error> warnings_;
public:
    void add(Error e);
    bool has_errors() const;
    void report_all(std::ostream& os) const;
};
```

### 3. 伝播パターン

```cpp
Result<ast::ExprPtr> parse_expr() {
    auto lhs = parse_primary();
    if (auto* err = std::get_if<Error>(&lhs)) {
        return *err;  // エラー伝播
    }
    // 成功パス
    return std::get<ast::ExprPtr>(lhs);
}
```

---

## 影響

- 一貫したエラーメッセージ
- 複数エラーの集約表示
- リソースリークの防止（強制終了の削減）

