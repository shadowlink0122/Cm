---
title: Atomic
---

# std::sync::atomic - アトミック操作

ロックフリーのアトミック操作。カウンタやフラグなどの単純な共有変数に使用します。

> **対応バックエンド:** Native (LLVM) のみ

**最終更新:** 2026-02-08

---

## 基本的な使い方

```cm
import std::sync::atomic::store_i32;
import std::sync::atomic::load_i32;
import std::sync::atomic::fetch_add_i32;
import std::thread::spawn;
import std::thread::join;
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
import std::sync::atomic::compare_exchange_i32;
import std::sync::atomic::load_i32;

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
