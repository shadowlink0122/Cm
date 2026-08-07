---
title: スレッド
---

# native::thread - スレッド管理

pthreadベースのスレッド生成・管理モジュール。

> **対応バックエンド:** Native (LLVM) のみ

**最終更新:** 2026-02-08

---

## 基本的な使い方

```cm
import native::thread::spawn;
import native::thread::join;
import native::thread::sleep_ms;
import std::io::println;

void* worker(void* arg) {
    println("Worker started");
    sleep_ms(100);
    println("Worker done");
    return 0 as void*;
}

int main() {
    ulong handle = spawn(worker as void*);
    join(handle);
    println("All done");
    return 0;
}
```

---

## 引数付きスレッド

```cm
import native::thread::spawn_with_arg;
import native::thread::join;
import std::io::println;

void* compute(void* arg) {
    int n = arg as int;
    println("Computing: {n}");
    return (n * 2) as void*;
}

int main() {
    ulong t = spawn_with_arg(compute as void*, 21 as void*);
    const long result = join(t);  // v0.17.0でlong（64bit）に。pthreadのvoid*戻り値を全幅で受ける
    println("Result: {result}");  // 42
    return 0;
}
```

---

## 複数スレッドの一括待機

```cm
import native::thread::spawn;
import native::thread::join_all;

int main() {
    ulong handles[4];
    for (int i = 0; i < 4; i++) {
        handles[i] = spawn(worker as void*);
    }
    join_all(handles as ulong*, 4);
    return 0;
}
```

---

## API一覧

| 関数 | 説明 |
|------|------|
| `spawn(fn)` | スレッド生成（引数なし） |
| `spawn_with_arg(fn, arg)` | スレッド生成（引数あり） |
| `join(handle)` | 完了待機、戻り値取得（`long`・64bit。v0.17.0で32bit切り詰めを解消） |
| `join_all(handles, count)` | 複数スレッド一括待機 |
| `detach(handle)` | バックグラウンド実行 |
| `current_id()` | 現在のスレッドID取得 |
| `sleep_ms(ms)` | ミリ秒スリープ |
| `sleep(seconds)` | 秒スリープ |

---

**関連:** [Mutex](mutex.html) · [Channel](channel.html) · [Atomic](atomic.html)

---

<!-- nav -->
← 前: [並行処理 - 概要](index.html) ｜ [目次](index.html) ｜ 次: [native::sync::mutex - 排他制御](mutex.html) →
