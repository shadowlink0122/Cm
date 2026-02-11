// net_runtime.cpp - Cm ネットワーク stdバッキング実装
// POSIXソケットAPI + kqueue(macOS)/poll(Linux) ベースの非同期I/O

#include <arpa/inet.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// プラットフォーム固有のイベント多重化
#ifdef __APPLE__
#include <sys/event.h>  // kqueue
#else
#include <poll.h>  // poll
#endif

// イベントループ内部構造
struct CmPollHandle {
#ifdef __APPLE__
    int kq;                 // kqueueファイルディスクリプタ
    struct kevent* events;  // イベント結果配列
    int event_count;        // 最後のwaitで取得したイベント数
    int max_events;         // イベント配列サイズ
#else
    struct pollfd* fds;  // pollfd配列
    int fd_count;        // 登録済みFD数
    int max_fds;         // 配列サイズ
    int event_count;     // 最後のwaitで取得したイベント数
#endif
};

// イベントタイプ定数（Cm側と共有）
// ビットフラグ: 1=読み取り可能, 2=書き込み可能, 4=エラー/切断
static const int CM_POLL_READ = 1;
static const int CM_POLL_WRITE = 2;
static const int CM_POLL_ERROR = 4;
static const int CM_POLL_HUP = 8;

extern "C" {

// ============================================================
// TCPソケット操作
// ============================================================

// TCPサーバソケットを作成し、指定ポートでリッスン
// 戻り値: サーバソケットFD（失敗時-1）
int64_t cm_tcp_listen(int32_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    // SO_REUSEADDR: TIME_WAITのポートを再利用可能にする
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    // バックログ128
    if (listen(fd, 128) < 0) {
        close(fd);
        return -1;
    }

    return static_cast<int64_t>(fd);
}

// クライアント接続を受け付ける（ブロッキング）
// 戻り値: クライアントソケットFD（失敗時-1）
int64_t cm_tcp_accept(int64_t server_fd) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int client_fd = accept(static_cast<int>(server_fd), (struct sockaddr*)&client_addr, &addr_len);
    if (client_fd < 0)
        return -1;

    // TCP_NODELAY: Nagleアルゴリズム無効化（低遅延）
    int opt = 1;
    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    return static_cast<int64_t>(client_fd);
}

// TCPクライアント接続
// host: 接続先ホスト名/IPアドレス（C文字列）
// port: 接続先ポート
// 戻り値: ソケットFD（失敗時-1）
int64_t cm_tcp_connect(int64_t host_ptr, int32_t port) {
    const char* host = reinterpret_cast<const char*>(host_ptr);
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &result) != 0) {
        return -1;
    }

    int fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(result);
        return -1;
    }

    if (connect(fd, result->ai_addr, result->ai_addrlen) < 0) {
        close(fd);
        freeaddrinfo(result);
        return -1;
    }

    freeaddrinfo(result);

    // TCP_NODELAY
    int opt = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    return static_cast<int64_t>(fd);
}

// ソケットからデータ読み取り
// buf_ptr: バッファポインタをlongで受け取り
// 戻り値: 読み取りバイト数（0=接続断、-1=エラー）
int32_t cm_tcp_read(int64_t fd, int64_t buf_ptr, int32_t size) {
    void* buf = reinterpret_cast<void*>(buf_ptr);
    ssize_t n = read(static_cast<int>(fd), buf, static_cast<size_t>(size));
    return static_cast<int32_t>(n);
}

// ソケットにデータ書き込み
// buf_ptr: バッファポインタをlongで受け取り
// 戻り値: 書き込みバイト数（-1=エラー）
int32_t cm_tcp_write(int64_t fd, int64_t buf_ptr, int32_t size) {
    const void* buf = reinterpret_cast<const void*>(buf_ptr);
    ssize_t n = write(static_cast<int>(fd), buf, static_cast<size_t>(size));
    return static_cast<int32_t>(n);
}

// ソケットクローズ
void cm_tcp_close(int64_t fd) {
    close(static_cast<int>(fd));
}

// ============================================================
// バッファ操作ヘルパー
// Cmの&array[index] as longキャスト制限を回避するため、
// C++側でヒープバッファの作成・バイト操作を提供
// ============================================================

// バッファ作成（戻り値: バッファハンドル = ポインタをlongで返す）
int64_t cm_buf_create(int32_t size) {
    void* buf = malloc(static_cast<size_t>(size));
    if (!buf)
        return 0;
    memset(buf, 0, static_cast<size_t>(size));
    return reinterpret_cast<int64_t>(buf);
}

// バッファの指定位置にバイトを書き込み
void cm_buf_set(int64_t buf_handle, int32_t index, int32_t value) {
    auto* buf = reinterpret_cast<uint8_t*>(buf_handle);
    if (buf)
        buf[index] = static_cast<uint8_t>(value);
}

// バッファの指定位置からバイトを読み取り
int32_t cm_buf_get(int64_t buf_handle, int32_t index) {
    auto* buf = reinterpret_cast<uint8_t*>(buf_handle);
    if (!buf)
        return -1;
    return static_cast<int32_t>(buf[index]);
}

// バッファ破棄
void cm_buf_destroy(int64_t buf_handle) {
    auto* buf = reinterpret_cast<void*>(buf_handle);
    free(buf);
}

// ソケットをノンブロッキングに設定
// 戻り値: 0=成功, -1=失敗
int32_t cm_tcp_set_nonblocking(int64_t fd) {
    int flags = fcntl(static_cast<int>(fd), F_GETFL, 0);
    if (flags < 0)
        return -1;
    return fcntl(static_cast<int>(fd), F_SETFL, flags | O_NONBLOCK) < 0 ? -1 : 0;
}

// ============================================================
// イベント多重化（kqueue/poll）
// ============================================================

// イベントループ作成
// 戻り値: ポールハンドル（失敗時0）
int64_t cm_tcp_poll_create() {
    auto* ph = static_cast<CmPollHandle*>(malloc(sizeof(CmPollHandle)));
    if (!ph)
        return 0;

    memset(ph, 0, sizeof(CmPollHandle));

#ifdef __APPLE__
    // kqueue作成
    ph->kq = kqueue();
    if (ph->kq < 0) {
        free(ph);
        return 0;
    }
    ph->max_events = 64;
    ph->events = static_cast<struct kevent*>(malloc(sizeof(struct kevent) * ph->max_events));
    if (!ph->events) {
        close(ph->kq);
        free(ph);
        return 0;
    }
    ph->event_count = 0;
#else
    // poll用配列
    ph->max_fds = 64;
    ph->fds = static_cast<struct pollfd*>(malloc(sizeof(struct pollfd) * ph->max_fds));
    if (!ph->fds) {
        free(ph);
        return 0;
    }
    ph->fd_count = 0;
    ph->event_count = 0;
#endif

    return reinterpret_cast<int64_t>(ph);
}

// イベントループにFDを登録
// events: ビットフラグ（CM_POLL_READ=1, CM_POLL_WRITE=2）
// 戻り値: 0=成功, -1=失敗
int32_t cm_tcp_poll_add(int64_t poll_handle, int64_t fd, int32_t events) {
    auto* ph = reinterpret_cast<CmPollHandle*>(poll_handle);
    if (!ph)
        return -1;

    int raw_fd = static_cast<int>(fd);

#ifdef __APPLE__
    // kqueueにFD登録
    struct kevent ev[2];
    int n = 0;
    if (events & CM_POLL_READ) {
        EV_SET(&ev[n], raw_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
        n++;
    }
    if (events & CM_POLL_WRITE) {
        EV_SET(&ev[n], raw_fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, nullptr);
        n++;
    }
    if (n > 0) {
        if (kevent(ph->kq, ev, n, nullptr, 0, nullptr) < 0) {
            return -1;
        }
    }
#else
    // poll配列にFD追加
    if (ph->fd_count >= ph->max_fds) {
        // 配列拡張
        int new_max = ph->max_fds * 2;
        auto* new_fds =
            static_cast<struct pollfd*>(realloc(ph->fds, sizeof(struct pollfd) * new_max));
        if (!new_fds)
            return -1;
        ph->fds = new_fds;
        ph->max_fds = new_max;
    }
    struct pollfd* pfd = &ph->fds[ph->fd_count++];
    pfd->fd = raw_fd;
    pfd->events = 0;
    if (events & CM_POLL_READ)
        pfd->events |= POLLIN;
    if (events & CM_POLL_WRITE)
        pfd->events |= POLLOUT;
    pfd->revents = 0;
#endif

    return 0;
}

// イベントループからFDを削除
// 戻り値: 0=成功, -1=失敗
int32_t cm_tcp_poll_remove(int64_t poll_handle, int64_t fd) {
    auto* ph = reinterpret_cast<CmPollHandle*>(poll_handle);
    if (!ph)
        return -1;

    int raw_fd = static_cast<int>(fd);

#ifdef __APPLE__
    // kqueueからFD削除
    struct kevent ev[2];
    EV_SET(&ev[0], raw_fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    EV_SET(&ev[1], raw_fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    // エラーを無視（既に削除済みの場合）
    kevent(ph->kq, ev, 2, nullptr, 0, nullptr);
#else
    // poll配列からFD削除
    for (int i = 0; i < ph->fd_count; i++) {
        if (ph->fds[i].fd == raw_fd) {
            // 最後の要素と入れ替え
            ph->fds[i] = ph->fds[ph->fd_count - 1];
            ph->fd_count--;
            break;
        }
    }
#endif

    return 0;
}

// イベント待機
// timeout_ms: タイムアウト（ミリ秒、-1=無限待機）
// 戻り値: 発生イベント数（0=タイムアウト、-1=エラー）
int32_t cm_tcp_poll_wait(int64_t poll_handle, int32_t timeout_ms) {
    auto* ph = reinterpret_cast<CmPollHandle*>(poll_handle);
    if (!ph)
        return -1;

#ifdef __APPLE__
    struct timespec ts;
    struct timespec* ts_ptr = nullptr;
    if (timeout_ms >= 0) {
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (timeout_ms % 1000) * 1000000L;
        ts_ptr = &ts;
    }
    int n = kevent(ph->kq, nullptr, 0, ph->events, ph->max_events, ts_ptr);
    ph->event_count = (n > 0) ? n : 0;
    return n;
#else
    int n = poll(ph->fds, static_cast<nfds_t>(ph->fd_count), timeout_ms);
    ph->event_count = (n > 0) ? n : 0;
    return n;
#endif
}

// 発生イベントのFDを取得
// index: イベントインデックス（0 〜 poll_wait戻り値-1）
// 戻り値: FD（範囲外の場合-1）
int64_t cm_tcp_poll_get_fd(int64_t poll_handle, int32_t index) {
    auto* ph = reinterpret_cast<CmPollHandle*>(poll_handle);
    if (!ph)
        return -1;

#ifdef __APPLE__
    if (index < 0 || index >= ph->event_count)
        return -1;
    return static_cast<int64_t>(ph->events[index].ident);
#else
    // pollの場合、reventsが非ゼロのFDをindex番目まで探す
    int found = 0;
    for (int i = 0; i < ph->fd_count; i++) {
        if (ph->fds[i].revents != 0) {
            if (found == index) {
                return static_cast<int64_t>(ph->fds[i].fd);
            }
            found++;
        }
    }
    return -1;
#endif
}

// 発生イベントの種類を取得
// index: イベントインデックス
// 戻り値: イベントビットフラグ（CM_POLL_READ/WRITE/ERROR/HUP）
int32_t cm_tcp_poll_get_events(int64_t poll_handle, int32_t index) {
    auto* ph = reinterpret_cast<CmPollHandle*>(poll_handle);
    if (!ph)
        return 0;

#ifdef __APPLE__
    if (index < 0 || index >= ph->event_count)
        return 0;
    int result = 0;
    struct kevent* ev = &ph->events[index];
    if (ev->filter == EVFILT_READ)
        result |= CM_POLL_READ;
    if (ev->filter == EVFILT_WRITE)
        result |= CM_POLL_WRITE;
    if (ev->flags & EV_EOF)
        result |= CM_POLL_HUP;
    if (ev->flags & EV_ERROR)
        result |= CM_POLL_ERROR;
    return result;
#else
    int found = 0;
    for (int i = 0; i < ph->fd_count; i++) {
        if (ph->fds[i].revents != 0) {
            if (found == index) {
                int result = 0;
                if (ph->fds[i].revents & POLLIN)
                    result |= CM_POLL_READ;
                if (ph->fds[i].revents & POLLOUT)
                    result |= CM_POLL_WRITE;
                if (ph->fds[i].revents & (POLLERR | POLLNVAL))
                    result |= CM_POLL_ERROR;
                if (ph->fds[i].revents & POLLHUP)
                    result |= CM_POLL_HUP;
                return result;
            }
            found++;
        }
    }
    return 0;
#endif
}

// イベントループ破棄
void cm_tcp_poll_destroy(int64_t poll_handle) {
    auto* ph = reinterpret_cast<CmPollHandle*>(poll_handle);
    if (!ph)
        return;

#ifdef __APPLE__
    close(ph->kq);
    free(ph->events);
#else
    free(ph->fds);
#endif
    free(ph);
}

// ============================================================
// UDPソケット操作
// ============================================================

// UDPソケットを作成
// 戻り値: ソケットFD（失敗時-1）
int64_t cm_udp_create() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("[net] UDP socket creation failed");
        return -1;
    }
    return static_cast<int64_t>(fd);
}

// UDPソケットを指定ポートにバインド
// 戻り値: 0=成功, -1=失敗
int32_t cm_udp_bind(int64_t fd, int32_t port) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(static_cast<int>(fd), (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[net] UDP bind failed");
        return -1;
    }
    return 0;
}

// UDPデータグラム送信
// host_ptr: 送信先ホスト名/IPアドレス（C文字列のポインタをlongで受け取り）
// port: 送信先ポート
// buf_ptr: 送信データバッファ（ポインタをlongで受け取り）
// size: 送信データサイズ
// 戻り値: 送信バイト数（-1=エラー）
int32_t cm_udp_sendto(int64_t fd, int64_t host_ptr, int32_t port, int64_t buf_ptr, int32_t size) {
    const char* host = reinterpret_cast<const char*>(host_ptr);

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(static_cast<uint16_t>(port));

    // IPアドレスまたはホスト名で解決
    if (inet_pton(AF_INET, host, &dest_addr.sin_addr) <= 0) {
        // ホスト名の場合はDNS解決
        struct addrinfo hints, *result;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(host, nullptr, &hints, &result) != 0) {
            return -1;
        }
        memcpy(&dest_addr.sin_addr, &((struct sockaddr_in*)result->ai_addr)->sin_addr,
               sizeof(struct in_addr));
        freeaddrinfo(result);
    }

    const void* buf = reinterpret_cast<const void*>(buf_ptr);
    ssize_t sent = sendto(static_cast<int>(fd), buf, static_cast<size_t>(size), 0,
                          (struct sockaddr*)&dest_addr, sizeof(dest_addr));
    return static_cast<int32_t>(sent);
}

// UDPデータグラム受信
// buf_ptr: 受信バッファ（ポインタをlongで受け取り）
// size: バッファサイズ
// 戻り値: 受信バイト数（-1=エラー、0=タイムアウト）
int32_t cm_udp_recvfrom(int64_t fd, int64_t buf_ptr, int32_t size) {
    void* buf = reinterpret_cast<void*>(buf_ptr);
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    ssize_t received = recvfrom(static_cast<int>(fd), buf, static_cast<size_t>(size), 0,
                                (struct sockaddr*)&from_addr, &from_len);
    return static_cast<int32_t>(received);
}

// UDPソケットクローズ
void cm_udp_close(int64_t fd) {
    close(static_cast<int>(fd));
}

// UDPブロードキャスト有効化
// 戻り値: 0=成功, -1=失敗
int32_t cm_udp_set_broadcast(int64_t fd) {
    int opt = 1;
    if (setsockopt(static_cast<int>(fd), SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) < 0) {
        return -1;
    }
    return 0;
}

// ============================================================
// DNS解決
// ============================================================

// ホスト名をIPアドレス文字列に解決
// 戻り値: IPアドレス文字列（strdup、失敗時はNULL）
char* cm_dns_resolve(const char* hostname) {
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(hostname, nullptr, &hints, &result) != 0) {
        return nullptr;
    }

    char ip_str[INET_ADDRSTRLEN];
    struct sockaddr_in* addr = (struct sockaddr_in*)result->ai_addr;
    inet_ntop(AF_INET, &addr->sin_addr, ip_str, sizeof(ip_str));
    freeaddrinfo(result);

    return strdup(ip_str);
}

// ============================================================
// ソケットオプション
// ============================================================

// 送受信タイムアウト設定（ミリ秒）
// 戻り値: 0=成功, -1=失敗
int32_t cm_socket_set_timeout(int64_t fd, int32_t timeout_ms) {
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int s = static_cast<int>(fd);
    if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        return -1;
    }
    if (setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        return -1;
    }
    return 0;
}

// アドレス再利用設定（SO_REUSEADDR）
// 戻り値: 0=成功, -1=失敗
int32_t cm_socket_set_reuse_addr(int64_t fd) {
    int opt = 1;
    if (setsockopt(static_cast<int>(fd), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        return -1;
    }
    return 0;
}

// TCP_NODELAY設定（Nagleアルゴリズム無効化）
// 戻り値: 0=成功, -1=失敗
int32_t cm_socket_set_nodelay(int64_t fd) {
    int opt = 1;
    if (setsockopt(static_cast<int>(fd), IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
        return -1;
    }
    return 0;
}

// SO_KEEPALIVE設定
// 戻り値: 0=成功, -1=失敗
int32_t cm_socket_set_keepalive(int64_t fd) {
    int opt = 1;
    if (setsockopt(static_cast<int>(fd), SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt)) < 0) {
        return -1;
    }
    return 0;
}

// 受信バッファサイズ設定
// 戻り値: 0=成功, -1=失敗
int32_t cm_socket_set_recv_buffer(int64_t fd, int32_t size) {
    if (setsockopt(static_cast<int>(fd), SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) < 0) {
        return -1;
    }
    return 0;
}

// 送信バッファサイズ設定
// 戻り値: 0=成功, -1=失敗
int32_t cm_socket_set_send_buffer(int64_t fd, int32_t size) {
    if (setsockopt(static_cast<int>(fd), SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) < 0) {
        return -1;
    }
    return 0;
}

}  // extern "C"
