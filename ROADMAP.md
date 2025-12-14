# Cm言語 開発ロードマップ

## 開発方針

> **LLVMベースアーキテクチャ**: 2024年12月より、LLVM IRを唯一のコード生成バックエンドとして採用。
> Rust/TypeScript/C++へのトランスパイルは今後行いません。

## Version 0.1.0 ✅

### 完成済み機能
- ✅ 基本的な式と文の実行（インタプリタ）
- ✅ フォーマット文字列（変数自動キャプチャ）
- ✅ MIRインタプリタ
- ✅ コマンドベースCLI
- ✅ デバッグシステム
- ✅ LLVMバックエンド基本実装
- ✅ LLVM Native/WASM出力
- ✅ 構造体定義とフィールドアクセス
- ✅ switch文（整数型、char型対応）
- ✅ 基本型システム（int, uint, char, string, bool, double, etc.）

## Version 0.2.0 - スコープとリソース管理 ✅

### 目標
ブロックスコープの完全サポートとリソース管理基盤の構築

### 実装項目
| 機能 | パーサー | 型チェック | HIR/MIR | インタプリタ | LLVM | WASM |
|------|----------|-----------|---------|-------------|------|------|
| スコープトラッキング | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| StorageDead自動発行 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| defer文 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| ブロック終了時処理 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

### 技術詳細
```cm
// defer: ブロック終了時に逆順で実行
void example() {
    defer println("3rd");
    defer println("2nd");
    defer println("1st");
    // 出力: 1st, 2nd, 3rd
}

// スコープによる変数の有効範囲
void scope_test() {
    {
        int x = 10;
        // StorageLive(x)
    }
    // StorageDead(x) - ここで自動発行
}
```

## Version 0.3.0 - Interface/Impl/Self ✅

### 目標
型に振る舞いを付与するインターフェースシステムの実装

### 設計原則
- **構造体には直接メソッドを定義できない**
- 全てのメソッドは`interface`を通じて定義される
- `impl Interface for Type`で実装を提供
- **プリミティブ型へのimplも可能**

### 実装項目
| 機能 | パーサー | 型チェック | HIR/MIR | インタプリタ | LLVM | WASM |
|------|----------|-----------|---------|-------------|------|------|
| interface定義 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| impl Interface for Type | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| selfキーワード | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| メソッド呼び出し | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 静的ディスパッチ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| プリミティブ型へのimpl | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

### 構文例
```cm
// インターフェース定義
interface Printable {
    void print();
}

interface Container<T> {
    void push(T item);
    T pop();
    bool is_empty();
}

// 構造体定義（メソッドは書けない）
struct Point {
    double x;
    double y;
}

// インターフェース実装（構造体）
impl Printable for Point {
    void print() {
        println("({}, {})", self.x, self.y);
    }
}

// プリミティブ型へのimpl
impl Printable for int {
    void print() {
        println("{}", self);
    }
}
```

## Version 0.4.0 - privateメソッドとdefaultメンバ推論 ✅

### 目標
privateメソッドによるカプセル化とdefaultメンバの暗黙的アクセス

### 実装項目
| 機能 | パーサー | 型チェック | HIR/MIR | インタプリタ | LLVM | WASM |
|------|----------|-----------|---------|-------------|------|------|
| privateメソッド | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| privateアクセスチェック | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| defaultメンバ暗黙代入 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| defaultメンバ暗黙取得 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| impl Type（基本impl） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| self()コンストラクタ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| overloadコンストラクタ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Type name(args)構文 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Type name 暗黙的コンストラクタ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| selfへの参照渡し | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| ~self()デストラクタ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| RAII自動呼び出し | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

### privateメソッド構文例
```cm
interface Calculator {
    int calculate(int x);
}

struct MyCalc {
    int base;
}

impl MyCalc for Calculator {
    // privateメソッド：impl内からのみ呼び出し可能
    private int helper(int n) {
        return n * 2;
    }

    // publicメソッド：外部から呼び出し可能
    int calculate(int x) {
        return self.helper(x) + self.base;
    }
}
```

### コンストラクタ/デストラクタ構文例（今後実装）
```cm
struct Vec<T> {
    private T* data;
    private size_t size;
    private size_t capacity;
}

// コンストラクタ/デストラクタ専用impl
impl<T> Vec<T> {
    self() {
        this.data = null;
        this.size = 0;
    }

    overload self(size_t capacity) {
        this.data = new T[capacity];
        this.size = 0;
        this.capacity = capacity;
    }

    ~self() {
        delete[] this.data;
    }
}

// メソッドはinterfaceを通じて実装
impl<T> Vec<T> for Container<T> {
    void push(T item) { /* ... */ }
    T pop() { /* ... */ }
}
```

## Version 0.5.0 - Enum・リテラル型・ユニオン型 ✅

### 目標
高度な型システム基盤の構築（Result/Option型の前提条件）

### 実装項目
| 機能 | パーサー | 型チェック | HIR/MIR | インタプリタ | LLVM | WASM |
|------|----------|-----------|---------|-------------|------|------|
| enum定義 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| enum値アクセス (NAME::MEMBER) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| enum負の値サポート | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| enum値重複チェック | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| enumオートインクリメント | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| enumをフィールド型として使用 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| typedef 型エイリアス | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| typedef 構造体エイリアス | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| typedef リテラル型 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| typedef ユニオン型 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 高度な型を引数/戻り値に使用 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| インターフェース型を引数に使用 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| インターフェースメソッド動的ディスパッチ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 構造体戻り値（バグ修正） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| switch文リテラル型推論（バグ修正） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

### Enum構文
```cm
// 基本的なenum定義
enum Color {
    Red = 0,      // 明示的に0
    Green,        // 自動で1
    Blue          // 自動で2
}

// 負の値を含むenum
enum Status {
    Error = -1,
    Unknown = 0,
    Success = 1,
    Pending        // 自動で2
}

// 使用例
int main() {
    int c = Color::Red;      // 0
    int s = Status::Error;   // -1
    
    // switch文との組み合わせ
    switch (c) {
        case Color::Red:   println("赤"); break;
        case Color::Green: println("緑"); break;
        case Color::Blue:  println("青"); break;
    }
    return 0;
}

// 値の重複はコンパイルエラー
enum Invalid {
    First = 1,
    One = 1      // エラー: 値1は既にFirstで使用されています
}
```

### リテラル型構文
```cm
// 文字列リテラル型
typedef HttpMethod = "GET" | "POST" | "PUT" | "DELETE";

// 数値リテラル型
typedef Digit = 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9;

// 使用例
void handle_request(HttpMethod method) {
    // methodは"GET", "POST", "PUT", "DELETE"のいずれか
}

int main() {
    handle_request("GET");    // OK
    handle_request("PATCH");  // コンパイルエラー
    return 0;
}
```

### ユニオン型構文
```cm
// プリミティブ型のユニオン
typedef Number = int | double;

// 構造体を含むユニオン
struct Circle { double radius; }
struct Rectangle { double width; double height; }
typedef Shape = Circle | Rectangle;

// interfaceを含むユニオン
interface Drawable { void draw(); }
interface Clickable { void on_click(); }
typedef Widget = Drawable | Clickable;

// 関数での使用
Number add(Number a, Number b) {
    // 型に応じた処理
}

void render(Shape shape) {
    // CircleかRectangleに応じた処理
}

// enumとユニオン型の組み合わせ
enum JsonType { Null, Boolean, Number, String, Array, Object }
typedef JsonValue = null | bool | double | string | JsonValue[] | Map<string, JsonValue>;
```

### 型情報の拡張
```cm
// 全ての型は関数の引数・戻り値として使用可能
struct Point { int x; int y; }
interface Serializable { string serialize(); }
enum Status { Ok, Error }
typedef Result = int | string;

// 構造体を返す
Point create_point(int x, int y) {
    Point p;
    p.x = x;
    p.y = y;
    return p;
}

// enumを引数に取る
void handle_status(Status s) { }

// ユニオン型を返す
Result divide(int a, int b) {
    if (b == 0) {
        return "Division by zero";
    }
    return a / b;
}
```

## Version 0.6.0 - ジェネリクスと定数 ✅完了

### 目標
型パラメータとジェネリックプログラミング、const/static修飾子の実装

### 実装項目
| 機能 | パーサー | 型チェック | HIR/MIR | インタプリタ | LLVM | WASM |
|------|----------|-----------|---------|-------------|------|------|
| 関数ジェネリクス | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 構造体ジェネリクス | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 型推論 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 関数モノモーフィゼーション | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 構造体モノモーフィゼーション | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| const修飾子 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| static修飾子（変数） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| デフォルト引数 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

### 完了した作業（2024年12月14日）
- ✅ パーサーレベルでのジェネリック構文サポート（<T>, <T: Constraint>）
- ✅ GenericParam構造体による型制約情報の保持
- ✅ ジェネリック型変数宣言のパース（Type<T> name）
- ✅ ジェネリック関数の型チェックと型推論
- ✅ ジェネリック構造体のフィールドアクセス時の型置換
- ✅ ジェネリック構造体パラメータからの型推論
- ✅ MIRレベルでのモノモーフィゼーション（関数）
- ✅ MIRレベルでのモノモーフィゼーション（構造体）- LLVM/WASM対応
- ✅ static変数の実装（全バックエンド対応）
- ✅ 型チェッカーでの型推論（呼び出し時）
- ✅ const修飾子（全バックエンド）
- ✅ デフォルト引数（全バックエンド対応）
- ✅ ジェネリック型に対する動的ディスパッチ（T__method -> ActualType__method）
- ✅ コンストラクタ呼び出し形式の正規化（Type name = Type(args)形式のサポート）
- ✅ インターフェースメソッド呼び出しのモノモーフィゼーション時書き換え
- ✅ ジェネリック構造体のLLVM/WASMモノモーフィゼーション（Pair<int>, Box<Point>等）

### テスト結果（2024年12月14日）
- インタプリタ: 89/89 PASS（全テスト合格）
- LLVM Native: 89/89 PASS（全テスト合格）
- WASM: 89/89 PASS（全テスト合格）

### 構文例
```cm
// Result型の定義（標準ライブラリ）
enum ResultTag { Ok, Err }
struct Result<T, E> {
    ResultTag tag;
    T ok_value;
    E err_value;
}

// Option型の定義（標準ライブラリ）
enum OptionTag { Some, None }
struct Option<T> {
    OptionTag tag;
    T value;
}

// 使用例
Result<int, string> divide(int a, int b) {
    if (b == 0) {
        return Result<int, string>{ ResultTag::Err, 0, "Division by zero" };
    }
    return Result<int, string>{ ResultTag::Ok, a / b, "" };
}

// ?演算子（エラー伝播）
Result<int, string> calculate() {
    int x = divide(10, 2)?;  // エラーなら即座にreturn
    int y = divide(x, 0)?;   // ここでエラー発生 → 関数からreturn
    return Result<int, string>{ ResultTag::Ok, y, "" };
}
```

## Version 0.7.0 - 高度なジェネリクスとWASMサポート (完了)

### 目標
ジェネリクスの完全実装とWASMバックエンドの提供

### 実装項目
| 機能 | パーサー | 型チェック | HIR/MIR | インタプリタ | LLVM | WASM | テスト |
|------|----------|-----------|---------|-------------|------|------|------|
| インターフェースジェネリクス | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| implジェネリクス | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 型制約（where句）パース | ✅ | - | - | - | - | - | - |
| インターフェースモノモーフィゼーション | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| implモノモーフィゼーション | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| WASMコンパイル | ✅ | ✅ | ✅ | - | ✅ | ✅ | ✅ |
| 複数型パラメータ（構造体） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 複数型パラメータ（関数） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

### 完了した作業（2024年12月14日）

#### ジェネリクス
- ✅ パーサー: `impl<T>` 構文のサポート
- ✅ パーサー: `impl<T> Type<T> for Interface<T>` 構文のサポート
- ✅ 型チェッカー: ジェネリックimplメソッドの型推論
- ✅ 型チェッカー: ジェネリック型パラメータの置換
- ✅ 型チェッカー: ジェネリック構造体メソッドの解決（Container<int> → Container<T>）
- ✅ 型チェッカー: ジェネリック関数戻り値型の置換（Option<T> → Option<int>）
- ✅ HIR/MIR: implジェネリックパラメータの関数への伝播
- ✅ モノモーフィゼーション: 関数名マングリング改善（Container<int>__print → Container__int__print）
- ✅ モノモーフィゼーション: 呼び出し箇所の自動書き換え
- ✅ モノモーフィゼーション: 複数型パラメータ構造体の置換（Pair__T__U → Pair__int__string）
- ✅ インタプリタ: ジェネリックimplメソッドの実行

#### WASMバックエンド
- ✅ LLVMからWASMへのコンパイル
- ✅ WASMランタイムライブラリ
- ✅ wasm-ldによるリンク
- ✅ 基本的なプログラムの動作確認
- ✅ ジェネリクスのWASMサポート

#### 複数型パラメータ
- ✅ 構造体定義（`Pair<T, U>`）
- ✅ フィールドアクセス
- ✅ 異なる型の組み合わせ（`Pair<int, string>`）
- ✅ モノモーフィゼーション（Pair__int__string）

### 既知の制限事項（v0.8.0で解決済み）

以下の問題はv0.8.0で検証の結果、実際には動作していました：

1. **implジェネリクスのLLVMコンパイル** - ✅ 解決済み
   - テスト: `tests/test_programs/generics/impl_generics.cm`

2. **複数型パラメータとジェネリック関数** - ✅ 解決済み
   - テスト: `tests/test_programs/generics/multiple_type_params.cm`

3. **型制約チェック** - ⚠️ v0.8.0で実装中
   - パースは可能だが実行時検証なし
   - `<T: Ord>`のような制約が検証されない

### テスト結果
- ✅ 90/90 LLVMテストパス
- ✅ 6つのジェネリクステスト
- ✅ WASMコンパイル・実行成功
- ✅ インタプリタ・LLVM両対応

### 備考
v0.7.0は単一型パラメータのジェネリクスを完全サポートし、WASMバックエンドを提供します。
型制約の完全実装はv0.8.0で行います。

### 実装アプローチ（メモ）
```
1. 型制約チェック
   - GenericParamのconstraints情報を使用
   - 型チェック時にインターフェース実装を検証
   - エラーメッセージ: "Type T does not implement Ord"

2. インターフェースジェネリクス
   - interface Comparable<T> { bool compare(T other); }
   - 型パラメータを持つインターフェースの定義と使用

3. implジェネリクス
   - impl<T> Container for Box<T> { ... }
   - 型パラメータを持つimplブロック
   - モノモーフィゼーション時に具体的な型で特殊化
```

---

## Version 0.8.0 - ジェネリクスの完全実装とパターンマッチング

### 目標
v0.7.0で残存する問題を解決し、ジェネリクスを完全に実装。加えてmatch式を導入。

### 完了した作業（2024年12月14日）

#### v0.7.0の「既知の問題」の検証と解決
v0.7.0で報告された以下の問題は、実際にはすでに解決されていました：

1. **impl<T>のLLVM関数署名**: ✅ 動作確認済み
   - `impl<T> Container<T> for Interface`パターンがLLVMでコンパイル・実行可能
   - テスト: `tests/test_programs/generics/impl_generics.cm`

2. **複数型パラメータとジェネリック関数**: ✅ 動作確認済み  
   - `<T, U> Pair<T, U> make_pair(T a, U b)`パターンが動作
   - テスト: `tests/test_programs/generics/multiple_type_params.cm`

3. **Option<T>/Result<T, E>型**: ✅ 動作確認済み
   - ジェネリック構造体として実装可能
   - テスト: `tests/test_programs/generics/option_type.cm`, `tests/test_programs/generics/result_type.cm`

#### match式の実装
- **パーサー**: ✅ `match (expr) { pattern => body, ... }` 構文をサポート
- **型チェック**: ✅ scrutineeとアームの型整合性チェック
- **HIR/MIR**: ✅ 三項演算子のネストに脱糖
- **インタプリタ**: ✅ 完全動作
- **LLVM（整数型）**: ✅ 動作確認済み
- **LLVM（文字列型）**: ⚠️ 既知の問題あり（三項演算子の型変換）

#### 実装されたmatchパターン
- リテラルパターン（数値、真偽値）
- enum値パターン（`Status::Ok`）
- ワイルドカードパターン（`_`）
- 変数束縛パターン（基本サポート）

### 実装項目
| 機能 | パーサー | 型チェック | HIR/MIR | インタプリタ | LLVM | WASM | テスト |
|------|----------|-----------|---------|-------------|------|------|------|
| impl<T>パラメータ型置換 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 複数型パラメータ+関数 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 型制約チェック | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| match式基本 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| パターンガード（基本） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| パターンガード（変数束縛） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 網羅性チェック（整数/bool） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 網羅性チェック（enum型） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

### テスト結果（2024年12月14日）
- インタプリタ: 全テストパス
- LLVM Native: 整数型match式パス
- 新規テスト追加:
  - `tests/test_programs/match/match_basic.cm`
  - `tests/test_programs/match/match_enum.cm`
  - `tests/test_programs/generics/impl_generics.cm`
  - `tests/test_programs/generics/multiple_type_params.cm`
  - `tests/test_programs/generics/option_type.cm`
  - `tests/test_programs/generics/result_type.cm`
  - `tests/test_programs/generics/generic_with_struct.cm`
  - `tests/test_programs/generics/simple_pair.cm`
  - `tests/test_programs/errors/constraint_error.cm`

### v0.8.0 完了（2024年12月14日）
v0.8.0の全機能が実装完了しました：
- ✅ match式（整数/文字列/bool型）
- ✅ パターンガード（変数束縛含む）
- ✅ 網羅性チェック（整数/bool/enum型）
- ✅ 型制約チェック
- ✅ LLVM/WASMコンパイル

### 構文例

```cm
enum Option<T> {
    Some(T),
    None
}

enum Result<T, E> {
    Ok(T),
    Err(E)
}

// 使用例
<T> Option<T> find(T[] arr, T target) {
    for (int i = 0; i < arr.length; i++) {
        if (arr[i] == target) {
            return Option::Some(arr[i]);
        }
    }
    return Option::None;
}

// matchでの処理
Option<int> result = find([1, 2, 3], 2);
match (result) {
    Option::Some(val) => println("見つかった: {}", val),
    Option::None => println("見つからなかった"),
}
```

---

## Version 0.9.0 - 配列とコレクション

### 目標
動的・静的配列とベクター型の実装（ジェネリクスを活用）

### 完了した作業（2024年12月15日）

#### C++スタイル配列構文
- ✅ パーサー: `int[5] arr;` 形式の配列宣言
- ✅ パーサー: `Point[3] points;` 構造体配列宣言
- ✅ パーサー: `Vec<int>[10] vecs;` ジェネリック型配列宣言
- ✅ パーサー: 配列インデックスアクセス `arr[i]`
- ✅ `is_type_start()`: `Type[N] name` パターンの認識

#### MIR Lowering
- ✅ 配列変数への直接参照（コピー防止）
- ✅ 配列インデックス代入: `arr[i] = value`
- ✅ 配列インデックス読み出し: `value = arr[i]`

#### インタプリタ
- ✅ ArrayValue型の実装
- ✅ 配列初期化（要素型に応じた初期値）
- ✅ Indexプロジェクションのload/store
- ✅ 構造体配列のサポート
- ✅ PointerValue型の実装
- ✅ Derefプロジェクションのload/store

#### LLVMバックエンド
- ✅ 配列型の変換（`[N x T]`）
- ✅ GEPによる配列要素アクセス
- ✅ インデックス型変換（i32→i64拡張）
- ✅ プリミティブ型配列のコンパイル
- ✅ 構造体配列のコンパイル

#### WASMバックエンド
- ✅ プリミティブ型配列のWASMコンパイル
- ✅ 構造体配列のWASMコンパイル

#### ポインタ型
- ✅ パーサー: `int* p;` C++スタイルポインタ宣言
- ✅ パーサー: `*p` デリファレンス式と型宣言の区別
- ✅ MIR: `Ref` Rvalue（アドレス取得）
- ✅ MIR: `Deref` プロジェクション（デリファレンス）
- ✅ インタプリタ: ポインタの読み書き
- ✅ LLVMバックエンド: ポインタ型のネイティブコンパイル
- ✅ WASMバックエンド: ポインタ型のWASMコンパイル

#### 配列→ポインタ暗黙変換（Array Decay）
- ✅ 型チェッカー: `int[N]` → `int*` の暗黙変換
- ✅ MIR: `Ref` with Indexプロジェクションで `&arr[0]` を生成
- ✅ インタプリタ: 配列要素へのポインタサポート
- ✅ LLVM: GEPによる配列要素アドレス取得
- ✅ WASM: 配列→ポインタ変換

### 実装項目
| 機能 | パーサー | 型チェック | HIR/MIR | インタプリタ | LLVM | WASM | テスト |
|------|----------|-----------|---------|-------------|------|------|------|
| 固定長配列 T[N] | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 構造体配列 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| ポインタ T* | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| アドレス &Val | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 配列→ポインタ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| typedef型ポインタ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 可変長配列 T[] | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| 文字列操作 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| スライス操作[f:e] | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| イテレータ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |

### テスト結果（2024年12月15日）
- インタプリタ: 全110テストパス
- LLVM Native: ポインタ完全サポート（typedef型ポインタ含む）
- WASM: ポインタ完全サポート（typedef型ポインタ含む）
- 新規テスト追加:
  - `tests/test_programs/array/array_basic.cm`
  - `tests/test_programs/array/array_struct.cm`
  - `tests/test_programs/pointer/pointer_basic.cm`
  - `tests/test_programs/pointer/pointer_struct.cm`
  - `tests/test_programs/pointer/pointer_typedef.cm`
  - `tests/test_programs/pointer/pointer_array_decay.cm`

### 完了した修正（2024年12月15日）
- ✅ `resolve_typedef()`: ポインタ型・配列型の要素型を再帰的に解決
  - `*MyInt` → `*int` の自動変換
  - `MyFloat[10]` → `float[10]` の自動変換

### 構文例
```cm
// プリミティブ型配列
int[5] numbers;
numbers[0] = 10;
numbers[1] = 20;
int sum = numbers[0] + numbers[1];

// 構造体配列
struct Point { int x; int y; }
Point[3] points;
points[0].x = 10;
points[0].y = 20;

// ジェネリック型配列（パーサーレベルでサポート）
Vec<int>[10] vectors;
```

---

---

## Version 0.10.0 - モジュールシステム

### 目標
完全なモジュールとパッケージ管理

### 実装項目
| 機能 | パーサー | 型チェック | HIR/MIR | インタプリタ | LLVM | WASM | テスト |
|------|----------|-----------|---------|-------------|------|------|------|
| import/export | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| モジュール解決 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| 標準ライブラリ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| constexpr | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| macro | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| genパッケージマネージャ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| 依存関係管理 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |

---

## Version 0.11.0 - FFIとインラインアセンブリ

### 目標
外部関数インターフェースとインラインアセンブリによる低レベルアクセス

### 実装項目
| 機能 | パーサー | 型チェック | HIR/MIR | インタプリタ | LLVM | WASM | テスト |
|------|----------|-----------|---------|-------------|------|------|------|
| extern "C" ブロック | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| FFI関数宣言 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| インラインアセンブリ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| 型マーシャリング | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| プラットフォーム固有コード | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |

### 構文例
```cm
// C関数の宣言
extern "C" {
    void* malloc(size_t size);
    void free(void* ptr);
    int printf(const char* format, ...);
}

// インラインアセンブリ
unsafe int get_cpu_id() {
    int eax, ebx, ecx, edx;
    asm {
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0)
    };
    return eax;
}

// プラットフォーム固有実装
#[cfg(target_os = "linux")]
void platform_specific() {
    // Linux固有の処理
}

#[cfg(target_os = "windows")]
void platform_specific() {
    // Windows固有の処理
}
```

### 用途
- システムライブラリとの連携
- パフォーマンスクリティカルなコード
- ハードウェア固有機能へのアクセス
- 既存Cライブラリの利用

---

## Version 0.12.0 - 非同期処理

### 目標
async/awaitとFuture型の実装

### 実装項目
| 機能 | パーサー | 型チェック | HIR/MIR | インタプリタ | LLVM | WASM | テスト |
|------|----------|-----------|---------|-------------|------|------|------|
| async関数 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| await式 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| Future型 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| ランタイム統合 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| 並行処理 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |

---

## Version 1.0.0 - 安定版リリース

### 達成条件
- ✅ すべてのコア機能の実装完了
- ✅ インタプリタ・LLVM・WASMバックエンドの動作一致
- ✅ 包括的なテストスイート（>90%カバレッジ）
- ✅ ドキュメント完備
- ✅ genパッケージマネージャ完成
- ✅ エディタサポート（VSCode, Vim）
- ✅ 標準ライブラリ安定版

### 最終チェックリスト
- [ ] 言語仕様書の完成
- [ ] APIドキュメント
- [ ] チュートリアル
- [ ] サンプルプロジェクト
- [ ] パフォーマンスベンチマーク
- [ ] セキュリティ監査

## 並行開発の原則

### 1. 機能実装フロー
```
1. 言語機能の仕様策定
2. テストケース作成（期待される動作の定義）
3. インタプリタ実装
4. LLVMバックエンド実装
5. 両実装の出力比較テスト
6. 不一致があれば修正
7. ドキュメント更新
```

### 2. テスト戦略
- **ゴールデンテスト**: 同一入力に対する出力の完全一致確認
- **プロパティベーステスト**: ランダム入力での動作一致
- **境界値テスト**: エッジケースでの動作確認
- **パフォーマンステスト**: 実行速度の比較

### 3. CI/CDパイプライン
```yaml
test:
  - stage: unit_test
    - interpreter_tests
    - llvm_tests
  - stage: integration_test
    - compare_outputs
    - golden_tests
  - stage: benchmark
    - performance_comparison
```

## リスクと緩和策

| リスク | 影響度 | 緩和策 |
|--------|-------|--------|
| インタプリタ/LLVMの動作不一致 | 高 | 各機能実装時に徹底的な比較テスト |
| パフォーマンス差が大きい | 中 | 最適化は別フェーズで実施 |
| 仕様変更による手戻り | 中 | 早期のプロトタイプと検証 |
| 依存関係の複雑化 | 低 | モジュール間の疎結合設計 |

## 次のアクション（v0.7.0エラーハンドリングに向けて）

1. **Result<T, E>型の実装** - 1週間
   - ジェネリック構造体Resultの標準ライブラリ定義
   - Ok/Errコンストラクタの実装
   - パターンマッチングとの連携準備

2. **Option<T>型の実装** - 1週間
   - Some/Noneコンストラクタの実装
   - null安全性の保証

3. **?演算子（エラー伝播）** - 2週間
   - パーサー拡張
   - HIR/MIRでの伝播ロジック
   - 各バックエンドでの実装

4. **パニックハンドリング** - 1週間
   - panic!マクロ
   - スタックトレース

**目標リリース日**: 2025年2月中旬

---

## 廃止された機能

以下の機能は2024年12月に廃止されました：

- **Rustトランスパイラ** (`--emit-rust`): LLVMバックエンドに置き換え
- **TypeScriptトランスパイラ** (`--emit-ts`): LLVMバックエンドに置き換え
- **C++トランスパイラ** (`--emit-cpp`): LLVMバックエンドに置き換え

これらの機能のソースコードは削除されました。

---

## 設計原則（重要）

### メソッドは必ずinterfaceを通じて定義
```cm
// ❌ 間違い：構造体に直接メソッドを定義
struct Point {
    double x;
    double y;

    void print() { }  // これは許可されない
}

// ✅ 正しい：interfaceを通じて定義
interface Printable {
    void print();
}

struct Point {
    double x;
    double y;
}

impl Point for Printable {
    void print() {
        println("({}, {})", self.x, self.y);
    }
}
```

### self と this の使い分け
- `self`: implブロック内でのインスタンス参照
- `this`: コンストラクタ/デストラクタ内でのインスタンス参照

```cm
impl<T> Vec<T> {
    self() {
        this.data = null;  // コンストラクタ内では this
    }
}

impl<T> Vec<T> for Container<T> {
    void push(T item) {
        self.data[self.size++] = item;  // メソッド内では self
    }
}
```