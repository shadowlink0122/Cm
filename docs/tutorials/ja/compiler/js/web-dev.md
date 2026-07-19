---
title: CmでのWeb開発
parent: Compiler
---

[English](../../../en/compiler/js/web-dev.html)

# CmでのWeb開発（vendor JSなし）

**ゴール:** HTML・CSS・ルーティング・インメモリストアをすべてCmで書き、Node組み込みの `http` だけでサーバサイドレンダリングのWebアプリを作る。
**所要時間:** 20分
**レベル:** 🟡 中級

---

## 概要

CmはJS/TSを第一級のバックエンドとするため、手書きのJS・JSONグルーを一切書かずにWebアプリをCmだけで組める。必要なFFIはNode組み込みの `http` モジュールだけで、`require("http")` はNode標準なので `vendor/` も `npm install` も不要。

構成要素:

- **HTML** — `web::html` の構造体ビルダー（`libs/web/html.cm`）
- **CSS** — `with Css` 構造体
- **状態** — モジュールレベルのグローバルなインメモリストア
- **HTTP** — `use "http" { ... }` でNode組み込み `http`

完全なサンプルは `examples/web-fullstack/` を参照。

---

## `web::html` ビルダーでHTMLを組む

文字列連結の代わりに `Html` ノードのツリーを組み立てる。ビルダーメソッドはselfを変更せず新しい `Html` を返すため、どのバックエンドでも出力が同じになり、テキスト・属性は自動でHTMLエスケープされる。

```cm
import web::html::*;

string card(string label) {
    return div().class("card")
        .add(h1().add(text(label)))
        .add(p().add(text("built with Cm")))
        .render();
}
// card("Hi <b>") => <div class="card"><h1>Hi &lt;b&gt;</h1><p>built with Cm</p></div>
```

よく使うタグにはショートカットがある（`div`, `ul`, `li`, `h1`, `a`, `form`, `button`, `meta`, ...）。`text()` は内容をエスケープし、`raw()` は信頼できるHTMLをそのまま挿入し、`document(root)` は `<!doctype html>` を前置する。

> インポートは `import web::html::*`（ワイルドカード）で行う。ローカル変数・引数にビルダーのショートカットと同名を付けない（例: 引数 `title` は `title()` と衝突する）。

---

## `with Css` でCSSを組む

CSSはCmの構造体から生成する（CSS文字列を手書きしない）。フィールド `background_color` がプロパティ `background-color` になる。

```cm
struct BodyStyle with Css {
    string font_family;
    string max_width;
    string color;
}

string stylesheet() {
    BodyStyle body = { font_family: "system-ui, sans-serif", max_width: "640px", color: "#1f2937" };
    return "body { " + body.to_css() + "}\n";
}
```

---

## Node組み込み `http` でHTTPサーバを立てる

Nodeの `http` オブジェクトの形を構造体で宣言する。素のフィールド（`req.method`, `req.url`, `res.statusCode`）はJSのプロパティに、メソッド（`res.setHeader`, `res.end`, `server.listen`）は関数型フィールドにすると `this` 束縛が保たれる。

```cm
//! platform: ts

struct Req { string method; string url; }
struct Res {
    int statusCode;
    void*(string, string) setHeader;
    void*(string) end;
}
struct Server { void*(int) listen; }

use "http" {
    Server createServer(void*(Req, Res) handler);
}

void handle(Req req, Res res) {
    res.statusCode = 200;
    res.setHeader("Content-Type", "text/html; charset=utf-8");
    res.end(card(req.url));   // HTMLビルダーで描画
}

int main() {
    Server server = createServer(handle);
    server.listen(8137);
    println("listening on http://localhost:8137");
    return 0;
}
```

`createServer(handle)` はCmの関数 `handle` をJSのコールバックとして渡す。`createServer` が返すサーバは外部JSオブジェクトなので、Cmは深いコピーをせず参照のまま保持する。そのため `server.listen(...)` が正しく呼べる。

実行:

```bash
cm compile --target=js server.cm -o server.js
node server.js
curl -s http://localhost:8137/
```

---

## 多ファイル構成

アプリをモジュールに分割し、依存グラフを木構造に保って、どのモジュールも2経路からインポートされないようにする（2経路になるとグローバルが重複する）。よくあるのは、ストア・スタイル・HTMLへの唯一の窓口となる**ファサード**モジュール（ここでは `view`）を置き、サーバはファサードだけをimportする形。

```
server.cm  →  view.cm  →  { store.cm, style.cm, web::html, models.cm }
```

あるモジュールが別モジュール経由で間接的にlibを使うのも問題ない。サーバの `import ./view::{page}` は、`view` の内部で使う `web::html` も取り込む。

---

## 次のステップ

- インメモリストアをFFI（`use "pg" { ... }`）で実DBに置き換える
- URLパラメータやPOSTボディをパースしてフォーム送信に対応する
- 完全なアプリは `examples/web-fullstack/` を参照

<!-- nav -->
← 前: [npmパッケージ連携](npm-interop.html) ｜ [目次](../index.html) ｜ 次: [コンパイラ編 - SystemVerilogバックエンド](../sv/index.html) →
