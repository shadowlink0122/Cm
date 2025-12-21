# Cm言語 開発ロードマップ

## 開発方針

> **LLVMベースアーキテクチャ**: 2024年12月より、LLVM IRを唯一のコード生成バックエンドとして採用。
> Rust/TypeScript/C++へのトランスパイルは今後行いません。

## Version 0.1.0 ✅

### 完成済み機能
- ✅ 基本的な式と文の実行（インタプリタ）
- ✅ フォーマット文字列（変数自動キャプチャ）
  - ✅ ポインタデリファレンス補間 `{*ptr}` （2024年12月19日追加）
  - ✅ アドレス補間 `{&variable}`
  - ✅ フォーマット指定子（`:x`, `:X`, `:b`, `:o`）
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

## Version 0.9.0 - 配列とコレクション ✅ 完了

### 目標
動的・静的配列とベクター型の実装（ジェネリクスを活用）

### 完了した作業（2024年12月15日最終更新）

#### 新機能（この更新で追加）

##### 明示的operator実装のLLVM float型対応
- ✅ 型推論の修正: HIRの型情報がoperator実装内の式に伝播されていない問題を修正
- ✅ MIR型推論の強化: `is_error()` 型もフォールバックとして扱うように変更
- ✅ LLVM float/double型変換: 異なる浮動小数点型間の演算時に自動で型変換を行うように修正
- ✅ FNeg演算: 浮動小数点の単項マイナスを正しく `fneg` 命令で処理するように修正

##### ufloat/udouble プリミティブ型
- ✅ ufloat (32bit): 非負制約付き単精度浮動小数点型
- ✅ udouble (64bit): 非負制約付き倍精度浮動小数点型
- ✅ レキサー・パーサー対応: `ufloat`, `udouble` キーワードの認識
- ✅ LLVM対応: float/doubleと同じネイティブ表現

##### デッドコード削除
- ✅ プログラムレベルDCE: 未使用の自動実装関数を自動的に削除
- ✅ 呼び出しグラフ解析: mainから到達可能な関数のみを保持
- ✅ 未使用構造体削除: 使用されない構造体定義の削除

##### インターフェース境界の統一
- ✅ 構文の統一: `<T: Interface>`, `where T: Interface` ですべてインターフェース境界として解釈
- ✅ AND境界: `T: I + J` でTはIとJの両方を実装している型
- ✅ OR境界: `T: I | J` でTはIまたはJを実装している型
- ✅ 具体型境界の廃止: 型境界は具体的な型ではなくインターフェースのみを許容
- ✅ ConstraintKind変更: `None`, `Single`, `And`, `Or` の4種類に整理

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
| 配列.size()/.len() | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| ポインタ T* | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| アドレス &Val | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 配列→ポインタ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| typedef型ポインタ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 範囲for (for-in) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 関数ポインタ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 文字列操作(.len()等) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 文字列インデックス s[i] | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 文字列スライス s[a:b] | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 配列高級関数(.some等) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| operatorキーワード | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| whereキーワード | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| with自動実装(Eq) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| with自動実装(Ord) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| with自動実装(Clone) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| with自動実装(Hash) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| ジェネリック構造体with | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| デッドコード削除 | - | - | ✅ | - | ✅ | ✅ | ✅ |
| インターフェース境界統一 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| ufloat/udouble型 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

### 実装完了（2024年12月15日最終更新）

#### operatorキーワード
- ✅ パーサー: `impl S for Eq { operator bool ==(S other) {...} }` 構文
- ✅ HIR/MIR: 明示的operator実装のlowering
- ✅ インタプリタ: 明示的operator実装の実行
- ✅ LLVM: 整数型・浮動小数点型両対応（float/double型変換修正済み）
- ✅ WASM: 動作確認済み（2024年12月15日）

#### デッドコード削除（実装完了）
- ✅ `ProgramDeadCodeElimination`: プログラム全体の未使用関数を削除
- ✅ 呼び出しグラフ解析: mainから到達可能な関数のみを保持
- ✅ `with Eq`を宣言したが`==`を使用していない場合のoperator関数削除
- ✅ 未使用の構造体定義の削除

### イテレータ（v0.10.0へ移動）
イテレータはラムダ式と動的メモリ確保に依存するため、v0.10.0で実装予定。

### 実装状況

#### 完了（2024年12月15日更新）
- **Lexer**: `operator`, `where`キーワード追加
- **Parser**: `interface I<T>`でoperatorシグネチャをパース
- **Parser**: `impl S for I`でoperator実装をパース
- **Parser**: `where T: Type`句のパース（struct/impl両対応）
- **Parser**: ユニオン型制約 `T: int | string` のパース
- **AST**: `OperatorKind`, `OperatorSig`, `OperatorImpl`, `WhereClause`追加
- **HIR**: 対応するノード追加
- **MIR**: `with Eq`による`==`演算子の自動実装
- **MIR**: `with Ord`による`<`演算子の自動実装
- **MIR**: `with Clone`による`clone()`メソッドの自動実装
- **MIR**: `with Hash`による`hash()`メソッドの自動実装
- **インタプリタ**: 自動実装されたoperator（`==`, `<`）の実行
- **LLVM**: 論理AND/OR演算子の対応（`MirBinaryOp::And`, `MirBinaryOp::Or`）
- **LLVM**: `with Eq`で自動生成された`Point__op_eq`のネイティブコンパイル
- **LLVM**: `with Ord`で自動生成された`Point__op_lt`のネイティブコンパイル
- **TypeChecker**: 組み込みインターフェース(Eq, Ord, Copy, Clone, Hash)の登録
- **デッドコード削除**: プログラム全体の未使用関数・構造体削除（program_dce.hpp）

### `with`キーワードによる自動実装（実装済み）

構造体宣言時に`with`キーワードでインターフェースを自動実装：

```cm
// Eq インターフェース定義
interface Eq<T> {
    operator bool ==(T other);
}

// 基本構文 - Eq自動実装
struct Point with Eq {
    int x;
    int y;
}

// 使用例
Point p1, p2;
p1.x = 10; p1.y = 20;
p2.x = 10; p2.y = 20;
if (p1 == p2) {
    println("Equal!");  // フィールドごとの比較が自動生成される
}
```

### 複数の構造体での自動実装（実装済み）

```cm
struct Color with Eq {
    int r; int g; int b;
}

struct Size with Eq {
    int width; int height;
}
```

### 演算子オーバーロード（interface/impl内限定）

`operator`キーワードはinterface定義とimpl内でのみ使用可能：

```cm
// インターフェース定義（ジェネリクス使用、Selfは使用禁止）
interface Eq<T> {
    operator bool ==(T other);
}

interface Ord<T> {
    operator bool <(T other);
}

interface Add<T, U> {
    operator T +(U other);
}

// 派生演算子はコンパイラが自動導出：
// a != b  →  !(a == b)
// a > b   →  b < a
// a <= b  →  !(b < a)
// a >= b  →  !(a < b)
```

### impl内でのoperator実装

```cm
struct Point { int x; int y; }

// 同じ型との比較（withを使わない場合の明示的実装）
impl Point for Eq {
    operator bool ==(Point other) {
        return self.x == other.x && self.y == other.y;
    }
}
// コンパイラ解釈: impl Point for Eq<Point>

// 異なる型との比較（where句で型制約）
impl<T> Point for Eq<T> where T: Polygon {
    operator bool ==(T other) {
        // PointとPolygonの比較ロジック
        return self.x == other.center_x && self.y == other.center_y;
    }
}

// 算術演算子
impl Point for Add {
    operator Point +(Point other) {
        Point result;
        result.x = self.x + other.x;
        result.y = self.y + other.y;
        return result;
    }
}
// コンパイラ解釈: impl Point for Add<Point, Point>
```

### `with`による自動生成

`with`で自動実装する場合、インターフェースの型パラメータは暗黙的に実装対象の型を使用：

```cm
// 基本ケース: struct S with I → impl S for I （暗黙的にI<S>）
struct Point with Eq {
    int x;
    int y;
}

// 自動生成されるコード:
// impl Point for Eq {
//     operator bool ==(Point other) {
//         return self.x == other.x && self.y == other.y;
//     }
// }
```

### ジェネリック構造体の`with`

```cm
// ジェネリック構造体: struct S<T> with I → impl<T> S<T> for I
struct Pair<T> with Eq {
    T first;
    T second;
}

// 自動生成されるコード:
// impl<T> Pair<T> for Eq {
//     operator bool ==(Pair<T> other) {
//         return self.first == other.first && self.second == other.second;
//     }
// }
```

### 型推論ルール

| 宣言 | 自動生成されるimpl | コンパイラ解釈 |
|------|-------------------|---------------|
| `struct S with Eq` | `impl S for Eq` | `impl S for Eq<S>` |
| `struct S with Ord` | `impl S for Ord` | `impl S for Ord<S>` |
| `struct S<T> with Eq` | `impl<T> S<T> for Eq` | `impl<T> S<T> for Eq<S<T>>` |

### 異なる型との比較（where句）

異なる型との比較が必要な場合は`with`を使わず、where句で明示的に型制約：

```cm
struct Point { int x; int y; }
struct Polygon { int center_x; int center_y; int vertices; }

// PointとPolygonを比較（withは使わない）
impl<T> Point for Eq<T> where T: Polygon {
    operator bool ==(T other) {
        return self.x == other.center_x && self.y == other.center_y;
    }
}

// 使用例
Point p; p.x = 10; p.y = 20;
Polygon poly; poly.center_x = 10; poly.center_y = 20;
if (p == poly) {  // impl<T> Point for Eq<T> where T: Polygon を使用
    println("Same center!");
}
```

### where句の使い分け

### インターフェース境界の構文（2024年12月15日更新）

すべての制約はインターフェース境界として解釈されます：

| 構文 | 意味 |
|------|------|
| `<T: I>` | TはインターフェースIを実装している |
| `<T: I + J>` | TはIとJの両方を実装している（AND） |
| `<T: I \| J>` | TはIまたはJを実装している（OR） |
| `where T: I` | TはインターフェースIを実装している |
| `where T: I + J` | TはIとJの両方を実装している（AND） |
| `where T: I \| J` | TはIまたはJを実装している（OR） |

**注意**: 境界はインターフェースのみを指定します。具体的な型（int, stringなど）を直接境界として使用することはできません。

### 自動実装可能なインターフェース一覧

| インターフェース | 説明 | 生成される演算子/メソッド | 自動実装の条件 | 実装状態 |
|-----------------|------|------------------------|---------------|---------|
| **Eq\<T\>** | 等価比較 | `==` (+ 自動導出`!=`) | 全フィールドがEq | ✅ 完了 |
| **Ord\<T\>** | 順序比較 | `<` (+ 自動導出`>`,`<=`,`>=`) | 全フィールドがOrd | ✅ MIR生成完了 |
| **Copy** | ビット単位コピー | （暗黙コピー許可） | 全フィールドがCopy（ポインタなし） | ✅ マーカー |
| **Clone** | 深いコピー | `.clone()` | 全フィールドがClone | ✅ MIR生成完了 |
| **Hash** | ハッシュ計算 | `.hash()` | 全フィールドがHash | ✅ MIR生成完了 |
| **Debug** | デバッグ出力 | `.debug() -> string` | 全フィールドがDebug | ⬜ v0.10.0予定 |
| **Display** | 文字列化 | `.toString() -> string` | 全フィールドがDisplay | ⬜ v0.10.0予定 |

### Debug/Display実装の依存関係

Debug/Displayインターフェースは以下の機能が必要なため、v0.10.0で実装予定：

1. **型→文字列変換関数**: `int_to_string()`, `float_to_string()`, `bool_to_string()`
2. **文字列連結**: `cm_string_concat`（既存）を繰り返し呼び出し
3. **フォーマット文字列構築**: `"StructName { field1: value1, field2: value2 }"`
4. **動的メモリ確保**: 結果文字列のメモリ管理

#### Debug出力例
```cm
struct Point with Debug {
    int x;
    int y;
}

struct Line with Debug {
    Point start;
    Point end;
}

int main() {
    Point p; p.x = 10; p.y = 20;
    println("{}", p.debug());  // "Point { x: 10, y: 20 }"
    
    Line l;
    l.start.x = 0; l.start.y = 0;
    l.end.x = 100; l.end.y = 100;
    println("{}", l.debug());  // "Line { start: Point { x: 0, y: 0 }, end: Point { x: 100, y: 100 } }"
    return 0;
}
```

### プリミティブ型のインターフェース実装状況

| 型 | Eq | Ord | Copy | Clone | Hash | Debug | Display |
|----|----|----|------|-------|------|-------|---------|
| int/uint | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| float/double | ✓ | ✓ | ✓ | ✓ | ✗ | ✓ | ✓ |
| bool | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| char | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| string | ✓ | ✓ | ✗ | ✓ | ✓ | ✓ | ✓ |
| T* | ✓ | ✓ | ✓ | ✗ | ✓ | ✓ | ✓ |
| T[N] | ✓* | ✓* | ✓* | ✓* | ✓* | ✓ | ✓ |

*: 要素型がインターフェースを実装している場合のみ

### 使用例

```cm
struct Point with Eq, Clone, Debug {
    int x;
    int y;
}

int main() {
    Point p1;
    p1.x = 10; p1.y = 20;
    Point p2 = p1.clone();
    
    if (p1 == p2) {          // operator == を呼び出し
        println("Equal!");
    }
    
    println("{}", p1.debug());  // "Point { x: 10, y: 20 }"
    return 0;
}
```

### 注記: v0.10.0への移動項目
以下の機能は動的メモリ確保が必要なため、v0.10.0で実装予定：
- 配列スライス `arr[a:b]` - 新しい配列を返すため動的メモリが必要
- `.map()`, `.filter()` - 結果として新しい配列を返すため動的メモリが必要
- ラムダ式 - map/filter等で使用するクロージャのため先に実装が必要

### 関数ポインタ（2024年12月15日完全実装）
- ✅ パーサー: `int*(int, int) op;` 新しい簡潔な構文
- ✅ 型チェッカー: 関数ポインタ型の互換性チェック
- ✅ HIR/MIR: 関数参照と間接呼び出しの区別
- ✅ インタプリタ: 関数ポインタ経由の呼び出し完全サポート
- ✅ LLVM Native: 間接呼び出し完全サポート
- ✅ WASM: 間接呼び出し完全サポート（call_indirect使用）
  - 関数ポインタ変数への代入
  - 関数ポインタ経由の呼び出し
  - 高階関数（関数ポインタを引数に取る関数）
  - void戻り値の関数ポインタ

### 文字列操作（2024年12月15日実装）
- ✅ `.len()` / `.length()` / `.size()` - 文字列の長さ
- ✅ `.charAt(index)` / `.at(index)` - 指定位置の文字
- ✅ `.substring(start, end?)` / `.slice(start, end?)` - 部分文字列
- ✅ `.indexOf(substr)` - 部分文字列の位置
- ✅ `.toUpperCase()` - 大文字変換
- ✅ `.toLowerCase()` - 小文字変換
- ✅ `.trim()` - 空白除去
- ✅ `.startsWith(prefix)` - 先頭一致判定
- ✅ `.endsWith(suffix)` - 末尾一致判定
- ✅ `.includes(substr)` / `.contains(substr)` - 部分文字列含有判定
- ✅ `.repeat(count)` - 文字列繰り返し
- ✅ `.replace(from, to)` - 文字列置換（最初の1つ）
- ✅ `s[index]` - 配列構文による文字アクセス
- ✅ インタプリタ、LLVM Native、WASM対応済み

### 文字列スライス（2024年12月15日実装）
Python風のスライス構文をサポート：
- ✅ `s[start:end]` - 基本スライス
- ✅ `s[start:]` - 開始位置から最後まで
- ✅ `s[:end]` - 最初から終了位置まで
- ✅ `s[:]` - 全体コピー
- ✅ `s[-n:-m]` - 負のインデックス（末尾から）

### テスト結果（2024年12月15日更新）
- インタプリタ: 全テストパス
- LLVM Native: 121/122テストパス（1スキップ）
- WASM: 全テストパス
- 新規テスト追加:
  - `tests/test_programs/string/string_methods.cm` - 文字列メソッド
  - `tests/test_programs/string/string_slice.cm` - スライス構文
  - `tests/test_programs/array/array_basic.cm`
  - `tests/test_programs/array/array_struct.cm`
  - `tests/test_programs/array/array_size.cm` - `.size()`/`.len()` メソッド
  - `tests/test_programs/array/array_struct_size.cm` - 構造体配列の`.size()`
  - `tests/test_programs/array/array_for_loop.cm` - forループイテレーション
  - `tests/test_programs/array/array_methods.cm` - 高級関数テスト
  - `tests/test_programs/pointer/pointer_basic.cm`
  - `tests/test_programs/pointer/pointer_struct.cm`
  - `tests/test_programs/pointer/pointer_typedef.cm`
  - `tests/test_programs/pointer/pointer_array_decay.cm`

### 配列メソッド（2024年12月15日追加）
- ✅ `.size()` / `.len()` / `.length()` - 配列のサイズを取得（コンパイル時定数）
- ✅ `.indexOf(value)` - 要素の位置を検索（見つからなければ-1）
- ✅ `.includes(value)` / `.contains(value)` - 要素が含まれているか
- ✅ `.some(predicate)` - いずれかの要素が条件を満たすか
- ✅ `.every(predicate)` - すべての要素が条件を満たすか
- ✅ `.findIndex(predicate)` - 条件を満たす最初の要素のインデックス
- ✅ インタプリタ対応済み（PointerValue/ArrayValue両対応）
- ✅ LLVM Native動作確認済み
- ✅ WASM対応済み（2024年12月15日追加）

### for-in構文（2024年12月15日追加）
- ✅ `for (Type var in arr)` - 型指定あり範囲for
- ✅ `for (var in arr)` - 型推論あり範囲for
- ✅ 構造体配列に対するfor-in
- ✅ インタプリタ、LLVM、WASMすべてで動作確認済み

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

## コードベースリファクタリング計画

### 背景
コンパイラの継続的な開発に伴い、いくつかのファイルが大きくなりすぎてメンテナンス性に影響が出ています。
将来のボトルネックを防ぐため、以下のリファクタリングを計画しています。

### 完了済み（2024年12月21日）

#### 1. builtins.hppの分割 ✅
インタプリタの組み込み関数を責務ごとに分離：
```
builtins.hpp (926行) →
├── builtin_format.hpp (285行) - フォーマットユーティリティ
├── builtin_io.hpp (147行) - I/O関数（println, print等）
├── builtin_string.hpp (261行) - 文字列操作関数
├── builtin_array.hpp (184行) - 配列操作関数
└── builtins.hpp (78行) - メインヘッダー（統合）
```

#### 2. expr_lowering_impl.cppの分割 ✅
MIR式loweringを論理的なグループに分離：
```
expr_lowering_impl.cpp (2507行) →
├── expr_lowering_basic.cpp (538行) - リテラル、変数参照、メンバー、インデックス等
├── expr_lowering_ops.cpp (696行) - 二項演算、単項演算
└── expr_lowering_call.cpp (1298行) - 関数呼び出し、文字列補間
```

#### 3. hir_lowering.hppの分割 ✅
ヘッダーオンリー実装を責務ごとにcppファイルに分割：
```
hir_lowering.hpp (2337行) →
├── hir_lowering_fwd.hpp (95行) - クラス宣言
├── hir_lowering.hpp (5行) - 後方互換性のためのラッパー
├── hir_lowering.cpp (335行) - メインエントリポイント、演算子変換
├── hir_lowering_decl.cpp (343行) - 宣言のlowering（関数、構造体、impl等）
├── hir_lowering_stmt.cpp (394行) - 文のlowering（let、if、for、switch等）
└── hir_lowering_expr.cpp (1066行) - 式のlowering（リテラル、二項演算、メソッド呼び出し等）
```

### 計画中（優先度順）

#### 4. type_checker.hppの分割（高優先度）
**現状**: 2532行（ヘッダーオンリー）
**課題**: ヘッダーオンリーでテンプレートや複雑な依存関係があり、分割には大きな作業が必要
**方針**: クラス宣言と実装を分離し、機能ごとにcppファイルに分割
```
type_checker.hpp (2532行) →
├── type_checker.hpp (~400行) - クラス宣言、パブリックAPI
├── type_checker_registration.cpp (~500行) - register_* 関数
├── type_checker_inference.cpp (~800行) - infer_* 関数（型推論）
├── type_checker_check.cpp (~500行) - check_* 関数（型チェック）
└── type_checker_utils.cpp (~300行) - ユーティリティ関数
```

#### 5. mir/lowering/の分割（✅ 完了）
**現状**: 分割済み
**実装済み**: 
```
src/mir/lowering/
├── lowering.hpp (~1200行) - メインヘッダー、クラス宣言
├── lowering_impl.cpp - 演算子実装
├── lowering_base.hpp - 基本lowering機能
├── lowering_context.hpp - loweringコンテキスト
├── stmt_lowering.hpp / stmt_lowering_impl.cpp - 文のlowering
├── expr_lowering.hpp - 式lowering共通ヘッダー
├── expr_lowering_basic.cpp - 基本式のlowering
├── expr_lowering_ops.cpp - 演算子のlowering
├── expr_lowering_call.cpp - 関数呼び出しのlowering
└── monomorphization*.cpp - ジェネリクス特殊化
```

#### 6. interpreter.hppの分割（中優先度）
**現状**: 601行
**方針**: eval部分を別ファイルに分離
```
interpreter.hpp (601行) →
├── interpreter.hpp (~200行) - クラス定義、メイン実行ロジック
├── interpreter_eval.cpp (~200行) - 式評価
└── interpreter_stmt.cpp (~200行) - 文実行
```

#### 7. monomorphization_impl.cppの分割（低優先度）
**現状**: 1201行
**方針**: 関数と構造体のモノモーフィゼーションを分離
```
monomorphization_impl.cpp (1201行) →
├── monomorphization_func.cpp (~600行) - 関数のモノモーフィゼーション
└── monomorphization_struct.cpp (~600行) - 構造体のモノモーフィゼーション
```

### 技術的課題

1. **ヘッダーオンリー実装の分割**
   - テンプレートクラスはヘッダーに残す必要がある
   - 複雑な依存関係の解決が必要
   - コンパイル時間とリンク時間のバランス

2. **MIR型の統一**
   - mir_lowering.hppとmir_nodes.hppで異なる型定義が使用されている
   - 将来的に統一が必要

### ガイドライン

1. **1ファイル1000行目安**: それ以上になる場合は分割を検討
2. **単一責任の原則**: 各ファイルは1つの責務に集中
3. **依存関係の最小化**: 循環依存を避ける
4. **テストの維持**: リファクタリング後も全テストがパスすることを確認
5. **段階的な実施**: 一度に大きな変更をせず、小さな変更を積み重ねる

### リファクタリング実施時の手順

1. バックアップを作成
2. 分割対象の関数境界を特定
3. 新しいファイルを作成し、関数を移動
4. ヘッダーに宣言を追加（必要に応じて）
5. CMakeLists.txtを更新
6. ビルドして重複定義エラーがないことを確認
7. 全テストを実行して動作確認
8. 元のファイルを削除（または不要部分を削除）

---

## Version 0.10.1 - 文字列補間拡張 ✅ (2024年12月19日完了)

### 実装済み機能
- ✅ ポインタデリファレンス補間 `{*ptr}`
  - MIRレベルでのデリファレンス処理
  - すべてのバックエンド（インタープリタ、Native、WASM）での統一動作
- ✅ フォーマット仕様の統一
  - 整数の16進数表示 `{n:x}` → `ff` （0xプレフィックスなし）
  - ポインタ/アドレスの16進数表示 `{&x:x}` → `0x1234abcd` （0xプレフィックスあり）
  - 既存テストとの互換性維持

### 使用例
```cm
int x = 255;
int* ptr = &x;

// デリファレンス補間
println("Value via ptr: {*ptr}");          // "Value via ptr: 255"
println("*ptr in hex: {*ptr:x}");          // "*ptr in hex: ff"

// アドレス補間
println("Address of x: {&x}");             // "Address of x: 12981144612" (デフォルト10進数)
println("Address in hex: {&x:x}");         // "Address in hex: 0x305bc8c24"
```

## Version 0.10.0 - 型システム強化とモジュールシステム（進行中）

### 現在の優先タスク（2024-12-20）

#### ✅ 完了: 型システム
- [x] **関数戻り値の型チェック**
  - 確認: 型不一致は正しく検出されている
  - テスト: `tests/test_programs/type_checking/function_return_type_mismatch.cm`
  - 結果: `Type mismatch in variable declaration` エラーが表示される ✅

#### ✅ 完了: グローバルconst変数の文字列補間（2024-12-20）
- [x] **グローバルconst変数の文字列補間を実装**
  - 修正: `src/mir/lowering/expr_lowering_impl.cpp` (57-82行目)
  - 実装: `ctx.get_const_value()` を使用してグローバルconst変数を解決
  - テスト: `tests/test_programs/const_interpolation/`
    - `global_const.cm`: ✅ PASS
    - `mixed_const.cm`: ✅ PASS
  - 動作: グローバルとローカル両方のconst変数が正しく補間される ✅

#### ✅ 完了: モジュールシステム実装（Phase 1）

**状態**: 基本機能完了 (2024-12-20)

**完了した作業**:
- ✅ 最終設計文書作成（`MODULE_SYSTEM_FINAL.md` v5.0）
- ✅ 実装計画作成（`MODULE_IMPLEMENTATION_PLAN.md`）
- ✅ 階層的インポート (`import std::io`) の解決
  - ルートモジュール（`std.cm`）を最初に探す
  - サブモジュール（`./io`）の再帰的インポート
- ✅ 再エクスポート (`export { io }`) の処理
  - 子モジュールを親namespace内に配置
  - `export { M };` 構文の認識
- ✅ ネストしたnamespaceの生成
  - `namespace std { namespace io { ... } }` を正しく生成
- ✅ 名前空間付き関数呼び出し
  - `std::io::add()` のような完全修飾名呼び出しが動作
  - HIRで`is_indirect`を正しく判定

**テスト結果**:
- `tests/test_programs/modules/basic_reexport/main.cm`: ✅ PASS
- `tests/test_programs/modules/hierarchical/main.cm`: ✅ PASS
- `tests/test_programs/modules/test_simple.cm`: ✅ PASS
- `tests/test_programs/modules/test_export_syntax.cm`: ✅ PASS

**残りの作業（Phase 2/3へ）**:
  - [ ] 深い階層テスト
  - [ ] エラーケーステスト
  - [ ] ユーザーガイドの作成

**参考文書**:
- 設計: `docs/design/MODULE_SYSTEM_FINAL.md`
- 実装計画: `docs/design/MODULE_IMPLEMENTATION_PLAN.md`
- テスト: `tests/test_programs/modules/`

### 現在の状況
- **文字列補間の改善**: ✅ 完了（2024年12月20日）
  - ローカルconst変数の補間: ✅ 動作
  - グローバルconst変数の補間: ✅ 動作
- **型システム**: ✅ 問題解決
  - 関数戻り値の型不一致が正しく検出される
- **モジュールシステム**: ✅ Phase 1完了（2024年12月20日）
  - 階層的インポート（`import std::io;`）: ✅ 動作
  - 再エクスポート（`export { io };`）: ✅ 動作
  - ネストした名前空間生成: ✅ 動作
  - 名前空間付き関数呼び出し: ✅ 動作
- **インラインアセンブリ**: ⬜ 未実装

### 目標
型システムを強化し、モジュールシステム（import/export）を実装してプロジェクト分割を可能にする。

### v0.9.0制約の完了（完了: 2024年12月16日）
| 機能 | パーサー | 型チェック | HIR/MIR | インタプリタ | LLVM | WASM | テスト |
|------|----------|-----------|---------|-------------|------|------|------|
| typedef型ポインタ（LLVM） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| typedef型ポインタ（WASM） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| with自動実装（WASM） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

### モジュールシステム（プリプロセッサベース実装 - Phase 1完了）

#### 実装アプローチ
パース前のプリプロセッサステップとしてモジュールシステムを実装：
- importされたモジュールのコードを直接インライン展開
- exportキーワードを削除して通常の定義として処理
- ネストした名前空間を自動生成
- シンプルで確実な動作

#### Phase 1-3 完了機能（2024-12-21 最終更新）
| 機能 | パーサー | 型チェック | HIR/MIR | インタプリタ | LLVM | WASM | テスト |
|------|----------|-----------|---------|-------------|------|------|------|
| export文パース（基本） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| export関数（単一ファイル） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| export定数（単一ファイル） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| export構造体 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 再エクスポート（export { M }） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 階層的インポート（std::io） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| ネストした名前空間生成 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 名前空間付き関数呼び出し | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 名前空間付き型宣言（ns::Type var） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 相対パスインポート（./） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 絶対パスインポート（std::io） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| ディレクトリベースモジュール | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 循環依存検出 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| エイリアスインポート（as） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 深い階層（3+レベル） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| ディレクトリベース名前空間 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| ワイルドカードインポート（./path/*） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 選択的インポート（::{items}） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| サブモジュールインポート（./path/std::io） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| サブモジュール直接インポート（./path/std::io::func） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| サブモジュールワイルドカード（./path/std::io::*） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

#### implの可視性ルール（2024-12-21 決定）
| ルール | 状態 | 備考 |
|--------|------|------|
| implは暗黙的エクスポート | ✅ | 構造体がexportされていればimplも公開 |
| impl上書き禁止 | ✅ | 同じ型への同じインターフェース実装は1回のみ |
| メソッド名重複禁止 | ✅ | 異なるインターフェースでも同名メソッドは禁止 |

#### 将来の拡張（オプショナル）
| 機能 | 状態 | 備考 |
|------|------|------|
| 階層再構築エクスポート（io::{file}） | ✅ | 完全サポート |
| モジュール間の型推論 | ⬜ | v0.11.0以降で検討 |

### テスト結果（2024-12-21 最終確認）

全てのバックエンドで **165 passed, 0 failed**：

| テスト | インタプリタ | LLVM Native | WASM |
|--------|-------------|-------------|------|
| basic_reexport | ✅ | ✅ | ✅ |
| hierarchical | ✅ | ✅ | ✅ |
| deep_hierarchy | ✅ | ✅ | ✅ |
| alias_import | ✅ | ✅ | ✅ |
| nested_namespace | ✅ | ✅ | ✅ |
| hier_import | ✅ | ✅ | ✅ |
| wildcard_submodule | ✅ | ✅ | ✅ |
| impl_export | ✅ | ✅ | ✅ |
| std_import | ✅ | ✅ | ✅ |
| ... (他27テスト) | ✅ | ✅ | ✅ |

### 文字列補間の改善（2024年12月20日完了）

v0.9.0で実装された文字列補間機能を拡張し、const変数のサポートを追加：

| 機能 | パーサー | 型チェック | HIR/MIR | インタプリタ | LLVM | WASM | テスト |
|------|----------|-----------|---------|-------------|------|------|------|
| 関数ローカルconst変数補間 | ✅ | ✅ | ✅ | ✅ | ⬜ | ⬜ | ✅ |
| モジュールレベルexport const補間 | ✅ | ✅ | ✅ | ✅ | ⬜ | ⬜ | ✅ |
| 混合const変数補間 | ✅ | ✅ | ✅ | ✅ | ⬜ | ⬜ | ✅ |

実装の詳細：
- MIR lowering時にconst変数を正しく解決
- 関数スコープとモジュールスコープの両方をサポート
- 既存の変数補間機能との統合

### インラインアセンブリ（未実装）
| 機能 | パーサー | 型チェック | HIR/MIR | インタプリタ | LLVM | WASM | テスト |
|------|----------|-----------|---------|-------------|------|------|------|
| asm("code") 式 | ⬜ | ⬜ | ⬜ | - | ⬜ | - | ⬜ |
| 文字列リテラル形式 | ⬜ | ⬜ | ⬜ | - | ⬜ | - | ⬜ |
| LLVM IR埋め込み | ⬜ | ⬜ | ⬜ | - | ⬜ | - | ⬜ |

注: パーサーレベルでの準備コードは存在するが、完全な実装はまだ行われていない。

### 構造体拡張（2024-12-21 実装完了）
| 機能 | パーサー | 型チェック | HIR/MIR | インタプリタ | LLVM | WASM | テスト |
|------|----------|-----------|---------|-------------|------|------|------|
| 構造体メンバの配列 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| ネストした構造体 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 明示的構造体リテラル | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 暗黙的構造体リテラル（型推論） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 配列リテラル | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 構造体の配列リテラル | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 構造体メンバのポインタ | ⚠️ | ⚠️ | ⚠️ | ⚠️ | ⚠️ | ⚠️ | ⬜ |

#### 構文例
```cm
// 暗黙的構造体リテラル（推奨）
Point p = {x: 10, y: 20};

// ネストした暗黙的リテラル
Line line = {
    start: {x: 0, y: 0},
    end: {x: 100, y: 100}
};

// 配列リテラル
int[3] nums = [1, 2, 3];

// 構造体の配列（型推論）
Point[2] points = [
    {x: 10, y: 20},
    {x: 30, y: 40}
];

// 構造体メンバに配列
struct Data {
    int[3] values;
    int count;
}
Data d = {values: [1, 2, 3], count: 3};
```

### モジュールシステム構文（C++風）

Cm言語はC++風の構文を採用するため、モジュールシステムもC++スタイルとします。

```cm
// モジュール宣言（ファイル先頭）
module math.geometry;

// エクスポート（関数）
export int add(int a, int b) {
    return a + b;
}

// エクスポート（構造体）
export struct Point {
    int x;
    int y;
}

// エクスポート（インターフェース）
export interface Drawable {
    void draw();
}

// インポート（単純）
import std.io;

// インポート（選択的）
import std.collections.{Vec, HashMap};

// インポート（エイリアス）
import std.collections.HashMap as Map;

// 使用例
int main() {
    std.io.println("Hello");
    Vec<int> v;
    Map<string, int> m;
    return 0;
}
```

### インラインアセンブリ構文（シンプル）

インラインアセンブリは文字列リテラルで記述し、パーサーは不要です。

```cm
// 基本構文: asm("アセンブリコード");
void example() {
    asm("nop");
    asm("mov rax, 1");
}

// asmパッケージから公開（std.asmモジュール）
module std.asm;

export void nop() {
    asm("nop");
}

export void cpuid() {
    asm("cpuid");
}

// 使用例
import std.asm.{nop, cpuid};

int main() {
    nop();
    cpuid();
    return 0;
}
```

### v0.11.0へ移動（ラムダ式と動的メモリは次バージョン）
以下の機能は動的メモリ確保が必要なため、v0.11.0で実装予定：
- ラムダ式 `|args| expr`
- クロージャ（変数キャプチャ）
- 可変長配列 `T[]`
- `new T[n]` 動的確保
- `delete` 解放
- `Vec<T>` 型
- 配列スライス `arr[a:b]`
- `.map(fn)`, `.filter(fn)`, `.reduce(fn, init)`
- イテレータ
- Debug/Display自動実装
- 型→文字列変換関数

### Debug/Display自動実装

Debug/Displayインターフェースは動的メモリ確保が必要なため、このバージョンで実装：

```cm
interface Debug {
    string debug();
}

interface Display {
    string toString();
}

// 自動実装例
struct Point with Debug, Display {
    int x;
    int y;
}

// 自動生成されるコード:
// impl Point for Debug {
//     string debug() {
//         return "Point { x: " + int_to_string(self.x) + ", y: " + int_to_string(self.y) + " }";
//     }
// }
// impl Point for Display {
//     string toString() {
//         return "(" + int_to_string(self.x) + ", " + int_to_string(self.y) + ")";
//     }
// }

int main() {
    Point p; p.x = 10; p.y = 20;
    println("{}", p.debug());     // "Point { x: 10, y: 20 }"
    println("{}", p.toString());  // "(10, 20)"
    return 0;
}
```

### ネストした構造体のDebug
```cm
struct Line with Debug {
    Point start;
    Point end;
}

// 自動生成（ネスト対応）:
// string debug() {
//     return "Line { start: " + self.start.debug() + ", end: " + self.end.debug() + " }";
// }

Line l;
println("{}", l.debug());
// "Line { start: Point { x: 0, y: 0 }, end: Point { x: 0, y: 0 } }"
```

### 型→文字列変換関数（組み込み）
以下の関数を組み込みとして提供：
- `int_to_string(int) -> string`
- `uint_to_string(uint) -> string`
- `float_to_string(float) -> string`
- `double_to_string(double) -> string`
- `bool_to_string(bool) -> string`
- `char_to_string(char) -> string`

### ラムダ式の構文
```cm
// 基本構文: |params| expr または |params| { statements }
int[5] arr = [1, 2, 3, 4, 5];

// 型推論あり
arr.map(|x| x * 2);                    // [2, 4, 6, 8, 10]
arr.filter(|x| x > 2);                 // [3, 4, 5]
arr.reduce(|acc, x| acc + x, 0);       // 15

// 明示的な型
arr.map(|int x| -> int { return x * 2; });

// クロージャ（外部変数のキャプチャ）
int multiplier = 3;
arr.map(|x| x * multiplier);           // [3, 6, 9, 12, 15]
```

### 動的メモリ確保
```cm
// 可変長配列（スタック上、allocaベース）
void example(int n) {
    int[] arr = new int[n];  // 実行時サイズ
    for (int i = 0; i < n; i++) {
        arr[i] = i * 2;
    }
    delete arr;
}

// Vec<T>（ヒープ、自動解放）
Vec<int> vec;
vec.push(1);
vec.push(2);
// スコープ終了時に自動解放
```

### 配列スライス（動的メモリ依存）
```cm
int[10] arr = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9];

// スライスは新しい配列を返す（動的メモリ確保）
int[] slice = arr[2:5];      // [2, 3, 4]
int[] tail = arr[5:];        // [5, 6, 7, 8, 9]
int[] head = arr[:3];        // [0, 1, 2]
int[] last3 = arr[-3:];      // [7, 8, 9]
```

### 高級関数（動的メモリ依存）
```cm
int[5] numbers = [1, 2, 3, 4, 5];

// map: 新しい配列を返す
int[] doubled = numbers.map(|x| x * 2);  // [2, 4, 6, 8, 10]

// filter: 新しい配列を返す
int[] evens = numbers.filter(|x| x % 2 == 0);  // [2, 4]

// reduce: 単一の値を返す（既にv0.9.0で実装済み）
int sum = numbers.reduce(|acc, x| acc + x, 0);  // 15

// チェーン
int result = numbers
    .filter(|x| x > 2)
    .map(|x| x * 10)
    .reduce(|acc, x| acc + x, 0);  // 120
```

### 実装の依存関係
```
ラムダ式
    ↓
クロージャ（変数キャプチャ）
    ↓
動的メモリ確保 (new/delete)
    ↓
┌───────────────┬───────────────┬───────────────┐
│  配列スライス  │     .map()    │   .filter()   │
│   arr[a:b]    │               │               │
└───────────────┴───────────────┴───────────────┘
```

---

## Version 0.11.0 - ラムダ式と動的メモリ確保

### 目標
ラムダ式と動的メモリ確保を実装し、高度なコレクション操作を可能にする。

### ラムダ式と動的メモリ
| 機能 | パーサー | 型チェック | HIR/MIR | インタプリタ | LLVM | WASM | テスト |
|------|----------|-----------|---------|-------------|------|------|------|
| ラムダ式 \|args\| expr | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| クロージャ（変数キャプチャ） | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| 可変長配列 T[] | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| new T[n] 動的確保 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| delete 解放 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| Vec<T> 型 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| 配列スライス arr[a:b] | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| .map(fn) | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| .filter(fn) | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| .reduce(fn, init) | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| イテレータ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| Debug自動実装 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| Display自動実装 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| 型→文字列変換関数 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |

### 構文例
```cm
// 参照型（読み取り専用）
void print_point(&Point p) {
    println("({}, {})", p.x, p.y);
}

// ポインタ経由で変更（明示的）
void increment(int* x) {
    *x = *x + 1;
}

// 所有権移動
struct Buffer { int* data; int size; }

void consume(move Buffer buf) {
    // bufの所有権を取得
    // 関数終了時にbufは破棄される
}

int main() {
    Buffer b;
    consume(move b);  // 所有権を移動
    // b はここで使用不可（コンパイルエラー）
    return 0;
}
```

---

## Version 0.12.0 - 参照と所有権

### 目標
参照型とmoveセマンティクスの実装（メモリ安全性の基盤）

### 設計方針
- **const以外は全て可変**（`mut`キーワードは不要）
- **ポインタ（T*）**: C互換、手動管理、unsafe
- **参照（&T）**: 借用チェック付き、安全、読み取り専用
- **move**: 所有権の明示的移動

### 実装項目
| 機能 | パーサー | 型チェック | HIR/MIR | インタプリタ | LLVM | WASM | テスト |
|------|----------|-----------|---------|-------------|------|------|------|
| &T 参照型 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| move キーワード | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| 借用チェッカー | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| ライフタイム基礎 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| Drop trait | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |

---

## Version 0.13.0 - constexprとマクロシステム

### 目標
コンパイル時計算とマクロによるメタプログラミング

### 実装項目
| 機能 | パーサー | 型チェック | HIR/MIR | インタプリタ | LLVM | WASM | テスト |
|------|----------|-----------|---------|-------------|------|------|------|
| constexpr | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| #macro定義 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| マクロ展開 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| 衛生的マクロ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| genパッケージマネージャ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| 依存関係管理 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |

---

## Version 0.14.0 - FFIとインラインアセンブリ (moved from v0.13.0)

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

## Version 0.14.0 - WASMフロントエンド

### 目標
ブラウザ環境でのCmアプリケーション実行基盤

### 実装項目
| 機能 | パーサー | 型チェック | HIR/MIR | ビルドツール | ランタイム | テスト |
|------|----------|-----------|---------|-------------|-----------|------|
| JavaScript FFI | ✅ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| DOM操作API | ✅ | ⬜ | ⬜ | - | ⬜ | ⬜ |
| WASMローダー生成 | - | - | - | ⬜ | ⬜ | ⬜ |
| HTMLテンプレート生成 | - | - | - | ⬜ | - | ⬜ |
| Web専用ランタイム | - | - | - | - | ⬜ | ⬜ |
| イベントリスナー | ✅ | ⬜ | ⬜ | - | ⬜ | ⬜ |
| 文字列変換（JS↔WASM） | - | - | - | - | ⬜ | ⬜ |

### 構文例
```cm
// JavaScript FFI
extern "js" {
    void console_log(string message);
    void alert(string message);
}

// DOM操作
interface Document {
    Element getElementById(string id);
}

interface Element {
    void setText(string text);
    void addEventListener(string event, void*(void*) callback);
}

int main() {
    console_log("Hello from Cm!");
    
    Document doc = get_document();
    Element btn = doc.getElementById("myButton");
    btn.addEventListener("click", |event| {
        alert("Button clicked!");
    });
    
    return 0;
}
```

### ビルドコマンド
```bash
# WASM + JavaScript + HTML を生成
cm build --target=wasm-web app.cm -o dist/

# 生成されるファイル
# - dist/app.wasm
# - dist/app.js (WASMローダー)
# - dist/index.html
```

### 開発サーバー
```bash
cm serve --target=wasm-web
# → http://localhost:3000 でアプリケーションを起動
```

---

## Version 0.15.0 - 非同期処理

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
| Promise統合（WASM） | ⬜ | ⬜ | ⬜ | - | - | ⬜ | ⬜ |

---

---

## Version 0.16.0 - ベアメタル・OSサポート

### 目標
UEFI/ベアメタル環境でのOS開発サポート

### 実装項目
| 機能 | パーサー | 型チェック | HIR/MIR | インタプリタ | LLVM | WASM | テスト |
|------|----------|-----------|---------|-------------|------|------|------|
| #[no_std] 属性 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | - | ⬜ |
| #[no_main] 属性 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | - | ⬜ |
| volatile read/write | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | - | ⬜ |
| packed構造体 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | - | ⬜ |
| カスタムリンカスクリプト | - | - | - | - | ⬜ | - | ⬜ |
| UEFIターゲット | - | - | - | - | ⬜ | - | ⬜ |
| freestanding環境 | - | - | - | - | ⬜ | - | ⬜ |

### UEFI Hello World に必要な機能

#### 1. 言語機能
- ✅ ポインタ型（`void*`, `T*`）
- ✅ 構造体（アライメント制御）
- ⬜ packed構造体（`#[repr(packed)]`）
- ⬜ volatile操作
- ⬜ カスタムエントリポイント

#### 2. コンパイル機能
- ⬜ `#[no_std]` - 標準ライブラリなしコンパイル
- ⬜ `#[no_main]` - mainなしコンパイル
- ⬜ カスタムターゲット（PE32+/COFF for UEFI）
- ⬜ リンカスクリプト指定

#### 3. FFI機能
- ⬜ `extern "efiapi"` 呼び出し規約（UEFI ABI）
- ⬜ UEFI型定義（EFI_STATUS, EFI_HANDLE等）

### 構文例（UEFI Hello World）
```cm
#[no_std]
#[no_main]

// UEFI型定義
typedef EFI_STATUS = uint64;
typedef EFI_HANDLE = void*;
typedef CHAR16 = uint16;

// UEFI構造体
#[repr(C)]
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    void* Reset;
    void* OutputString;
    // ...
}

#[repr(C)]
struct EFI_SYSTEM_TABLE {
    // ...
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut;
    // ...
}

// UEFIエントリポイント
extern "efiapi"
EFI_STATUS efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* system_table) {
    // "Hello, World!\r\n" in UTF-16
    CHAR16[16] message = [0x0048, 0x0065, 0x006C, 0x006C, 0x006F, 0x002C, 
                          0x0020, 0x0057, 0x006F, 0x0072, 0x006C, 0x0064,
                          0x0021, 0x000D, 0x000A, 0x0000];
    
    // 関数ポインタを通じてOutputStringを呼び出し
    system_table->ConOut->OutputString(system_table->ConOut, &message[0]);
    
    return 0;  // EFI_SUCCESS
}
```

### ビルドフロー
```bash
# 1. Cmソースをコンパイル（UEFIターゲット）
cm compile --target=x86_64-unknown-uefi hello.cm -o hello.o

# 2. PE32+実行ファイルにリンク
lld-link /subsystem:efi_application /entry:efi_main /out:hello.efi hello.o

# 3. UEFIシェルまたはQEMUで実行
qemu-system-x86_64 -bios OVMF.fd -drive file=fat:rw:esp/,format=raw
```

### OS非依存の原則
- 標準ライブラリを**OS依存部分**と**OS非依存部分**に分離
- `#[no_std]`環境では`println`等のOS依存機能は使用不可
- 代わりに`core`ライブラリ（OS非依存）の機能のみ使用可能

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
- ✅ 参照型と所有権システム
- ✅ FFIとインラインアセンブリ
- ✅ ベアメタル・OSサポート

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
---

## Version 1.1.0 - パフォーマンス最適化

### 目標
コンパイル時最適化とランタイム性能の向上

### 実装項目
| 機能 | 優先度 | 複雑度 | 状態 |
|------|--------|--------|------|
| 文字列リテラルのlen()定数化 | 高 | 低 | ⬜ |
| 文字列メソッドのインライン展開 | 中 | 中 | ⬜ |
| 文字列プーリング | 中 | 中 | ⬜ |
| 配列境界チェック最適化 | 中 | 中 | ⬜ |
| ループアンローリング | 低 | 高 | ⬜ |
| SIMD文字列操作 | 低 | 高 | ⬜ |

### 最適化ターゲット

#### 1. コンパイル時定数化
```cm
// 現在: ランタイム関数呼び出し
string s = "Hello";
uint len = s.len();  // __builtin_string_len("Hello") を呼び出し

// 最適化後: コンパイル時に解決
string s = "Hello";
uint len = 5;  // 定数に置き換え
```

#### 2. 文字列インターニング
同一文字列リテラルを共有してメモリ使用量を削減：
```cm
string a = "Hello";
string b = "Hello";
// 最適化後: a と b は同じメモリ位置を指す
```

#### 3. 境界チェック最適化
ループ内の配列アクセスでの冗長な境界チェックを除去：
```cm
int[100] arr;
for (int i = 0; i < 100; i++) {
    arr[i] = i;  // 境界チェックは1回だけ
}
```

### パフォーマンス目標
| 操作 | 現在 | 目標 |
|------|------|------|
| len() (リテラル) | ~50ns | 0ns (定数) |
| substring() | ~1μs | ~500ns |
| indexOf() | ~2μs | ~1μs |

### 備考
- 現在の実装は機能的には十分で、一般的なアプリケーションでは問題なく使用可能
- 最適化は後方互換性を保ちながら段階的に実装
- プロファイリングに基づいて優先順位を調整
