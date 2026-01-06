# Enum（代数的データ型）の改善とOptional/Resultの統一に関する提案

## 概要
現在Cm言語で特別扱いされている `Optional` や `Result` を、より一般的で強力な **Enum（Tagged Union / 代数的データ型）** として再定義し、標準ライブラリ内で実装するための設計案です。これはRustの `enum` 実装に準拠し、任意のデータを持つバリアント（`ident(T)`）をサポートします。

## 1. 現状の課題
*   **特別扱い**: `Optional` や `Result` が言語組み込み、または特殊な構文として処理されている可能性があり、ユーザーが同様の構造（例: `Either<A, B>`）を定義できない。
*   **表現力不足**: 単なる整数定数の列挙（C言語スタイル）しかサポートしていない場合、データを持つ列挙型（例: `Message::Quit`, `Message::Move { x: int, y: int }`）が表現できない。

## 2. 提案: Rust準拠のEnumシステム

### 2.1 構文定義
任意の型 `T` を保持できるバリアントをサポートします。

```rust
// 標準ライブラリでの定義イメージ
enum Option<T> {
    None,
    Some(T), // データを保持するバリアント
}

enum Result<T, E> {
    Ok(T),
    Err(E),
}

// ユーザー定義の複雑なEnum
enum Message {
    Quit,
    Move { x: int, y: int }, // 構造体的なバリアント
    Write(string),           // タプル的なバリアント
    ChangeColor(int, int, int),
}
```

### 2.2 メモリレイアウト (Memory Layout)
コンパイラはEnumを「タグ（判別子）」と「ユニオン（データ）」の組み合わせとしてレイアウトします。

*   **構造**:
    ```c
    struct EnumRep {
        discriminant: u8; // どのバリアントかを示すタグ（アラインメントによりサイズは変わる）
        union {
            Variant1Data v1;
            Variant2Data v2;
            ...
        } payload;
    };
    ```
*   **サイズ**: `sizeof(tag) + sizeof(max(payloads)) + padding`
*   **アラインメント**: ペイロードの中で最大のアラインメント要求を持つ型に合わせる。

#### 最適化: Null Pointer Optimization
Rustの `Option<&T>` のように、「無効な値（例: NULLポインタ）」が存在する型をラップする場合、タグ領域を省略できます。
*   `Option<*T>` は、`*T` と同じサイズ（64bit）にする。
*   `None` は `NULL (0x0)` として表現。
*   `Some(ptr)` はそのままポインタ値として表現。

### 2.3 パターンマッチング (Pattern Matching)
データを取り出すための `match` 式をサポートします。

```rust
Option<int> val = Option::Some(42);

match val {
    Option::Some(x) => {
        print("Value is: " + x);
    },
    Option::None => {
        print("No value");
    },
}
```

### 2.4 実装ステップ

1.  **Parser/ASTの拡張**:
    *   `enum` 定義で `Variant(Type)` や `Variant { field: Type }` を受け付けるように変更。
    *   ジェネリクス (`<T>`) のサポートを確認。

2.  **型チェック/HIR**:
    *   Enum構築子（`Option::Some(10)`）を関数呼び出しのように型チェックする。
    *   `match` 式の網羅性チェック（Exhaustiveness Check）: 全てのバリアントがカバーされているか確認。

3.  **MIR/Codegen**:
    *   **Tagged Unionの生成**: `struct` と `union` を組み合わせたLLVM IR（またはターゲットコード）を生成。
    *   **Switch文の生成**: `match` をタグに基づく分岐（`switch`）に変換し、各分岐でペイロードの適切なフィールドへキャストしてアクセスする。

4.  **標準ライブラリの書き換え**:
    *   組み込みの `Optional`/`Result` を削除し、`std::core` または `std::prelude` で上記Enumを使って再定義する。

### 2.5 ユーザー定義バリアントと強力なパターンマッチング
本提案の核となるのは、`Some` や `Ok` に限らず、ユーザーが**任意の識別子とデータ構造を持つバリアント**を自由に定義できる点です。

#### 自由なバリアント定義
以下のように、タプル形式、構造体形式、ユニット形式（データなし）を混在させることができます。

```rust
enum AppEvent {
    Click,                  // データなし（ユニットバリアント）
    KeyPress(char),         // タプルバリアント（データ1つ）
    Move { x: int, y: int }, // 構造体バリアント（名前付きフィールド）
    Resize(int, int),       // タプルバリアント（データ複数）
    Unknown(any),           // 任意のデータを持つバリアント
}
```

#### 安全なデータ抽出 (Destructuring)
`match` 式を使用することで、タグの判定と同時に、内部データの抽出（デストラクチャリング）を行います。

```rust
AppEvent event = AppEvent::Move { x: 10, y: 20 };

match event {
    AppEvent::Click => {
        print("Clicked");
    },
    // データ 'c' を抽出して利用
    AppEvent::KeyPress(c) => {
        print("Key pressed: " + c);
    },
    // 構造体のフィールドを抽出
    AppEvent::Move { x, y } => {
        print("Moved to (" + x + ", " + y + ")");
    },
    // 特定の値（Resizeの第1引数）を無視する場合 '_' を使用
    AppEvent::Resize(_, h) => {
        print("Resized height to " + h);
    },
    // 網羅性チェック: 全てのバリアントをカバーしないとコンパイルエラーになる
    // 'match' は網羅的でなければならない
}
```

この仕組みにより、「値が入っているか確認し、入っていれば取り出す」という手順を、安全かつ簡潔に記述できるようになります。

## 3. 結論
`Optional` と `Result` を特別な型ではなく、標準的な `enum` の一種として実装することは、言語の一貫性を高め、ユーザーに強力な型表現力を提供します。この変更は、将来の `Async/Await` ステートマシンの実装（状態をEnumで表現する）にも不可欠な基盤となります。
