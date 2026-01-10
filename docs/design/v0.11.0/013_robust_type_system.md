# Cm言語 堅牢な型システム設計

## 設計原則

1. **明示性**: すべての型は明示的に宣言
2. **予測可能性**: 型推論は局所的で予測可能
3. **安全性**: コンパイル時に型エラーを検出
4. **ツーリング**: IDE/LSPサポートを考慮

## ジェネリック型の宣言

### 関数のジェネリクス（明示的）

```cm
// Option 1: TypeScript/Rust風
func max<T: Ord>(a: T, b: T) -> T {
    return a > b ? a : b;
}

// Option 2: C++改良版
template<T where T: Ord>
T max(T a, T b) {
    return a > b ? a : b;
}

// Option 3: 簡潔版（でも明示的）
<T: Ord> T max(T a, T b) {
    return a > b ? a : b;
}
```

### 構造体のジェネリクス

```cm
// 明示的な型パラメータ宣言
struct Vec<T> {
    data: *T;
    size: usize;
    capacity: usize;
}

// impl ブロックも明示的
impl<T> Vec<T> {
    func new() -> Vec<T> {
        return Vec {
            data: null,
            size: 0,
            capacity: 0
        };
    }

    func push(self: &mut Vec<T>, item: T) {
        // ...
    }
}

// 型制約付き実装
impl<T: Display> Vec<T> {
    func print(self: &Vec<T>) {
        for item in self {
            println(item);
        }
    }
}
```

## 型エイリアス

### 基本的な型エイリアス

```cm
// type キーワードを使用（TypeScript風）
type Int32 = int;
type StringMap<T> = Map<string, T>;
type Predicate<T> = func(T) -> bool;

// 使用例
let numbers: Int32[] = [1, 2, 3];
let cache: StringMap<int> = new Map();
let is_even: Predicate<int> = (n) => n % 2 == 0;
```

### ユニオン型

```cm
// 明示的なタグ付きユニオン
type Result<T, E> =
    | { tag: "ok", value: T }
    | { tag: "err", error: E };

// パターンマッチング
func process<T, E>(result: Result<T, E>) {
    match result {
        { tag: "ok", value } => println("Success: " + value),
        { tag: "err", error } => println("Error: " + error),
    }
}

// 簡略記法（糖衣構文）
type Option<T> = Some(T) | None;

// 展開されると：
type Option<T> =
    | { tag: "some", value: T }
    | { tag: "none" };
```

### リテラル型

```cm
// 文字列リテラル型
type HttpMethod = "GET" | "POST" | "PUT" | "DELETE";

// 数値リテラル型
type StatusCode = 200 | 404 | 500;

// 使用時の型チェック
func handle_request(method: HttpMethod, path: string) -> StatusCode {
    if method == "GET" {
        return 200;
    }
    // return 999;  // エラー：999 は StatusCode に含まれない
    return 404;
}
```

## 型推論

### ローカル型推論（安全）

```cm
// 変数の型推論（右辺から推論）
let x = 42;              // x: int
let s = "hello";         // s: string
let arr = [1, 2, 3];     // arr: int[]

// ジェネリック関数の呼び出し（実引数から推論）
func identity<T>(value: T) -> T { return value; }

let a = identity(42);     // T = int と推論
let b = identity("hi");   // T = string と推論
```

### 制限された推論

```cm
// 戻り値型は推論しない（明示的に書く）
func add(a: int, b: int) -> int {  // -> int は必須
    return a + b;
}

// ジェネリック型パラメータは明示的に宣言
// func bad(x: T) -> T { ... }  // エラー：T が未宣言
func good<T>(x: T) -> T { ... }  // OK：T を明示的に宣言
```

## 型の互換性と変性

### 共変性と反変性

```cm
// 共変性（出力位置）
type Producer<+T> = func() -> T;

// 反変性（入力位置）
type Consumer<-T> = func(T) -> void;

// 不変性（デフォルト）
type Container<T> = struct {
    value: T;
};

// サブタイプ関係
class Animal {}
class Dog : Animal {}

let dogProducer: Producer<Dog> = () => new Dog();
let animalProducer: Producer<Animal> = dogProducer;  // OK（共変）

let animalConsumer: Consumer<Animal> = (a: Animal) => {};
let dogConsumer: Consumer<Dog> = animalConsumer;  // OK（反変）
```

## 型ガード

```cm
// 型の絞り込み
func process(value: int | string) {
    if typeof(value) == "int" {
        // ここでは value: int
        let doubled = value * 2;
    } else {
        // ここでは value: string
        let upper = value.to_upper();
    }
}

// カスタム型ガード
func is_some<T>(opt: Option<T>) -> bool {
    return opt.tag == "some";
}

func unwrap<T>(opt: Option<T>) -> T {
    if is_some(opt) {
        // 型ガードにより opt は Some<T> と推論
        return opt.value;
    }
    panic("Called unwrap on None");
}
```

## null安全性

```cm
// null許容型
type Nullable<T> = T | null;

// null非許容がデフォルト
let x: int = 42;        // null不可
let y: ?int = null;     // null可能（糖衣構文）
let z: int | null = null;  // 明示的

// nullチェック
func process(value: ?string) {
    if value != null {
        // ここでは value: string（nullでない）
        println(value.len());
    }
}

// Optional chaining
let length = user?.address?.street?.len();
```

## エラー処理の型

```cm
// Resultベースのエラー処理
type Result<T, E = Error> = Ok(T) | Err(E);

func divide(a: int, b: int) -> Result<int> {
    if b == 0 {
        return Err(Error("Division by zero"));
    }
    return Ok(a / b);
}

// ? 演算子でのエラー伝播
func calculate() -> Result<int> {
    let x = divide(10, 2)?;  // エラーなら早期リターン
    let y = divide(x, 3)?;
    return Ok(y);
}
```

## 型レベルプログラミング

```cm
// 条件型
type If<C: bool, T, F> = C extends true ? T : F;

// マップ型
type Readonly<T> = {
    readonly [P in keyof T]: T[P];
};

// ユーティリティ型
type Partial<T> = {
    [P in keyof T]?: T[P];
};

type Required<T> = {
    [P in keyof T]-?: T[P];
};

type Pick<T, K: keyof T> = {
    [P in K]: T[P];
};
```

## 実装段階

### Phase 1: 基本型システム
- [x] プリミティブ型
- [x] 構造体
- [ ] 明示的ジェネリクス宣言
- [ ] 型エイリアス（type/typedef）

### Phase 2: 高度な型
- [ ] ユニオン型
- [ ] リテラル型
- [ ] null許容型
- [ ] 型ガード

### Phase 3: 型推論
- [ ] ローカル変数の型推論
- [ ] ジェネリック関数の型推論
- [ ] 型の絞り込み

### Phase 4: 高度な機能
- [ ] 変性（共変・反変）
- [ ] 条件型
- [ ] マップ型
- [ ] 型レベル計算

## まとめ

堅牢な型システムの鍵は**明示性**と**予測可能性**です。暗黙的すぎる推論は避け、プログラマーの意図を明確に表現できる構文を採用します。これにより：

1. **型安全性**: コンパイル時にエラーを検出
2. **IDE支援**: 自動補完や型情報の表示が容易
3. **保守性**: コードの意図が明確で理解しやすい
4. **拡張性**: 将来の機能追加が容易

TypeScriptの成功から学び、C++の表現力と組み合わせることで、実用的で堅牢な型システムを実現します。