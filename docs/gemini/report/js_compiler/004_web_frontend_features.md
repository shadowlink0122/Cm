# JSコンパイラ詳細設計: 004 Webフロントエンド機能

## 1. CSS as Struct

Cm言語の特徴である「構造体定義からCSSを生成する」機能のJSバックエンド実装です。

### 1.1 設計思想
構造体のフィールドとデフォルト値をCSSプロパティにマッピングします。
JS実行時、またはコンパイル時にCSS文字列を生成し、ドキュメントに注入します。

### 1.2 変換ロジック

**Cm:**
```rust
struct MyButtonStyle {
    color: string = "red";
    background_color: string = "blue";
    font_size: int = 16;
}
```

**JS (Runtime Extraction):**
構造体のメタデータ（リフレクション情報）を利用して、インスタンス化されたタイミング、あるいは静的にCSSクラスを生成します。

```javascript
class MyButtonStyle {
    constructor() {
        this.color = "red";
        this.background_color = "blue";
        this.font_size = 16;
    }
    
    // CSS文字列生成メソッド
    toCSS() {
        return `
            color: ${this.color};
            background-color: ${this.background_color};
            font-size: ${this.font_size}px;
        `;
    }
}

// 使用時
let style = new MyButtonStyle();
// ランタイムがユニークなクラス名 (e.g. .MyButtonStyle_xv8s) を生成し、
// <style>タグに inject する。
```

### 1.3 `css!` マクロ (仮)
あるいは、コンパイル時に静的にCSSファイルを抽出するモードも検討します（Zero-runtime CSS）。

## 2. DOM連携

### 2.1 DOM APIのバインディング
ブラウザのネイティブAPI（`document`, `window`, `HTMLElement`）への型定義を提供します。
`extern` ブロックを使用して定義します。

```rust
// std/web/dom.cm
extern "JS" {
    type HTMLElement;
    fn get_element_by_id(id: string) -> Option<HTMLElement>;
    // ...
}
```

### 2.2 イベントハンドリング
JSのイベントリスナーはコールバック関数を要求します。Cmの関数やクロージャをJSのコールバックとして渡す際の変換が必要です。

*   **クロージャ**: JSの無名関数に変換。キャプチャ変数はJSのスコープチェーンで自然に解決されます。
*   **`this` の扱い**: Cmのメソッドをコールバックにする場合、`.bind(this)` が自動的に行われるようなラッパーを生成します。

## 3. WebAssembly (Wasm) との連携
JSコンパイラ出力だけでなく、計算負荷の高い処理をWasmで行う「ハイブリッド構成」も視野に入れます。JS側からWasmのメモリ（Linear Memory）を読み書きするヘルパー関数を生成します。
