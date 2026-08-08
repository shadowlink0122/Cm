# 前方検討: JS/TSエコシステム連携ロードマップ（React・Node活用）

最終ゴール: React・Node等の一般的なWebフレームワーク・ライブラリをCmから活用し、フロントエンド・サーバ開発をCmで行えるようにする。
Phase 1（npmパッケージFFI・構造体互換・コールバック・メソッドthis束縛）は実装済み（`docs/archive/v0.17.0/backends/01_js_npm_interop.md`）。

## Phase 2: TypeScript型定義との統合

- **`.d.ts` の出力**: `--target=js` に `--emit-dts` を追加し、Cmのexport関数・構造体からTypeScript型定義を生成する。TSプロジェクトがCm生成モジュールを型付きでimportできるようになる（TSバックエンドへの全面移行より先にこちらを推奨: 実行はNodeで直接可能なまま型互換だけを得られる）
- **`.d.ts` の取り込み（将来）**: パッケージの型定義から `use` 宣言を自動生成するツール（`cm bindgen <pkg>`）。手書き宣言の写経を排除する
- TSバックエンド（.ts出力）はtsc/esbuildへの依存が実行経路に入るため、`.d.ts` 双方向対応後に必要性を再評価する

## Phase 3: 非同期・ランタイム連携の完成

- **Promise連携**: JS関数が返すPromiseを `await` で受ける（Cmのasync/awaitはJSターゲット対応済み。extern宣言の戻り値をPromise<T>として扱う型付けを追加）
- **クロージャのライフタイム**: イベントハンドラ等、呼び出し元より長く生きるコールバックのキャプチャ意味論を定義する
- **ESM出力**: `import` 形式の出力切替（現状はCommonJS `require`。webターゲットはESM）。package.jsonの `type: module` プロジェクトへの対応
- **エラー連携**: JS例外をCmの `Result<T, E>` へ写像する `try` 境界

## Phase 4: React/フロントエンド戦略

- **JSXは導入しない**: `React.createElement` の直接呼び出し（hyperscript形式）をPhase 1の関数FFIで実現し、必要ならCm側のビルダーAPI（`h("div", props, children...)`）を標準ライブラリ `libs/web` に用意する
- **DOM API**: `libs/web` にDOMバインディング（document/element/イベント）を拡充する
- **コンポーネント**: Cmの構造体+関数型フィールドがpropsとコンポーネント関数に対応する（Phase 1で基盤済み）。フックはPromise/クロージャ対応（Phase 3）後に検証する
- **サーバ（Node/express）**: `use "express" {}` + 関数型フィールド呼び出し（`app.get(...)`）で到達可能。Phase 3のクロージャライフタイム確立後にE2Eサンプル（`examples/web-server/`）を追加する

## 検証済みの技術的前提（Phase 1で実証）

- Cm struct ⇔ JSオブジェクトはフィールド名保持で相互変換不要（ゼロコスト互換）
- 関数ポインタ・ラムダはJS関数としてそのまま渡る
- JSオブジェクトのメソッドは関数型フィールドとして宣言すれば `obj.method(args)` 構文とthis束縛つきで呼べる
- `require` はNode組み込み・node_modulesの両方を解決する
