// ============================================================
// バンドル配置定数（クライアント起動・パッケージングテストで共有）
// ============================================================
// esbuildの出力先・package.jsonのmainと一致させる。変更時は3箇所（本ファイル・package.json main・build:bundleスクリプト）の整合をpackaging.test.tsが検証する。

// クライアントバンドルの拡張ルートからの相対パス
export const CLIENT_MODULE_PATH = 'dist/extension.js';

// LSPサーバーバンドルの拡張ルートからの相対パス
export const SERVER_MODULE_PATH = 'dist/server/main.js';
