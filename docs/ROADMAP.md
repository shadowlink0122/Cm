---
title: ロードマップ
---

# Cm言語 開発ロードマップ

**最終更新:** 2026-02-08

---

## 現在: v0.13.1（リリース済み）

v0.13.0-v0.13.1 で達成した主要機能:
- ✅ ネイティブI/O (stdin/stdout)
- ✅ インラインアセンブリ (`__asm__`)
- ✅ 条件付きコンパイル (`#ifdef`/`#ifndef`)
- ✅ 標準ライブラリ (Vector, Queue, HashMap, Mutex, Channel)
- ✅ HTTP/HTTPS通信 (`std::http`)
- ✅ GPU/Metal計算 (`std::gpu`)
- ✅ テスト基盤安定化 (353 PASS)

---

## 次期: v0.14.0（計画中）

### テーマ: JavaScriptバックエンド & ファイルI/O

| 機能 | 状態 | 説明 |
|------|------|------|
| JSバックエンド改善 | ⬜ | ポインタ非依存セマンティクス |
| File I/O強化 | ⬜ | 本格的なファイル入出力 |
| `for (i in 0..n)` | ⬜ | 範囲イテレータ |
| パッケージ管理 | ⬜ | `cm pkg init/add` |

---

## 将来バージョン概要

| バージョン | テーマ | 主要機能 |
|------------|--------|----------|
| v0.14.0 | JS & File I/O | JSバックエンド、ファイル操作 |
| v0.15.0 | ベアメタル | `#[no_std]`, UEFI対応 |
| v0.16.0 | WASM Web | DOM操作、JSブリッジ強化 |
| v0.17.0 | 非同期処理 | async/await, Future型 |
| v1.0.0 | 安定版 | 言語仕様確定、ドキュメント完備 |

---

## 設計原則

### 構文スタイル: C++風
```cm
int add(int a, int b) { return a + b; }
```

### メソッドはinterfaceを通じて定義
```cm
interface Printable {
    void print();
}

impl Point for Printable {
    void print() { println("{self.x}, {self.y}"); }
}
```

---

## 最適化パス（実装済み）

| パス | 状態 |
|------|------|
| 定数畳み込み | ✅ |
| デッドコード除去 | ✅ |
| 定数伝播 (SCCP) | ✅ |
| コピー伝播 | ✅ |
| 共通部分式除去 | ✅ |
| インライン化 | ✅ |
| ループ不変式移動 | ✅ |
