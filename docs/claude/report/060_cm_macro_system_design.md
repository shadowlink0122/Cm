# Cm言語マクロシステム設計書（Cm構文完全準拠版）

作成日: 2026-01-11
対象バージョン: v0.11.0
ステータス: 最終改訂v2

## エグゼクティブサマリー

Cm言語の**既存パーサーと中間言語（MIR）を活用**したマクロシステムを設計します。マクロはトップレベル宣言として、他の構文と統一された形式を採用します。

## 1. 設計理念

### 1.1 基本方針

1. **トップレベル宣言の統一**: struct, enum, macro等すべて同じ形式
2. **既存パーサー活用**: 新たなパーサーを書かない
3. **MIR統合**: 中間言語レベルで自然に処理
4. **型安全重視**: autoを使わず明示的な型宣言

### 1.2 構文の一貫性

| 宣言種別 | 構文例 |
|---------|--------|
| 構造体 | `struct Point { ... }` |
| 列挙型 | `enum State { ... }` |
| インターフェース | `interface Iterator { ... }` |
| **マクロ** | `macro vec(...) { ... }` |
| ジェネリック構造体 | `struct Vector<T> { ... }` |
| **ジェネリックマクロ** | `macro min<T>(...) { ... }` |

## 2. マクロ定義構文

### 2.1 基本構造（トップレベル宣言）

```cm
// マクロ定義：トップレベル宣言として
macro vec() {
    return Vector<int>();
}

macro vec(int first) {
    Vector<int> v;
    v.push(first);
    return v;
}

// 可変長引数版（Cm既存の...構文）
macro vec(int first, int... rest) {
    Vector<int> v;
    v.push(first);
    // 展開時にrestの各要素に対してpushを生成
    @foreach(r in rest) {
        v.push(r);
    }
    return v;
}
```

### 2.2 型ジェネリック対応（正しい構文順）

```cm
// ジェネリックマクロ（macroが先、<T>は名前の後）
macro create_vector<T>(T first) {
    Vector<T> v;
    v.push(first);
    return v;
}

macro create_vector<T>(T first, T... rest) {
    Vector<T> v;
    v.push(first);
    @foreach(r in rest) {
        v.push(r);
    }
    return v;
}

// 複数型パラメータ
macro pair<T, U>(T first, U second) {
    Pair<T, U> p;
    p.first = first;
    p.second = second;
    return p;
}
```

## 3. マクロ呼び出し構文

### 3.1 関数呼び出し風

```cm
int main() {
    // 通常の関数呼び出しと同じ
    Vector<int> v1 = vec();
    Vector<int> v2 = vec(1);
    Vector<int> v3 = vec(1, 2, 3);

    // ジェネリックマクロの呼び出し
    Vector<float> v4 = create_vector<float>(1.0f);
    Vector<float> v5 = create_vector<float>(1.0f, 2.0f, 3.0f);

    // 型推論（将来的に）
    Pair<int, string> p = pair(42, "hello");

    return 0;
}
```

## 4. 特殊マクロディレクティブ

### 4.1 @foreach - 繰り返し展開

```cm
macro print_all(string... messages) {
    @foreach(msg in messages) {
        printf("%s\n", msg);
    }
}

// 展開結果：
// print_all("Hello", "World") →
// printf("%s\n", "Hello");
// printf("%s\n", "World");
```

### 4.2 @if/@else - 条件付き展開

```cm
macro assert(bool condition, string message = "") {
    @if(message == "") {
        if (!condition) {
            fprintf(stderr, "Assertion failed at %s:%d\n",
                   __FILE__, __LINE__);
            abort();
        }
    }
    @else {
        if (!condition) {
            fprintf(stderr, "Assertion failed: %s at %s:%d\n",
                   message, __FILE__, __LINE__);
            abort();
        }
    }
}
```

### 4.3 @stringify - 文字列化

```cm
macro dbg(int value) {
    fprintf(stderr, "[DEBUG] %s = %d\n", @stringify(value), value);
    return value;
}

// 使用例：
// int x = 42;
// dbg(x);  // 出力: [DEBUG] x = 42
```

### 4.4 @type_name - 型名取得

```cm
macro print_type<T>(T value) {
    printf("Type: %s, Value: ", @type_name(T));
    @if(T == int) {
        printf("%d\n", value);
    }
    @elif(T == float) {
        printf("%f\n", value);
    }
    @else {
        printf("<object>\n");
    }
}
```

## 5. Pin実装用マクロ

### 5.1 pinマクロ（正しい構文順）

```cm
// 値をPinする
macro pin<T>(T& value) {
    Pin<T> pinned(value);
    return pinned;
}

// 新規変数として作成
macro pin_new<T>(T value) {
    T temp = value;
    Pin<T> pinned(temp);
    return pinned;
}

// Box版
macro pin_box<T>(T value) {
    Box<T> boxed = Box<T>::new(value);
    Pin<Box<T>> pinned(boxed);
    return pinned;
}

// 使用例
int main() {
    int value = 42;
    Pin<int> p1 = pin(value);
    Pin<float> p2 = pin_new(3.14f);
    Pin<Box<int>> p3 = pin_box(100);

    return 0;
}
```

## 6. 標準マクロライブラリ

### 6.1 基本マクロ（正しい構文）

```cm
// std/macros.cm

// 最小値
macro min<T>(T a, T b) {
    return (a < b) ? a : b;
}

// 最大値
macro max<T>(T a, T b) {
    return (a > b) ? a : b;
}

// スワップ
macro swap<T>(T& a, T& b) {
    T temp = a;
    a = b;
    b = temp;
}

// 配列サイズ
macro array_size<T, int N>(T[N]& arr) {
    return N;
}

// 範囲チェック
macro clamp<T>(T value, T min_val, T max_val) {
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}
```

### 6.2 出力マクロ

```cm
// 改行付き出力
macro println() {
    printf("\n");
}

macro println(string format, void*... args) {
    printf(format, args);
    printf("\n");
}

// デバッグ出力（ジェネリック版）
macro debug<T>(string name, T value) {
    fprintf(stderr, "[%s:%d] %s = ", __FILE__, __LINE__, name);
    // 型に応じた出力を生成
    @if(T == int) {
        fprintf(stderr, "%d\n", value);
    }
    @elif(T == float) {
        fprintf(stderr, "%f\n", value);
    }
    @elif(T == string) {
        fprintf(stderr, "%s\n", value);
    }
    @else {
        fprintf(stderr, "<%s>\n", @type_name(T));
    }
}
```

### 6.3 コレクションマクロ

```cm
// ベクター初期化
macro vec_of<T>(T... elements) {
    Vector<T> v;
    @foreach(elem in elements) {
        v.push(elem);
    }
    return v;
}

// マップ初期化（ペア形式）
macro map_of<K, V>(K key, V value) {
    Map<K, V> m;
    m.insert(key, value);
    return m;
}

// 使用例
int main() {
    Vector<int> numbers = vec_of(1, 2, 3, 4, 5);
    Map<string, int> ages = map_of("Alice", 30);

    return 0;
}
```

## 7. MIRレベルでの処理

### 7.1 マクロ展開のタイミング

```
ソースコード（Cm構文）
    ↓
[Lexer] → [Parser] → AST
    ↓
[マクロ展開フェーズ] ← ASTレベルで展開
    ↓
[HIR生成]
    ↓
[MIR生成] ← 通常の関数と同じ扱い
```

### 7.2 パーサーでの認識

```cpp
// parser.cpp での処理
if (current_token == "macro") {
    // トップレベル宣言として処理
    return parse_macro_declaration();
}

// macro <name> または macro <name><T>
MacroDecl* parse_macro_declaration() {
    expect("macro");
    string name = parse_identifier();

    // ジェネリックパラメータ（オプション）
    if (current_token == "<") {
        parse_generic_params();
    }

    // パラメータリスト
    expect("(");
    parse_parameter_list();
    expect(")");

    // マクロ本体
    expect("{");
    parse_macro_body();
    expect("}");
}
```

## 8. 衛生性の簡易実装

### 8.1 自動変数名変換

```cm
// マクロ内のローカル変数は自動的に一意な名前に変換
macro safe_swap<T>(T& a, T& b) {
    T temp = a;  // → T __macro_temp_XXX = a;
    a = b;
    b = temp;    // → b = __macro_temp_XXX;
}

// 名前衝突しない
int main() {
    int x = 1, y = 2;
    int temp = 100;  // ユーザーの変数

    safe_swap(x, y);  // tempは衝突しない

    printf("%d\n", temp);  // 100が出力される
    return 0;
}
```

## 9. エラー処理

### 9.1 構文エラー

```
error: expected generic parameters after macro name
  --> main.cm:10:6
   |
10 | macro<T> vec(T first) {
   |      ^ should be after 'vec'
   |
   = help: correct syntax: macro vec<T>(T first)
```

### 9.2 型エラー

```
error: type mismatch in macro 'create_vector'
  --> main.cm:12:20
   |
12 | Vector<int> v = create_vector<int>(1, 2.0f);
   |                                    ^   ^^^^ float
   |                                    int
   |
   = note: all arguments must have the same type T
```

## 10. 構文のBNF（簡略版）

```ebnf
// トップレベル宣言
top_level_decl ::= struct_decl
                 | enum_decl
                 | interface_decl
                 | function_decl
                 | macro_decl

// マクロ宣言
macro_decl ::= 'macro' identifier generic_params? '(' parameters ')' block

// ジェネリックパラメータ
generic_params ::= '<' type_param (',' type_param)* '>'

// パラメータ
parameters ::= parameter (',' parameter)*
parameter ::= type identifier ('...')? ('=' default_value)?

// マクロ本体
block ::= '{' (statement | directive)* '}'
directive ::= '@foreach' | '@if' | '@stringify' | '@type_name'
```

## 11. 実装優先度

### Phase 1: 基本実装
1. `macro` キーワードの認識
2. 単純な置換マクロ
3. オーバーロード解決

### Phase 2: ジェネリック対応
1. `macro name<T>` 構文の解析
2. 型パラメータの展開
3. 型推論の基礎

### Phase 3: ディレクティブ
1. @foreach 実装
2. @if/@else 実装
3. @stringify, @type_name 実装

## まとめ

この設計により：

1. **構文の一貫性**: `macro vec<T>` はCmの他の宣言と同じ形式
2. **既存パーサー活用**: トップレベル宣言として自然に処理
3. **型安全性**: ジェネリックによる完全な型チェック
4. **学習コスト最小**: 関数定義と同じ感覚で書ける
5. **MIR統合**: 特殊処理不要で既存の最適化を活用

トップレベル宣言として統一された、Cmらしいマクロシステムです。

---

**作成者:** Claude Code
**ステータス:** 最終設計v2
**次ステップ:** パーサー拡張から実装開始