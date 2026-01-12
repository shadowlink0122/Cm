[English](type_system.en.html)

# Cm言語 型システム設計

## 型エイリアス（typedef）

### 基本構文

```cm
// 基本的な型エイリアス
typedef Int32 = int;
typedef StringPtr = string*;
typedef IntArray = int[];

// ジェネリック型エイリアス
typedef Vec<T> = std::vector<T>;
typedef Map<K, V> = std::unordered_map<K, V>;
```

### リテラル型

文字列や数値の特定の値を型として扱う：

```cm
// 文字列リテラル型
typedef Method = "GET" | "POST" | "PUT" | "DELETE";
typedef Status = "success" | "error" | "pending";

// 数値リテラル型
typedef HttpCode = 200 | 404 | 500;
typedef Priority = 1 | 2 | 3 | 4 | 5;
```

### ユニオン型（直積型）

タグ付きユニオン（代数的データ型）：

```cm
// Result型
typedef Result<T, E> =
    | { tag: "ok", value: T }
    | { tag: "err", error: E };

// Option型
typedef Option<T> =
    | { tag: "some", value: T }
    | { tag: "none" };

// より簡潔な記法（タグを自動生成）
typedef Result<T> = Ok(T) | Err(string);
typedef Option<T> = Some(T) | None;
```

### 使用例

```cm
// リテラル型の使用
typedef Status = "idle" | "loading" | "done" | "error";

Status getCurrentStatus() {
    return "loading";  // OK
    // return "invalid";  // コンパイルエラー
}

// ユニオン型の使用
typedef JsonValue =
    | Null
    | Bool(bool)
    | Number(double)
    | String(string)
    | Array(Vec<JsonValue>)
    | Object(Map<string, JsonValue>);

JsonValue parseJson(string input) {
    if (input == "null") {
        return Null;
    }
    if (input == "true") {
        return Bool(true);
    }
    // ...
}

// パターンマッチング
void processJson(JsonValue value) {
    match (value) {
        Null => println("null value"),
        Bool(b) => println("boolean: " + b),
        Number(n) => println("number: " + n),
        String(s) => println("string: " + s),
        Array(arr) => println("array with " + arr.len() + " items"),
        Object(obj) => println("object with " + obj.size() + " keys"),
    }
}
```

## テンプレート/ジェネリクス

### 簡潔な構文

```cm
// 関数テンプレート（Rust風）
fn max<T: Ord>(a: T, b: T) -> T {
    return a > b ? a : b;
}

// 構造体テンプレート
struct Vec<T> {
    T* data;
    size_t size;
    size_t capacity;
}

// impl ブロック（型パラメータを推論）
impl Vec<T> {  // T は Vec<T> から推論
    void push(T value) {
        // ...
    }

    T pop() {
        // ...
    }
}

// 複数の型パラメータ
struct Pair<A, B> {
    A first;
    B second;
}

impl Pair<A, B> {  // A, B は Pair<A, B> から推論
    void swap() {
        // A と B を入れ替える特殊化
    }
}
```

### 型制約

```cm
// 基本的な制約
fn sort<T: Ord>(vec: Vec<T>) {
    // T は Ord インターフェースを実装している必要がある
}

// 複数の制約
fn serialize<T: Debug + Serialize>(value: T) -> string {
    // T は Debug と Serialize の両方を実装
}

// where節（複雑な制約）
fn complex<T, U>(x: T, y: U) -> Result<T>
where
    T: Clone + Debug,
    U: Into<T>
{
    // ...
}
```

## 型推論

### ローカル型推論

```cm
// 変数の型推論
let x = 42;           // int と推論
let y = 3.14;         // double と推論
let s = "hello";      // string と推論

// ジェネリック関数の型推論
let max_int = max(10, 20);        // T = int と推論
let max_str = max("a", "b");      // T = string と推論
```

### 戻り値型の推論

```cm
// auto による戻り値型推論
auto add(int a, int b) {
    return a + b;  // int と推論
}

// テンプレート関数での推論
auto identity<T>(T value) {
    return value;  // T と推論
}
```

## 名前衝突の解決

### モジュールレベル

```cm
// 同じモジュール内での重複はエラー
module math;

int add(int a, int b) { ... }
int add(int a, int b) { ... }  // エラー：関数 'add' は既に定義されています

// 異なるモジュールなら OK
module math1;
export int add(int a, int b) { ... }

module math2;
export int add(int a, int b) { ... }

// 使用時は修飾名で区別
import math1;
import math2;

int main() {
    math1::add(1, 2);
    math2::add(1, 2);
}
```

### オーバーロード

```cm
// 関数オーバーロード（パラメータ型で区別）
int add(int a, int b) { ... }
double add(double a, double b) { ... }
string add(string a, string b) { ... }

// テンプレート特殊化
template<typename T>
T process(T value) { ... }

// 特定の型に対する特殊化
template<>
string process<string>(string value) { ... }
```

## 実装段階

### Phase 1: 基本機能
- [x] 基本型（int, double, string, bool）
- [x] ポインタ型
- [x] 配列型
- [ ] typedef による型エイリアス

### Phase 2: 高度な型
- [ ] リテラル型
- [ ] ユニオン型（タグ付き）
- [ ] ジェネリック型エイリアス

### Phase 3: テンプレート改善
- [ ] 簡潔なテンプレート構文
- [ ] impl ブロックの型推論
- [ ] 型制約（where節）

### Phase 4: 型推論
- [ ] ローカル型推論の改善
- [ ] 戻り値型の推論
- [ ] ジェネリック型引数の推論