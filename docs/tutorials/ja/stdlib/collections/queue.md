---
title: Queue
---

# std::collections::queue - FIFOキュー

`Queue<T>` は連結リストベースのジェネリックFIFOキューです。

> **対応バックエンド:** Native (LLVM) のみ

**最終更新:** 2026-02-08

---

## 基本的な使い方

```cm
import std::collections::queue::Queue;
import std::io::println;

int main() {
    Queue<int> q();

    q.enqueue(10);
    q.enqueue(20);
    q.enqueue(30);

    println("len: {q.len()}");           // 3
    println("front: {q.peek()}");        // 10

    int first = q.dequeue();             // 10（先頭を取り出し）
    println("dequeued: {first}");
    println("new front: {q.peek()}");    // 20

    return 0;
}
// スコープ終了時に ~self() が自動呼出し → 全ノード解放
```

---

## API一覧

| メソッド | 戻り値 | 説明 |
|---------|--------|------|
| `enqueue(value)` | `void` | 末尾に要素を追加 |
| `dequeue()` | `T` | 先頭の要素を取得して削除 |
| `peek()` | `T` | 先頭の要素を参照（削除しない） |
| `len()` | `int` | 要素数 |
| `is_empty()` | `bool` | 空かどうか |
| `clear()` | `void` | 全要素削除・全ノード解放 |

---

## 使用例: タスクキュー

```cm
import std::collections::queue::Queue;
import std::io::println;

int main() {
    Queue<int> tasks();

    // タスクIDを追加
    tasks.enqueue(101);
    tasks.enqueue(102);
    tasks.enqueue(103);

    // FIFO順に処理
    while (!tasks.is_empty()) {
        int task_id = tasks.dequeue();
        println("Processing task: {task_id}");
    }
    // 出力:
    //   Processing task: 101
    //   Processing task: 102
    //   Processing task: 103

    return 0;
}
```

---

## ネスト / move

Vectorと同様に、所有権型をキューに入れる場合は `move` が必要です。

```cm
Queue<Vector<int>> q();

Vector<int> items();
items.push(1);
items.push(2);

q.enqueue(move items);  // moveで所有権を転送
```

---

## 内部構造

`Queue<T>` は単方向連結リストで実装されています。

```
front → [10|→] → [20|→] → [30|null] ← rear
```

- `enqueue()`: rear に新ノードを追加 — **O(1)**
- `dequeue()`: front のノードを取り出して free — **O(1)**
- `peek()`: front のデータを返す — **O(1)**

---

**関連:** [Vector](vector.html) · [HashMap](hashmap.html)
