---
title: 並行処理
---

# 並行処理 - 概要

Cmは pthreadベースの並行処理モジュールを標準ライブラリで提供します。

> **対応バックエンド:** Native (LLVM) のみ

**最終更新:** 2026-02-08

---

## モジュール構成

| モジュール | 用途 | ドキュメント |
|-----------|------|------------|
| `std::thread` | スレッド生成・管理 | [スレッド](thread.html) |
| `std::sync::mutex` | 排他ロック・読み書きロック | [Mutex](mutex.html) |
| `std::sync::channel` | スレッド間メッセージパッシング | [Channel](channel.html) |
| `std::sync::atomic` | ロックフリーのアトミック操作 | [Atomic](atomic.html) |

---

## 使い分けガイド

| 要件 | 推奨 |
|------|------|
| 共有状態の排他制御 | [Mutex](mutex.html) |
| スレッド間データ送受信 | [Channel](channel.html) |
| カウンタ・フラグなど単純な共有変数 | [Atomic](atomic.html) |
| 読み取り多数・書き込み少数 | [RwLock](mutex.html) |

---

## 基本パターン

```cm
import std::thread::spawn;
import std::thread::join;
import std::sync::channel::create;
import std::sync::channel::send;
import std::sync::channel::recv;
import std::sync::channel::destroy;
import std::io::println;

long ch;

void* worker(void* arg) {
    send(ch, 42);
    return 0 as void*;
}

int main() {
    ch = create(1);
    ulong t = spawn(worker as void*);

    long value = 0;
    recv(ch, &value);
    println("Received: {value}");

    join(t);
    destroy(ch);
    return 0;
}
```
