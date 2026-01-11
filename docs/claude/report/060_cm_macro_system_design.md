# Cm言語マクロシステム設計書（Cm構文準拠版）

作成日: 2026-01-11
対象バージョン: v0.11.0
ステータス: 最終改訂

## エグゼクティブサマリー

Cm言語の**既存パーサーと中間言語（MIR）を活用**したマクロシステムを設計します。Rust風の構文を完全に排除し、Cmの構文規則に100%準拠します。

## 1. 設計理念

### 1.1 基本方針

1. **既存パーサー活用**: 新たなパーサーを書かない
2. **MIR統合**: 中間言語レベルで自然に処理
3. **型安全重視**: autoを使わず明示的な型宣言
4. **Cm構文準拠**: 関数オーバーロードのような自然な構文

### 1.2 従来の問題点と解決

| 問題点 | 解決策 |
|--------|--------|
| $x:expr などRust構文 | **廃止** - 通常のパラメータ構文 |
| 別パーサーが必要 | **不要** - 既存パーサーを拡張 |
| autoの多用 | **禁止** - 明示的な型宣言 |
| template<typename T> | **<T>構文** - Cm標準スタイル |

## 2. マクロ定義構文

### 2.1 基本構造（関数オーバーロード風）

```cm
// マクロ定義：関数定義と同じ構文
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

### 2.2 型ジェネリック対応

```cm
// ジェネリックマクロ（Cm標準の<T>構文）
<T>
macro create_vector(T first) {
    Vector<T> v;
    v.push(first);
    return v;
}

<T>
macro create_vector(T first, T... rest) {
    Vector<T> v;
    v.push(first);
    @foreach(r in rest) {
        v.push(r);
    }
    return v;
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

    // 型推論（将来的に）
    Vector<float> v4 = create_vector(1.0f);
    Vector<float> v5 = create_vector(1.0f, 2.0f, 3.0f);

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

## 5. Pin実装用マクロ

### 5.1 pinマクロ（Cm構文版）

```cm
// 値をPinする
<T>
macro pin(T& value) {
    Pin<T> pinned(value);
    return pinned;
}

// 新規変数として作成
<T>
macro pin_new(T value) {
    T temp = value;
    Pin<T> pinned(temp);
    return pinned;
}

// 使用例
int main() {
    int value = 42;
    Pin<int> p1 = pin(value);

    Pin<float> p2 = pin_new(3.14f);

    return 0;
}
```

## 6. 標準マクロライブラリ

### 6.1 基本マクロ（Cm構文）

```cm
// std/macros.cm

// 最小値
<T>
macro min(T a, T b) {
    return (a < b) ? a : b;
}

// 最大値
<T>
macro max(T a, T b) {
    return (a > b) ? a : b;
}

// スワップ
<T>
macro swap(T& a, T& b) {
    T temp = a;
    a = b;
    b = temp;
}

// 配列サイズ
<T, int N>
macro array_size(T[N]& arr) {
    return N;
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

// デバッグ出力
<T>
macro debug(string name, T value) {
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
        fprintf(stderr, "<object>\n");
    }
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

### 7.2 MIR表現例

```cm
// マクロ呼び出し
Vector<int> v = vec(1, 2, 3);

// AST展開後
Vector<int> v;
v.push(1);
v.push(2);
v.push(3);

// MIR表現
%0 = call Vector<int>::Vector()
%1 = const 1 : int
call Vector<int>::push(%0, %1)
%2 = const 2 : int
call Vector<int>::push(%0, %2)
%3 = const 3 : int
call Vector<int>::push(%0, %3)
```

## 8. 衛生性の簡易実装

### 8.1 自動変数名変換

```cm
// マクロ内のローカル変数は自動的に一意な名前に変換
macro safe_swap(int& a, int& b) {
    int temp = a;  // → int __macro_temp_XXX = a;
    a = b;
    b = temp;      // → b = __macro_temp_XXX;
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

### 9.1 コンパイルエラー

```
error: macro 'vec' expects arguments
  --> main.cm:10:15
   |
10 | Vector<int> v = vec;
   |                 ^^^ missing ()
   |
   = help: did you mean vec()?
```

### 9.2 型エラー

```
error: type mismatch in macro 'create_vector'
  --> main.cm:12:20
   |
12 | Vector<int> v = create_vector(1, 2.0f);
   |                               ^   ^^^^ float
   |                               int
   |
   = note: all arguments must have the same type
```

## 10. 実装ロードマップ

### Phase 1: 基本実装
1. マクロ定義の構文解析（既存パーサー拡張）
2. 単純な置換マクロ
3. オーバーロード解決

### Phase 2: 高度な機能
1. @foreach, @if ディレクティブ
2. 型ジェネリック対応
3. 衛生性実装

### Phase 3: 最適化
1. 展開結果のキャッシュ
2. インライン化との統合
3. デバッグ情報の保持

## 11. 他言語との比較

| 機能 | Cm（本設計） | Rust | C++ |
|------|------------|------|-----|
| 構文 | 関数風 | 特殊構文 | #define |
| 型安全 | ✅完全 | ✅ | ❌ |
| 既存パーサー活用 | ✅ | ❌ | N/A |
| 学習容易性 | ✅簡単 | ❌複雑 | ⚠️ |
| MIR統合 | ✅自然 | N/A | N/A |

## まとめ

この設計により：

1. **既存パーサーをそのまま活用**（新規パーサー不要）
2. **Cm構文に100%準拠**（関数定義と同じ）
3. **型安全性を完全保証**（autoを使わない）
4. **MIRレベルで自然に処理**（特殊な処理不要）
5. **学習コストゼロ**（関数と同じ構文）

Rustの複雑な構文を避け、Cmの既存インフラを最大限活用する実用的な設計です。

---

**作成者:** Claude Code
**ステータス:** 最終設計
**次ステップ:** パーサー拡張から実装開始