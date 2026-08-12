---
title: Atomic
---

# native::sync::atomic - アトミック操作

ロックフリーのアトミック操作。カウンタやフラグなどの単純な共有変数に使用します。

> **対応バックエンド:** Native (LLVM) のみ

**最終更新:** 2026-02-08

---

## 構造体API: AtomicInt / AtomicLong / AtomicBool（v0.17.0で利用可能に）

関数APIに加えて、値を保持する構造体APIも使えます:

```cm
import native::sync::{AtomicInt, AtomicBool};

AtomicInt a = AtomicInt::new(5);
a.fetch_add(3);
println("{a.load()}");   // 8

AtomicBool flag = AtomicBool::new(true);
flag.store(false);
```

| API | 説明 |
|------|------|
| `AtomicInt::new(v)` / `AtomicLong::new(v)` / `AtomicBool::new(v)` | 生成 |
| `.load()` / `.store(v)` | 読み取り・書き込み |
| `.fetch_add(v)` / `.fetch_sub(v)` | 加減算（旧値を返す。Int/Longのみ） |
| `.compare_exchange(expected, desired)` | CAS（Int/Longのみ） |

## 基本的な使い方

```cm
import native::sync::atomic::store_i32;
import native::sync::atomic::load_i32;
import native::sync::atomic::fetch_add_i32;
import native::thread::spawn;
import native::thread::join;
import std::io::println;

int counter = 0;

void* increment(void* arg) {
    for (int i = 0; i < 1000; i++) {
        fetch_add_i32(&counter, 1);
    }
    return 0 as void*;
}

int main() {
    store_i32(&counter, 0);

    ulong t1 = spawn(increment as void*);
    ulong t2 = spawn(increment as void*);
    join(t1);
    join(t2);

    int result = load_i32(&counter);
    println("Counter: {result}");  // 2000
    return 0;
}
```

---

## Compare-and-Swap (CAS)

ロックフリーアルゴリズムの基本操作です。

```cm
import native::sync::atomic::compare_exchange_i32;
import native::sync::atomic::load_i32;

int shared = 0;

// アトミックに値を更新（CASループ）
void atomic_update(int* ptr, int new_value) {
    int expected = load_i32(ptr);
    while (!compare_exchange_i32(ptr, &expected, new_value)) {
        // expectedには現在の値が書き戻される
        // ループして再試行
    }
}
```

---

## API一覧

### int (32-bit)

| 関数 | 戻り値 | 説明 |
|------|--------|------|
| `load_i32(ptr)` | `int` | アトミック読み取り |
| `store_i32(ptr, value)` | `void` | アトミック書き込み |
| `fetch_add_i32(ptr, value)` | `int` | 加算して旧値を返す |
| `fetch_sub_i32(ptr, value)` | `int` | 減算して旧値を返す |
| `compare_exchange_i32(ptr, &expected, desired)` | `bool` | CAS操作 |

### long (64-bit)

| 関数 | 戻り値 | 説明 |
|------|--------|------|
| `load_i64(ptr)` | `long` | アトミック読み取り |
| `store_i64(ptr, value)` | `void` | アトミック書き込み |
| `fetch_add_i64(ptr, value)` | `long` | 加算して旧値を返す |
| `fetch_sub_i64(ptr, value)` | `long` | 減算して旧値を返す |
| `compare_exchange_i64(ptr, &expected, desired)` | `bool` | CAS操作 |

---

## Mutex vs Atomic

| 比較 | Mutex | Atomic |
|------|-------|--------|
| ロック | あり | なし（ロックフリー） |
| 複数フィールドの同時更新 | ✅ 可能 | ❌ 1変数のみ |
| パフォーマンス | やや遅い | 高速 |
| 用途 | 複雑な共有状態 | カウンタ、フラグ |

---

**関連:** [スレッド](thread.html) · [Mutex](mutex.html) · [Channel](channel.html)

---

<!-- nav -->
← 前: [native::sync::channel - メッセージパッシング](channel.html) ｜ [目次](index.html) ｜ 次: [native::gpu - GPU計算 (Apple Metal)](../gpu.html) →
