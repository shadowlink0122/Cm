---
title: Mutex / RwLock
---

# native::sync::mutex - 排他制御

pthreadベースのMutex（排他ロック）とRwLock（読み書きロック）を提供します。

> **対応バックエンド:** Native (LLVM) のみ

**最終更新:** 2026-02-08

---

## 高レベルAPI: Mutex\<T\> / RwLock\<T\>（v0.17.0で利用可能に）

型安全なジェネリックMutex/RwLockが使えます（従来はimplメソッドの`export`修飾子がパース不能でモジュール自体がimportできませんでした）:

```cm
import std::io::println;
import native::sync::{Mutex, RwLock};

int main() {
    // Mutex<T>: 値を保持する排他ロック
    Mutex<int> m = Mutex<int>::new(10);
    MutexGuard<int> g = m.lock();
    int* p = g.get();
    *p = *p + 5;
    g.unlock();
    println("{m.value}");   // 15

    // RwLock<T>: 読み書きロック
    RwLock<int> rw = RwLock<int>::new(3);
    RwLockWriteGuard<int> w = rw.write();
    *w.get() = 7;
    w.unlock();
    RwLockReadGuard<int> r = rw.read();
    println("{*r.get()}");  // 7
    r.unlock();
    return 0;
}
```

| API | 説明 |
|------|------|
| `Mutex<T>::new(value)` / `m.lock()` / `m.try_lock()` | 生成・ロック（ガード取得）・試行 |
| `MutexGuard<T>.get()` / `.unlock()` | 値ポインタ取得・解放 |
| `RwLock<T>::new(value)` / `rw.read()` / `rw.write()` | 生成・読み/書きガード取得 |

ガードの`unlock()`は明示呼び出しです（スコープ連動の自動解放は未対応）。低レベルAPIも引き続き利用できます。

## 低レベルAPI

### 基本的な使い方

```cm
import native::sync::mutex::mutex_init;
import native::sync::mutex::mutex_lock;
import native::sync::mutex::mutex_unlock;
import native::sync::mutex::mutex_destroy;

int main() {
    // 64バイトのバッファにMutexを確保
    tiny mtx[64];
    mutex_init(mtx as void*);

    // クリティカルセクション
    mutex_lock(mtx as void*);
    // ... 共有リソースへのアクセス ...
    mutex_unlock(mtx as void*);

    mutex_destroy(mtx as void*);
    return 0;
}
```

### trylock（ノンブロッキング）

```cm
int result = mutex_trylock(mtx as void*);
if (result == 0) {
    // ロック取得成功
    mutex_unlock(mtx as void*);
} else {
    // 他のスレッドがロック中
}
```

### API一覧

| 関数 | 戻り値 | 説明 |
|------|--------|------|
| `mutex_init(ptr)` | `int` | Mutex初期化 |
| `mutex_lock(ptr)` | `int` | ロック取得（ブロッキング） |
| `mutex_unlock(ptr)` | `int` | ロック解放 |
| `mutex_trylock(ptr)` | `int` | ロック試行（ノンブロッキング） |
| `mutex_destroy(ptr)` | `int` | Mutex破棄 |

---

## RwLock（読み書きロック）

複数スレッドからの同時読み取りを許可し、書き込み時のみ排他制御を行います。

### 基本的な使い方

```cm
import native::sync::mutex::rwlock_init;
import native::sync::mutex::rwlock_rdlock;
import native::sync::mutex::rwlock_wrlock;
import native::sync::mutex::rwlock_unlock;
import native::sync::mutex::rwlock_destroy;

int main() {
    tiny lock[64];
    rwlock_init(lock as void*);

    // 読み取りロック（複数スレッドから同時取得可能）
    rwlock_rdlock(lock as void*);
    // ... 読み取り ...
    rwlock_unlock(lock as void*);

    // 書き込みロック（排他）
    rwlock_wrlock(lock as void*);
    // ... 書き込み ...
    rwlock_unlock(lock as void*);

    rwlock_destroy(lock as void*);
    return 0;
}
```

### API一覧

| 関数 | 説明 |
|------|------|
| `rwlock_init(ptr)` | RwLock初期化 |
| `rwlock_rdlock(ptr)` | 読み取りロック取得 |
| `rwlock_wrlock(ptr)` | 書き込みロック取得 |
| `rwlock_unlock(ptr)` | ロック解放 |
| `rwlock_destroy(ptr)` | RwLock破棄 |

---

## 注意事項

- Mutexの確保に `tiny mtx[64]` のようにスタック上のバイト配列を使用します
- `mutex_lock` / `rwlock_rdlock` はブロッキング呼び出しです
- デッドロックに注意：複数のMutexを取得する場合は常に同じ順序で取得してください

---

**関連:** [スレッド](thread.html) · [Channel](channel.html) · [Atomic](atomic.html)

---

<!-- nav -->
← 前: [native::thread - スレッド管理](thread.html) ｜ [目次](index.html) ｜ 次: [native::sync::channel - メッセージパッシング](channel.html) →
