---
layout: default
title: WASMバックエンド
parent: Tutorials
nav_order: 32
---

# コンパイラ編 - WASMバックエンド

**難易度:** 🟡 中級  
**所要時間:** 20分

## 特徴

- WebAssembly出力
- ブラウザで実行可能
- サーバーレス環境対応

## サポート状況（v0.9.0）

| 機能 | 状態 |
|------|------|
| 基本機能 | ✅ 完全対応 |
| 配列・ポインタ | ✅ 完全対応 |
| ジェネリクス | ✅ 完全対応 |
| with自動実装 | ⚠️ v0.10.0予定 |

## WASM実行

```bash
# WASMコンパイル
cm compile program.cm --target=wasm -o program.wasm

# wasmtimeで実行
wasmtime program.wasm

# ブラウザで実行（将来）
# <script src="program.wasm"></script>
```

## WASMの使い道

1. **ブラウザアプリ**
   - Webアプリケーション
   - ゲーム開発
   - データビジュアライゼーション

2. **サーバーレス**
   - Cloudflare Workers
   - Fastly Compute@Edge
   - AWS Lambda（カスタムランタイム）

3. **サンドボックス環境**
   - 安全なコード実行
   - プラグインシステム

---

**前の章:** [LLVMバックエンド](llvm.md)  
**完了:** チュートリアル修了！
