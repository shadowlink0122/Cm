# Cm言語 開発ロードマップ

## 開発方針

> **LLVMベースアーキテクチャ**: 2024年12月より、LLVM IRを唯一のコード生成バックエンドとして採用。
> Rust/TypeScript/C++へのトランスパイルは今後行いません。

## Version 0.1.0 (現在) ✅

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
| 機能 | インタプリタ | LLVM | テスト |
|------|-------------|------|--------|
| スコープトラッキング | ✅ | ✅ | ✅ |
| StorageDead自動発行 | ✅ | ✅ | ✅ |
| defer文 | ✅ | ✅ | ✅ |
| ブロック終了時処理 | ✅ | ✅ | ✅ |

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
| 機能 | インタプリタ | LLVM | テスト |
|------|-------------|------|--------|
| interface定義 | ✅ | ✅ | ✅ |
| impl Interface for Type | ✅ | ✅ | ✅ |
| selfキーワード | ✅ | ✅ | ✅ |
| メソッド呼び出し | ✅ | ✅ | ✅ |
| 静的ディスパッチ | ✅ | ✅ | ✅ |
| プリミティブ型へのimpl | ✅ | ✅ | ✅ |

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
| 機能 | インタプリタ | LLVM | テスト |
|------|-------------|------|--------|
| privateメソッド | ✅ | ✅ | ✅ |
| privateアクセスチェック | ✅ | ✅ | ✅ |
| defaultメンバ暗黙代入 | ✅ | ✅ | ✅ |
| defaultメンバ暗黙取得 | ✅ | ✅ | ✅ |
| impl Type（基本impl） | ✅ | ✅ | ✅ |
| self()コンストラクタ | ✅ | ✅ | ✅ |
| overloadコンストラクタ | ✅ | ✅ | ✅ |
| Type name(args)構文 | ✅ | ✅ | ✅ |
| Type name 暗黙的コンストラクタ | ✅ | ✅ | ✅ |
| selfへの参照渡し | ✅ | ✅ | ✅ |
| ~self()デストラクタ | ✅ | ✅ | ✅ |
| RAII自動呼び出し | ✅ | ✅ | ✅ |

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

## Version 0.5.0 - Enum・リテラル型・ユニオン型（現在の目標）

### 目標
高度な型システム基盤の構築（Result/Option型の前提条件）

### 実装項目
| 機能 | インタプリタ | LLVM | テスト |
|------|-------------|------|--------|
| enum定義 | ⬜ | ⬜ | ⬜ |
| enum値アクセス (NAME::MEMBER) | ⬜ | ⬜ | ⬜ |
| enum負の値サポート | ⬜ | ⬜ | ⬜ |
| enum値重複チェック | ⬜ | ⬜ | ⬜ |
| enumオートインクリメント | ⬜ | ⬜ | ⬜ |
| typedef ユニオン型 | ⬜ | ⬜ | ⬜ |
| typedef リテラル型 | ⬜ | ⬜ | ⬜ |
| ユニオン型への構造体/interface | ⬜ | ⬜ | ⬜ |
| 高度な型を引数/戻り値に使用 | ⬜ | ⬜ | ⬜ |

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

## Version 0.6.0 - エラーハンドリングとResult型

### 目標
安全なエラーハンドリングの実装（v0.5.0のEnum/ユニオン型が前提）

### 実装項目
| 機能 | インタプリタ | LLVM | テスト |
|------|-------------|------|--------|
| Result<T, E>型 | ⬜ | ⬜ | ⬜ |
| Option<T>型 | ⬜ | ⬜ | ⬜ |
| ?演算子 | ⬜ | ⬜ | ⬜ |
| パニックハンドリング | ⬜ | ⬜ | ⬜ |

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

## Version 0.7.0 - 配列とコレクション

### 目標
動的・静的配列とベクター型の実装

### 実装項目
| 機能 | インタプリタ | LLVM | テスト |
|------|-------------|------|--------|
| 固定長配列 [T; N] | ⬜ | ⬜ | ⬜ |
| 動的配列 Vec<T> | ⬜ | ⬜ | ⬜ |
| 文字列操作 | ⬜ | ⬜ | ⬜ |
| スライス操作 | ⬜ | ⬜ | ⬜ |
| イテレータ | ⬜ | ⬜ | ⬜ |

## Version 0.8.0 - ジェネリクス

### 目標
型パラメータとジェネリックプログラミング

### 実装項目
| 機能 | インタプリタ | LLVM | テスト |
|------|-------------|------|--------|
| 関数ジェネリクス | ⬜ | ⬜ | ⬜ |
| 構造体ジェネリクス | ⬜ | ⬜ | ⬜ |
| 型制約 where句 | ⬜ | ⬜ | ⬜ |
| 関連型 | ⬜ | ⬜ | ⬜ |
| 型推論強化 | ⬜ | ⬜ | ⬜ |

## Version 0.9.0 - モジュールシステム

### 目標
完全なモジュールとパッケージ管理

### 実装項目
| 機能 | インタプリタ | LLVM | テスト |
|------|-------------|------|--------|
| import/export | ⬜ | ⬜ | ⬜ |
| モジュール解決 | ⬜ | ⬜ | ⬜ |
| 標準ライブラリ | ⬜ | ⬜ | ⬜ |
| genパッケージマネージャ | ⬜ | ⬜ | ⬜ |
| 依存関係管理 | ⬜ | ⬜ | ⬜ |

## Version 0.10.0 - パターンマッチング

### 目標
強力なパターンマッチング機能（v0.5.0のenum/ユニオン型を活用）

### 実装項目
| 機能 | インタプリタ | LLVM | テスト |
|------|-------------|------|--------|
| match式 | ⬜ | ⬜ | ⬜ |
| パターンガード | ⬜ | ⬜ | ⬜ |
| 構造化束縛 | ⬜ | ⬜ | ⬜ |
| 網羅性チェック | ⬜ | ⬜ | ⬜ |

### 構文例
```cm
// enumとのパターンマッチング
enum Color { Red, Green, Blue }

void describe_color(Color c) {
    match (c) {
        Color::Red => println("情熱の赤"),
        Color::Green => println("自然の緑"),
        Color::Blue => println("静寂の青"),
    }
}

// ユニオン型とのパターンマッチング
typedef Shape = Circle | Rectangle;

double area(Shape s) {
    match (s) {
        Circle c => 3.14159 * c.radius * c.radius,
        Rectangle r => r.width * r.height,
    }
}

// パターンガード
void classify(int n) {
    match (n) {
        x if x < 0 => println("負の数"),
        0 => println("ゼロ"),
        x if x < 10 => println("一桁"),
        _ => println("それ以外"),
    }
}
```

## Version 0.11.0 - 非同期処理

### 目標
async/awaitとFuture型の実装

### 実装項目
| 機能 | インタプリタ | LLVM | テスト |
|------|-------------|------|--------|
| async関数 | ⬜ | ⬜ | ⬜ |
| await式 | ⬜ | ⬜ | ⬜ |
| Future型 | ⬜ | ⬜ | ⬜ |
| ランタイム統合 | ⬜ | ⬜ | ⬜ |
| 並行処理 | ⬜ | ⬜ | ⬜ |

## Version 1.0.0 - 安定版リリース

### 達成条件
- ✅ すべてのコア機能の実装完了
- ✅ インタプリタとLLVMバックエンドの動作一致
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

## 次のアクション（v0.5.0に向けて）

1. **Enum型の実装** - 2週間
   - Lexer: `enum`キーワード追加
   - Parser: enum定義のパース
   - AST/HIR: EnumDecl追加
   - 型チェック: 重複値検出
   - MIR: int型としてlowering
   - 値アクセス: `NAME::MEMBER`構文

2. **リテラル型の実装** - 1週間
   - typedef構文の拡張
   - リテラル値の型チェック

3. **ユニオン型の実装** - 2週間
   - typedef構文: `T1 | T2 | ...`
   - 構造体/interfaceを含むユニオン
   - タグ付きユニオンの内部表現

4. **型の拡張** - 1週間
   - 関数の引数/戻り値にenum/typedef/構造体を使用可能に
   - 型情報の統一的な扱い

**目標リリース日**: 2025年1月下旬

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