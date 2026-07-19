# Cm フルスタックWeb開発サンプル（vendor JSゼロ）

**Cmだけで**Web開発を完結するサンプルです。HTMLもCSSもルーティングもインメモリDBもすべてCmで書き、外部JSファイル（`vendor/`）や手書きのグルーコードは一切ありません。唯一の外部依存はNode組み込みの `http` モジュールで、`require("http")` はNode標準なのでインストールもfixtureも不要です。

## モジュール構成（すべてCm）

| ファイル | 役割 | 実装 |
|---------|------|------|
| `src/models.cm` | ドメインモデル | `struct Task` を各層で共有（バックエンド非依存） |
| `src/store.cm` | データ層 | タスクをメモリ上に保持するインメモリストア（グローバル構造体、FFIなし） |
| `src/style.cm` | CSS | `with Css` でCmの構造体からスタイルシートを生成（CSSを手書きしない） |
| `src/view.cm` | HTML/ファサード | `web::html` の構造体ビルダーでHTMLを組み立て、ストア/スタイルを束ねる |
| `src/server.cm` | サーバ/ルータ | Node組み込み `http` でサーバを立て、分岐・状態更新・SSRをCmで行う |

依存は `server → view → {store, style, web::html, models}` の一本道です。viewがストア・スタイル・HTMLビルダーへの唯一の窓口（ファサード）になっており、サーバはviewだけをimportすれば済みます。

## Cmだけで書くWeb開発

- **HTML**: `libs/web/html.cm` の構造体ビルダーを使う。`div().class("card").add(h1().add(text("Cm Tasks")))` のようにHtmlを積む。テキスト・属性は自動でHTMLエスケープされる
- **CSS**: `with Css` 構造体からCSSを生成する。フィールド名 `background_color` がCSSプロパティ `background-color` へ自動変換される
- **DB**: グローバル構造体のインメモリストア。`init_store()` でseedし、`add_task()`/`complete_next()` で状態を更新する
- **ルーティング**: `req.method`/`req.url` で分岐し、`Response` をCmで組み立てる
- **HTTP**: Node組み込み `http` の `createServer` にCmの関数をハンドラとして渡す。`req`/`res` は構造体で型付けし、`res.setHeader`/`res.end` は関数型フィールド（JSメソッド）として呼ぶ

## 実行

```bash
# JSへコンパイルしてNodeで起動（npm install不要）
cm compile --target=js src/server.cm -o server.js
node server.js
# → http://localhost:8137/ をブラウザ/curlで開く

# TypeScriptを出力して型検査する
cm compile --target=ts src/server.cm -o server.ts
npx tsc --noEmit --lib es2020 server.ts
```

起動後のルート:

| メソッド/パス | 動作 |
|--------------|------|
| `GET /` | タスク一覧ページをSSRで返す |
| `GET /styles.css` | `with Css` が生成したCSSを返す |
| `GET /add` | タスクを1件追加して一覧を返す |
| `GET /toggle` | 未完了の先頭タスクを完了にして一覧を返す |

```bash
curl -s http://localhost:8137/           # 一覧HTML
curl -s http://localhost:8137/add        # 追加して一覧
curl -s http://localhost:8137/toggle     # 完了にして一覧
curl -s http://localhost:8137/styles.css # CSS
```

## ポイント

- **vendor JSゼロ**: 外部JSファイルもJSON設定も書かない。Node組み込み `http` だけをFFI境界に使う
- **Web開発ロジックはすべてCm**: HTML・CSS・ルーティング・状態管理をCmで書く
- **1つの型定義が全層を貫く**: `struct Task` がストア・レスポンス・ビューのデータすべての型になる
- **型検査**: 生成TSは `tsc` で型エラーなく通る

## 本番構成への発展

- **DB**: インメモリストアを実際のDBクライアントに置き換える。例えば `use "pg" { Task[] query(string sql); }` のようにFFIでPostgresへ繋ぐ（SQL文字列・行マッピングはCm側に書く）
- **ルーティング**: URLパラメータのパースやPOSTボディの読み取りを足して、フォーム送信に対応する
