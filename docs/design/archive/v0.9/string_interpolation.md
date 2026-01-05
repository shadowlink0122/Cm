# 文字列埋め込み（String Interpolation）の正しい実装

## 現在の問題点

1. **println専用処理** - 文字列埋め込みがprintln/print関数でのみ動作
2. **分離された処理** - フォーマット文字列と引数が分離されている
3. **汎用性の欠如** - 任意の文字列リテラルで使用できない

## 正しい設計

### 原則
- **文字列埋め込みは言語の基本機能**
- すべての文字列リテラルで`{}`による値の埋め込みが可能
- println以外でも動作する

### 実装レベル

```
レクサー → パーサー → AST → HIR → MIR → コードジェン
           ↑
       ここで処理すべき
```

## 実装方法

### 1. パーサーでの文字列リテラル処理

```cpp
// parser_expr.cpp
ExprPtr parse_string_literal() {
    std::string raw = current().get_string();

    // 埋め込み式を検出
    if (contains_interpolation(raw)) {
        return parse_interpolated_string(raw);
    }

    return ast::make_string_literal(raw);
}

ExprPtr parse_interpolated_string(const std::string& raw) {
    std::vector<ExprPtr> parts;
    size_t pos = 0;

    while (pos < raw.size()) {
        size_t brace_start = raw.find('{', pos);

        if (brace_start == std::string::npos) {
            // 残りの文字列
            parts.push_back(make_string_literal(raw.substr(pos)));
            break;
        }

        // {の前の文字列
        if (brace_start > pos) {
            parts.push_back(make_string_literal(raw.substr(pos, brace_start - pos)));
        }

        // エスケープ {{ をチェック
        if (brace_start + 1 < raw.size() && raw[brace_start + 1] == '{') {
            parts.push_back(make_string_literal("{"));
            pos = brace_start + 2;
            continue;
        }

        // 埋め込み式を解析
        size_t brace_end = raw.find('}', brace_start);
        if (brace_end != std::string::npos) {
            std::string expr_str = raw.substr(brace_start + 1, brace_end - brace_start - 1);

            // フォーマット指定子をチェック
            size_t colon = expr_str.find(':');
            if (colon != std::string::npos) {
                std::string var_name = expr_str.substr(0, colon);
                std::string format_spec = expr_str.substr(colon + 1);

                // フォーマット付き埋め込み式
                parts.push_back(make_format_expr(var_name, format_spec));
            } else {
                // 単純な埋め込み式
                parts.push_back(make_to_string_expr(expr_str));
            }

            pos = brace_end + 1;
        }
    }

    // 文字列連結式として返す
    return make_string_concat(parts);
}
```

### 2. AST表現

```cpp
// 新しいAST ノード
struct StringInterpolation : ExprKind {
    std::vector<ExprPtr> parts;  // 文字列と埋め込み式の配列
};

struct FormatExpr : ExprKind {
    ExprPtr value;           // フォーマット対象の値
    std::string format_spec; // フォーマット指定子
};
```

### 3. HIR/MIRでの変換

```cpp
// 例: "Hello {name}, you are {age} years old"
//
// AST:
//   StringInterpolation [
//     "Hello ",
//     ToStringExpr(name),
//     ", you are ",
//     ToStringExpr(age),
//     " years old"
//   ]
//
// HIR/MIR:
//   _tmp1 = toString(name)
//   _tmp2 = concat("Hello ", _tmp1)
//   _tmp3 = concat(_tmp2, ", you are ")
//   _tmp4 = toString(age)
//   _tmp5 = concat(_tmp3, _tmp4)
//   _tmp6 = concat(_tmp5, " years old")
//   result = _tmp6
```

### 4. println での使用

```cpp
// println("Hello {name}")
//
// 変換後:
//   _tmp1 = toString(name)
//   _tmp2 = concat("Hello ", _tmp1)
//   println(_tmp2)  // 単一の文字列引数
```

## 利点

1. **一貫性** - すべての文字列リテラルで同じ動作
2. **柔軟性** - println以外でも使用可能
3. **シンプルさ** - コードジェンが単純化

## 使用例

```cm
// 基本的な埋め込み
string greeting = "Hello {name}!";

// 計算式の埋め込み
string result = "The sum is {a + b}";

// フォーマット指定
string hex = "Value in hex: {value:x}";

// printlnでの使用
println("Debug: x={x}, y={y}");

// 変数への代入
string message = "Error at line {line}: {error_msg}";

// 関数の引数
log_message("User {user_id} logged in at {timestamp}");

// 配列要素
string[] messages = [
    "First: {first}",
    "Second: {second}"
];
```

## 実装手順

1. **Phase 1: 基本的な文字列埋め込み**
   - パーサーで`{変数}`を検出
   - 文字列連結に変換
   - printlnが単一文字列を受け取る

2. **Phase 2: 式の埋め込み**
   - `{a + b}`などの式をサポート
   - 適切なtoString変換

3. **Phase 3: フォーマット指定子**
   - `{value:x}`, `{pi:.2}`などのサポート
   - フォーマット関数の実装

## テストケース

```cm
// test_string_interpolation.cm
int main() {
    string name = "Alice";
    int age = 25;

    // 基本的な埋め込み
    string s1 = "Hello {name}";
    assert(s1 == "Hello Alice");

    // 複数の埋め込み
    string s2 = "{name} is {age} years old";
    assert(s2 == "Alice is 25 years old");

    // println での使用
    println("Name: {name}, Age: {age}");
    // 出力: Name: Alice, Age: 25

    // エスケープ
    string s3 = "Literal braces: {{ and }}";
    assert(s3 == "Literal braces: { and }");

    return 0;
}
```