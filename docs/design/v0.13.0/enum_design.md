# Enum 設計仕様

## 基本原則

Cm言語のenumは **Tagged Union** として設計されており、以下の原則に従う：

### 1. 各バリアントは0個または1個のフィールドのみ持つ

```cm
// 正しい設計
enum Message {
    Quit,                  // データなし
    Move(Point),           // 構造体を1つ持つ
    Write(string),         // 文字列を1つ持つ
    ChangeColor(RGB)       // 構造体を1つ持つ
}

// 誤った設計（複数フィールド禁止）
enum Message {
    Move(int x, int y),    // ❌ 複数フィールドは禁止
    ChangeColor(int r, int g, int b)  // ❌
}
```

### 2. 複数値が必要な場合は構造体で包む

```cm
// 構造体を定義
struct Point {
    int x;
    int y;
}

struct RGB {
    int r;
    int g;
    int b;
}

// enumで構造体を使用
enum Message {
    Move(Point),
    ChangeColor(RGB)
}
```

### 3. Result<T, E> の設計

`Result<T, E>` は `Ok(T)` と `Err(E)` で各々1つの型を持つ：

```cm
enum Result<T, E> {
    Ok(T),   // T型の値を1つ持つ
    Err(E)   // E型の値を1つ持つ
}
```

## match文の設計

### 1. matchはステートメント形式で使用

```cm
int value = 0;
match (opt) {
    Option::None => {
        value = 0;
    }
    Option::Some(v) => {
        value = v;
    }
}
```

### 2. matchが直接値をreturnするのは暗黙的return

```cm
// 以下は暗黙的returnの省略形
int v = match (opt) {
    Option::None => 0,
    Option::Some(x) => x
};

// 明示的に書くと
int v;
match (opt) {
    Option::None => { v = 0; }
    Option::Some(x) => { v = x; }
}
```

## パターンマッチでの値抽出

### 構文

```
EnumType::Variant(binding_var) => { ... }
```

### 例

```cm
match (result) {
    Result::Ok(value) => {
        // value に Ok の値が束縛される
        println("Success: {value}");
    }
    Result::Err(error) => {
        // error に Err の値が束縛される
        println("Error: {error}");
    }
}
```

## 実装状況

| 機能 | 状況 |
|:-----|:----:|
| 基本enum定義 | ✅ 完了 |
| Associated Data（1フィールド） | ✅ 完了 |
| HirEnumMember.fields | ✅ 完了 |
| enum値のmatch | ✅ 完了 |
| パターンバインディング | 🚧 実装中 |
| impl for enum | ⏳ 未実装 |
