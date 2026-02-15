# Cm Web App サンプル

Cm言語のJSバックエンドと `with Css` 自動実装を活用したWebアプリケーションサンプルです。

## 特徴

- **`with Css` による自動CSS生成**: 構造体フィールド → CSSプロパティの自動変換
- **CmのJSバックエンド**: `cm compile --target=js` でJavaScriptに変換
- **ビルドパイプライン**: Cm → JS → CSS生成 → http-server

## ファイル構成

| ファイル | 説明 |
|---------|------|
| `src/app.cm` | Cmソース（スタイル定義 + CSS生成ロジック） |
| `public/index.html` | HTMLフロントエンド（Todoアプリ） |
| `public/styles.css` | 生成されるCSS（git管理外） |
| `public/app.js` | 生成されるJS（git管理外） |
| `build.sh` | ビルド＆起動スクリプト |
| `package.json` | pnm依存（http-server） |

## セットアップ・実行

```bash
cd examples/web-app

# 依存インストール
pnpm install

# ビルド＆起動（全自動）
./build.sh

# → http://localhost:3000 でアクセス
```

## 手動ビルド

```bash
# 1. CmソースをJSにコンパイル
cm compile --target=js src/app.cm -o public/app.js

# 2. JSを実行してCSSを生成
node public/app.js > public/styles.css

# 3. 開発サーバー起動
pnpm exec http-server public -p 3000
```

## 仕組み

```
src/app.cm  ──[cm compile --target=js]──> public/app.js
                                              │
                                        [node app.js]
                                              │
                                              ▼
                                        public/styles.css
                                              │
public/index.html  ◄──────────────────────────┘
```

`with Css` が構造体のフィールド名（`background_color` → `background-color`）を
CSSプロパティに自動変換し、`css()` / `to_css()` メソッドを生成します。
