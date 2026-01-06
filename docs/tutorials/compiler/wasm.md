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

## サポート状況（v0.10.0）

| 機能 | 状態 |
|------|------|
| 基本機能 | ✅ 完全対応 |
| 配列・ポインタ | ✅ 完全対応 |
| ジェネリクス | ✅ 完全対応 |
| with自動実装 | ✅ 完全対応 |

## with自動実装インターフェース

以下のインターフェースが`with`キーワードで自動実装できます：

| インターフェース | 機能 | 生成されるメソッド |
|-----------------|------|-------------------|
| `Eq` | 等価性比較 | `operator ==(T other)` |
| `Ord` | 順序比較 | `operator <(T other)` |
| `Clone` | コピー | `clone()` |
| `Hash` | ハッシュ値 | `hash()` |
| `Debug` | デバッグ表示 | `debug()` |
| `Display` | 文字列変換 | `toString()` |

```cm
// 複数インターフェースの自動実装
struct Point with Eq, Ord, Clone {
    int x;
    int y;
}

int main() {
    Point p1 = {x: 10, y: 20};
    Point p2 = p1.clone();
    
    if (p1 == p2) {
        println("equal");  // 出力される
    }
    return 0;
}
```

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
