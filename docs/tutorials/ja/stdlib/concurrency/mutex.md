---
title: Mutex / RwLock
---

# std::sync::mutex - 排他制御

pthreadベースのMutex（排他ロック）とRwLock（読み書きロック）を提供します。

> **対応バックエンド:** Native (LLVM) のみ

**最終更新:** 2026-02-08

---

## Mutex

### 基本的な使い方

```cm
import std::sync::mutex::mutex_init;
import std::sync::mutex::mutex_lock;
import std::sync::mutex::mutex_unlock;
import std::sync::mutex::mutex_destroy;

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
import std::sync::mutex::rwlock_init;
import std::sync::mutex::rwlock_rdlock;
import std::sync::mutex::rwlock_wrlock;
import std::sync::mutex::rwlock_unlock;
import std::sync::mutex::rwlock_destroy;

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
