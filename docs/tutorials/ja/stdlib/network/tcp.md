---
title: TCP/UDP通信
---

# std::net - TCP/UDP通信

低レベルのソケット通信モジュール。TCP/UDP/DNS/イベント多重化(poll)を提供します。

> **対応バックエンド:** Native (LLVM) のみ

**最終更新:** 2026-02-08

---

## TCP通信

### エコーサーバ

```cm
import std::net::tcp_listen;
import std::net::tcp_accept;
import std::net::tcp_read;
import std::net::tcp_write;
import std::net::tcp_close;
import std::net::buf_create;
import std::net::buf_get;
import std::net::buf_destroy;
import std::io::println;

int main() {
    long server_fd = tcp_listen(8080);
    println("Listening on port 8080");

    long client = tcp_accept(server_fd);
    long buf = buf_create(256);
    int n = tcp_read(client, buf, 255);
    if (n > 0) {
        tcp_write(client, buf, n);  // エコーバック
    }
    buf_destroy(buf);
    tcp_close(client);
    tcp_close(server_fd);
    return 0;
}
```

### TCPクライアント

```cm
import std::net::tcp_connect;
import std::net::tcp_read;
import std::net::tcp_write;
import std::net::tcp_close;
import std::net::buf_create;
import std::net::buf_set;
import std::net::buf_get;
import std::net::buf_destroy;
import std::io::println;

int main() {
    long fd = tcp_connect("127.0.0.1" as long, 8080);
    long msg = buf_create(4);
    buf_set(msg, 0, 72);   // H
    buf_set(msg, 1, 105);  // i
    buf_set(msg, 2, 33);   // !
    tcp_write(fd, msg, 3);
    buf_destroy(msg);

    long rbuf = buf_create(256);
    int n = tcp_read(fd, rbuf, 255);
    println("Received {n} bytes");
    buf_destroy(rbuf);
    tcp_close(fd);
    return 0;
}
```

### TCP API

| 関数 | 説明 |
|------|------|
| `tcp_listen(port)` | サーバソケット作成（bind+listen） |
| `tcp_accept(server_fd)` | クライアント接続受付（ブロッキング） |
| `tcp_connect(host, port)` | サーバに接続 |
| `tcp_read(fd, buf, size)` | データ読み取り → 読み取ったバイト数 |
| `tcp_write(fd, buf, size)` | データ書き込み → 書き込んだバイト数 |
| `tcp_close(fd)` | ソケットクローズ |
| `tcp_set_nonblocking(fd)` | ノンブロッキングモード設定 |

---

## バッファ操作

TCPではバイト列を直接操作するため、ヒープバッファAPIを使用します。

| 関数 | 説明 |
|------|------|
| `buf_create(size)` | ヒープバッファ作成 |
| `buf_set(buf, index, value)` | バイト書き込み |
| `buf_get(buf, index)` | バイト読み取り |
| `buf_destroy(buf)` | バッファ解放 |

---

## UDP通信

```cm
import std::net::udp_create;
import std::net::udp_bind;
import std::net::udp_sendto;
import std::net::udp_recvfrom;
import std::net::udp_close;
import std::net::buf_create;
import std::net::buf_set;
import std::net::buf_destroy;

int main() {
    long fd = udp_create();
    udp_bind(fd, 9000);

    long buf = buf_create(256);
    int n = udp_recvfrom(fd, buf, 255);
    // ... process data ...
    buf_destroy(buf);
    udp_close(fd);
    return 0;
}
```

### UDP API

| 関数 | 説明 |
|------|------|
| `udp_create()` | UDPソケット作成 |
| `udp_bind(fd, port)` | ポートにバインド |
| `udp_sendto(fd, host, port, buf, size)` | データグラム送信 |
| `udp_recvfrom(fd, buf, size)` | データグラム受信 |
| `udp_close(fd)` | ソケットクローズ |
| `udp_set_broadcast(fd)` | ブロードキャスト有効化 |

---

## DNS解決

```cm
import std::net::dns_resolve;
string ip = dns_resolve("example.com");
// ip = "93.184.216.34"
```

---

## ソケットオプション

TCP/UDP共通のオプション設定。

| 関数 | 説明 |
|------|------|
| `socket_set_timeout(fd, ms)` | 送受信タイムアウト(ミリ秒) |
| `socket_set_reuse_addr(fd)` | アドレス再利用 (TIME_WAIT回避) |
| `socket_set_nodelay(fd)` | Nagle無効化 (低レイテンシ) |
| `socket_set_keepalive(fd)` | KeepAlive有効化 |
| `socket_set_recv_buffer(fd, size)` | 受信バッファサイズ |
| `socket_set_send_buffer(fd, size)` | 送信バッファサイズ |

---

## イベント多重化 (poll)

複数ソケットを効率的に監視するためのAPI (kqueue/poll ベース)。

```cm
import std::net::poll_create;
import std::net::poll_add;
import std::net::poll_wait;
import std::net::poll_get_fd;
import std::net::poll_get_events;
import std::net::poll_destroy;
import std::net::poll_read_flag;

long poller = poll_create();
poll_add(poller, server_fd, poll_read_flag());

int ready = poll_wait(poller, 1000);  // 1秒タイムアウト
for (int i = 0; i < ready; i++) {
    long fd = poll_get_fd(poller, i);
    int events = poll_get_events(poller, i);
    // ... handle events ...
}

poll_destroy(poller);
```

### イベントフラグ

| 関数 | 値 | 説明 |
|------|----|------|
| `poll_read_flag()` | 1 | 読み取り可能 |
| `poll_write_flag()` | 2 | 書き込み可能 |
| `poll_error_flag()` | 4 | エラー発生 |
| `poll_hup_flag()` | 8 | 接続切断 |

---

**関連:** [HTTP通信](../http.html)（高レベルAPI）
