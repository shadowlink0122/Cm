# Cm フルスタックWeb開発サンプル（TypeScriptバックエンド）

**Cmだけで**Web開発を完結するサンプルです。HTML生成・CSS・コンポーネント・ルーティング・レスポンス組み立ては**すべてCmで書き**、FFI（node_modules）は実行時の境界（DBドライバ・HTTPソケット）にのみ使います。

## モジュール構成（すべてCm）

| ファイル | 役割 | 実装 |
|---------|------|------|
| `src/models.cm` | ドメインモデル | `struct Task` / `struct Response` を各層で共有 |
| `src/db.cm` | データアクセス | SQLと行→structマッピングをCmで記述（pgは汎用SQL実行のみ） |
| `src/style.cm` | CSS | `with Css` でCmの構造体からスタイルシートを生成（CSSを手書きしない） |
| `src/view.cm` | HTML/コンポーネント | 小さなHTMLビルダー `el()` とタスクコンポーネントをCmで実装 |
| `src/server.cm` | ルータ/サーバ | メソッド・パスの分岐とレスポンス組み立てをCmで実装 |

`import ./models::{Task}` で構造体を全モジュールが共有し、`--target=ts` 出力では `export interface Task` になります。

## FFI（node_modules）の使いどころ

Cmで実装できない**実行時の境界だけ**をFFIに委ねます。

| パッケージ | 用途 | Cm側 / FFI側の分担 |
|-----------|------|-------------------|
| `pg`（Postgres） | DBドライバ | SQL文字列・行マッピング・いつ何を呼ぶかは**Cm**。SQL実行だけがFFI |
| `express`（HTTP） | HTTPソケット | ルーティング分岐・HTML/CSS生成・レスポンスは**Cm**。listen/受信だけがFFI |

## 実行

```bash
# 同梱fixture（vendor/）をNODE_PATHで解決して実行する（npm install不要）
NODE_PATH=vendor cm run --target=ts src/server.cm

# TypeScriptを出力して型検査する
cm compile --target=ts src/server.cm -o server.ts
npx tsc --noEmit --lib es2020 server.ts
```

実行すると、Cmが生成したHTML（インラインCSS付き）→ タスク追加 → 完了トグル → 再描画の流れが出力されます。

## 本番構成との違い

`vendor/` はこのサンプル専用の最小fixtureで、npm install なしに動かすためのものです。本番では次のように置き換えます。

- **`pg`**: 実際のPostgresクライアント。`const client = new Client({ connectionString }); await client.query("SELECT id, title, done FROM tasks")`（`--target=ts` はasync/awaitに対応）。SQLはこのサンプルと同じくCm側に書く
- **`express`**: 実際のExpress。`http.createServer` でリクエストを受け、`route(method, url, body)` に渡してCmが組み立てたレスポンスを返す（`vendor/express` の `createServer` が雛形）

## ポイント

- **Web開発ロジックはすべてCm**: HTML・CSS・コンポーネント・ルーティング・レスポンスをCmで書く。FFIはDB/HTTPの実行時境界のみ
- **1つの型定義が全層を貫く**: `struct Task` がDB行・レスポンス・ビューのデータすべての型になり、モジュール間で共有される
- **`with Css`**: CSSをCmの構造体から自動生成（`background_color` → `background-color`）
- **型検査**: 生成TSは `tsc` で型エラーなく通る
