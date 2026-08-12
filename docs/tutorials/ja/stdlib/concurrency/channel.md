---
title: Channel
---

# native::sync::channel - メッセージパッシング

Go風のバウンデッドチャネルによるスレッド間メッセージパッシング。

> **対応バックエンド:** Native (LLVM) のみ

**最終更新:** 2026-02-08

---

## 構造体API: Channel（v0.17.0で利用可能に）

関数APIに加えて、ハンドルを保持する構造体APIも使えます:

```cm
import native::sync::Channel;

Channel ch = Channel::new(4);   // 容量4のバウンデッドチャネル
ch.send(42);
println("{ch.len()} {ch.recv()}");  // 1 42
ch.close();
ch.destroy();
```

`send/recv/try_send/try_recv/len/is_closed/close/destroy`が関数APIと同じ意味で使えます。

## 基本的な使い方

```cm
import native::sync::channel::create;
import native::sync::channel::send;
import native::sync::channel::recv;
import native::sync::channel::close;
import native::sync::channel::destroy;
import native::thread::spawn;
import native::thread::join;
import std::io::println;

long ch;

void* producer(void* arg) {
    for (int i = 0; i < 5; i++) {
        send(ch, i as long);
    }
    close(ch);
    return 0 as void*;
}

int main() {
    ch = create(4);  // バッファサイズ4

    ulong t = spawn(producer as void*);

    // 受信ループ
    long value = 0;
    while (recv(ch, &value) == 0) {
        println("Received: {value}");
    }

    join(t);
    destroy(ch);
    return 0;
}
```

---

## ノンブロッキング送受信

```cm
import native::sync::channel::try_send;
import native::sync::channel::try_recv;

// 送信試行
int result = try_send(ch, 42);
if (result == 0) {
    println("Sent");
} else if (result == -2) {
    println("Buffer full");
}

// 受信試行
long value = 0;
result = try_recv(ch, &value);
if (result == 0) {
    println("Got: {value}");
} else if (result == -2) {
    println("Buffer empty");
}
```

---

## API一覧

| 関数 | 戻り値 | 説明 |
|------|--------|------|
| `create(capacity)` | `long` | チャネル作成 |
| `send(handle, value)` | `int` | ブロッキング送信。0=成功, -1=クローズ済み |
| `recv(handle, &value)` | `int` | ブロッキング受信。0=成功, -1=クローズ済みかつ空 |
| `try_send(handle, value)` | `int` | ノンブロッキング送信。-2=満杯 |
| `try_recv(handle, &value)` | `int` | ノンブロッキング受信。-2=空 |
| `close(handle)` | `void` | クローズ（送信不可、残データは受信可） |
| `destroy(handle)` | `void` | チャネル破棄 |
| `len(handle)` | `int` | バッファ内の要素数 |
| `is_closed(handle)` | `int` | クローズ済み=1 |

---

## 注意事項

- `value` は `long` 型です。ポインタを渡す場合はキャストが必要です
- `close()` 後も残データの `recv()` は可能です
- `destroy()` はチャネルのメモリを解放します。使用後は必ず呼んでください

---

**関連:** [スレッド](thread.html) · [Mutex](mutex.html) · [Atomic](atomic.html)

---

<!-- nav -->
← 前: [native::sync::mutex - 排他制御](mutex.html) ｜ [目次](index.html) ｜ 次: [native::sync::atomic - アトミック操作](atomic.html) →
