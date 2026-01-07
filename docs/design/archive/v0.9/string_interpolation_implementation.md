[English](string_interpolation_implementation.en.html)

# 文字列埋め込み実装計画

## 問題の本質

現在の実装では：
1. 文字列埋め込みが`println`/`print`関数**専用**の機能になっている
2. フォーマット文字列と引数が**分離**されて処理されている
3. コードジェンで言語ごとに**複雑な処理**が必要

## 正しい設計

### 基本原則
- **文字列埋め込みは言語の基本機能**
- すべての文字列リテラルで使用可能
- 構文レベルで正しく処理

### 実装アーキテクチャ

```
パーサー → HIR変換 → MIR変換 → コードジェン
         ↑
    ここで処理すべき
```

## 具体的な実装手順

### Step 1: HIR Lowering の修正

`src/hir/hir_lowering.cpp` で文字列リテラルを処理：

```cpp
HirExprPtr lower_string_literal(const ast::LiteralExpr& lit) {
    std::string str = std::get<std::string>(lit.value);

    // 文字列埋め込みをチェック
    if (StringInterpolationProcessor::hasInterpolation(str)) {
        // 埋め込み文字列を文字列連結式に変換
        return StringInterpolationProcessor::createInterpolatedStringExpr(
            str,
            [this](const std::string& var_name) {
                return resolve_variable(var_name);
            }
        );
    }

    // 単純な文字列リテラル
    return make_literal(str);
}
```

### Step 2: MIR Lowering の修正

現在の`println`専用処理を削除し、すべての文字列が既に処理済みになるように：

```cpp
// 現在（mir_lowering.hpp:698-777）の特別処理を削除
// printlnは単一の文字列引数を受け取るだけに

if (call.func_name == "println" || call.func_name == "print") {
    // 引数をそのまま処理（文字列埋め込みは既に解決済み）
    for (const auto& arg : call.args) {
        LocalId arg_local = lower_expr(ctx, *arg);
        args.push_back(MirOperand::copy(MirPlace(arg_local)));
    }
}
```

### Step 3: コードジェンの簡略化

各言語のコードジェンから複雑なフォーマット処理を削除：

#### Rust (`rust_codegen.hpp`)
```cpp
// 削除すべきコード（行854-911）
// フォーマット文字列の特別処理は不要に

// 新しいコード
if (func_name == "println!" || func_name == "print!") {
    // 単純に引数を出力
    call = func_name + "(";
    for (const auto& arg : data.args) {
        call += operand_to_rust(*arg);
    }
    call += ")";
}
```

#### C++ (`cpp_codegen.hpp`)
同様に簡略化

## 実装の利点

### 1. 一貫性
- すべての文字列リテラルで同じ動作
- `string s = "Hello {name}"` が正しく動作

### 2. シンプルさ
- コードジェンが大幅に簡略化
- 言語ごとの特別処理が不要

### 3. 拡張性
- 新しいフォーマット指定子の追加が容易
- 計算式の埋め込み（`{a + b}`）も将来対応可能

## 実装例

### 入力コード
```cm
string name = "Alice";
int age = 25;

// どこでも文字列埋め込みが使える
string greeting = "Hello, {name}!";
string info = "Age: {age:x}";  // フォーマット指定子付き

println(greeting);  // 単一の文字列引数
println("Direct: {name}");  // 直接埋め込みも可能
```

### HIR変換後
```
// greeting = "Hello, {name}!"
greeting = concat("Hello, ", toString(name), "!")

// info = "Age: {age:x}"
info = concat("Age: ", formatHex(age))

// println(greeting)
println(greeting)  // 既に処理済みの文字列

// println("Direct: {name}")
_tmp = concat("Direct: ", toString(name))
println(_tmp)
```

### MIR生成
```
// 文字列連結と変換が明示的に
_1 = "Hello, "
_2 = toString(name)
_3 = concat(_1, _2)
_4 = "!"
greeting = concat(_3, _4)

// printlnは単純に
println(greeting)
```

## テスト計画

1. **基本的な埋め込み**
   - `"Hello {name}"`
   - 複数変数: `"{x} and {y}"`

2. **フォーマット指定子**
   - 16進数: `{value:x}`
   - 精度: `{pi:.2}`
   - アライメント: `{text:<10}`

3. **エスケープ**
   - `"{{ literal brace }}"`

4. **使用場所**
   - 変数代入
   - 関数引数
   - 戻り値

## 移行計画

### Phase 1（最小実装）
1. `string_interpolation.hpp` の統合
2. HIR lowering での基本的な埋め込み処理
3. 既存テストの動作確認

### Phase 2（完全実装）
1. フォーマット指定子のサポート
2. MIR lowering から専用処理を削除
3. コードジェンの簡略化

### Phase 3（拡張）
1. 計算式の埋め込み
2. カスタムフォーマッタ
3. 最適化

## リスクと対策

### リスク
- 既存コードの大規模変更
- パフォーマンスへの影響
- 後方互換性

### 対策
- 段階的移行
- 既存テストの維持
- パフォーマンステストの追加

## まとめ

文字列埋め込みを**言語の基本機能**として実装することで：
- コードがより直感的に
- 実装がよりシンプルに
- 保守がより容易に

現在の96.3%のテスト成功率を維持しながら、より良い設計に移行可能です。