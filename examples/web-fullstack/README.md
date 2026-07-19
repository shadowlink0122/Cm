# Cm フルスタックWeb開発サンプル（TypeScriptバックエンド）

CmのTypeScriptバックエンド（`--target=ts`）で、フロントエンド（React SSR）・サーバ（Express）・DB（Postgres）を1つのアプリとして実装するサンプルです。静的型付けのCm `struct` が全層で共有される型（`Task`）になります。

## 構成

| 要素 | 使用パッケージ | このサンプルでの実装 |
|------|--------------|--------------------|
| ドメインモデル | — | `struct Task { int id; string title; bool done; }` |
| DB層 | `pg`（Postgres） | `use "pg"` でタスクの取得・追加・トグル |
| フロントエンド | `react` / `react-dom/server` | `createElement` で要素ツリーを組み立て `renderToStaticMarkup` でSSR |
| サーバ | `express` | GET `/`・POST `/tasks`・POST `/tasks/:id/toggle` 相当のルート |

`src/app.cm` に全層をセクション分けで実装しています（Cmのモジュール間の型共有には一部制限があるため、このサンプルは単一ファイル構成）。

## 実行

```bash
# このサンプル同梱のfixture（vendor/）をNODE_PATHで解決して実行する
NODE_PATH=vendor cm run --target=ts src/app.cm

# TypeScriptを出力して型検査する
cm compile --target=ts src/app.cm -o app.ts
npx tsc --noEmit --lib es2020 app.ts
```

実行すると、SSRされたタスク一覧HTML → タスク追加 → 完了トグル → 再描画、の流れが出力されます。

## 本番構成との違い

`vendor/` 配下は **このサンプル専用の最小fixture** で、npm install なしに動かすためのものです。本番では次のように置き換えます。

- **`pg`**: 実際のPostgresクライアント。同期ヘルパーの代わりに `const client = new Client({ connectionString }); await client.query("SELECT id, title, done FROM tasks")` を使う（`--target=ts` はasync/awaitに対応）
- **`react` / `react-dom/server`**: 実際のReact。`createElement` はJSX（`<li>{t.title}</li>`）でも書けるが、Cmは `createElement` の直接呼び出しで組み立てる
- **`express`**: 実際のExpress。`app.listen(3000)` でHTTPサーバを起動し、ルートハンドラでこのサンプルの関数を呼ぶ

## ポイント

- **1つの型定義が全層を貫く**: `struct Task` がDBの行・APIのレスポンス・Reactのpropsすべての型になり、`--target=ts` 出力では `export interface Task` としてTSプロジェクトからも参照できる
- **構造体配列のFFI**: `Task[]` をそのままJSのオブジェクト配列としてReactへ渡してリストレンダリングする
- **型検査**: 生成TSは `tsc` で型エラーなく通る
