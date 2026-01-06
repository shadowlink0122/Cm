# JSコンパイラ詳細設計: 003 関数・モジュール・制御構文

## 1. 関数 (Functions)

### 1.1 関数定義とマングリング
JSはオーバーロードをサポートしないため、Cmのオーバーロードされた関数は一意な名前にマングリングする必要があります。

*   **命名規則**: `OriginalName + "_" + Hash(Signature)` または `OriginalName_ArgType1_ArgType2`
    *   例: `add(int, int)` -> `add_i32_i32`
    *   例: `add(float, float)` -> `add_f32_f32`
*   **エクスポート**: `export` された関数は、マングリングされた名前でエクスポートし、必要に応じて元の名前でエイリアスを提供します（オーバーロードがない場合）。

### 1.2 メソッド
構造体に関連付けられたメソッドは、JSクラスのメソッドとして定義します。
`impl Struct` ブロック内の関数は、`Struct.prototype` に追加します。

## 2. モジュールシステム (Modules)

Cmの `import/module` は ES Modules (`import/export`) に直接マッピングします。

### 2.1 マッピングルール
*   **ファイル単位**: Cmの1ファイル = JSの1モジュール。
*   **パス解決**: Cmの `std::io` のようなパスは、ビルド時に決定された相対パスまたはパッケージパス（`./std/io.mjs`）に変換します。

```javascript
// Cm: import std::io::println;
import { println } from "./std/io.mjs";
```

## 3. 制御構文 (Control Flow)

### 3.1 If / While / For
これらはJSの構文とほぼ1:1で対応します。
ただし、Cmの `if` は式（値を返す）ですが、JSの `if` は文です。

**式としてのif変換:**
```rust
let x = if cond { 1 } else { 0 };
```

**JS (即時関数または三項演算子):**
```javascript
let x = cond ? 1 : 0;
// または複雑なブロックの場合
let x = (() => {
    if (cond) { return 1; } else { return 0; }
})();
```

### 3.2 Pattern Matching (Match)
JSには `match` がないため、`switch` 文または `if-else` チェーンに展開します。

**Enumのマッチ:**
Tagged Unionとして実装されたEnum（`002`参照）の `tag` プロパティを見て分岐します。

```javascript
// match val { Option::Some(x) => ... }
switch (val.tag) {
    case Option.TAG_SOME:
        let x = val.payload;
        // ...
        break;
    // ...
}
```

### 3.3 ループ制御
`break`, `continue` はそのままJSにマッピングします。
`break value;` （値を返すbreak）はJSには存在しないため、変数への代入と `break` に変換します。

```javascript
// Cm: let x = loop { break 10; };
let x;
while (true) {
    x = 10;
    break;
}
```
