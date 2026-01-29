// ============================================================
// Cm Language Runtime - Event Loop
// v0.13.0: イベントループ基盤
// ============================================================

#ifndef CM_RUNTIME_EVENT_LOOP_H
#define CM_RUNTIME_EVENT_LOOP_H

#include "runtime_async.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __APPLE__
#include <sys/event.h>  // kqueue
#define CM_USE_KQUEUE 1
#elif defined(__linux__)
#include <sys/epoll.h>  // epoll
#define CM_USE_EPOLL 1
#else
// ポーリングフォールバック
#include <poll.h>
#define CM_USE_POLL 1
#endif

// ============================================================
// イベントタイプ
// ============================================================
typedef enum {
    CM_EVENT_READ = 1,
    CM_EVENT_WRITE = 2,
    CM_EVENT_TIMER = 4,
    CM_EVENT_ERROR = 8,
} CmEventType;

// ============================================================
// イベント構造体
// ============================================================
typedef struct CmEvent {
    int fd;            // ファイルディスクリプタ
    CmEventType type;  // イベントタイプ
    void* user_data;   // ユーザーデータ
    CmFuture* future;  // 関連するFuture
} CmEvent;

// ============================================================
// イベントループ構造体
// ============================================================
typedef struct CmEventLoop {
#ifdef CM_USE_KQUEUE
    int kq;  // kqueue fd
#elif defined(CM_USE_EPOLL)
    int epfd;  // epoll fd
#else
    struct pollfd* fds;  // poll用
    int nfds;
    int capacity;
#endif
    bool running;
    CmEvent* pending_events;
    int pending_count;
    int pending_capacity;
} CmEventLoop;

// ============================================================
// グローバルイベントループ
// ============================================================
extern CmEventLoop* cm_global_event_loop;

// ============================================================
// Event Loop API
// ============================================================

// イベントループの作成と破棄
CmEventLoop* cm_event_loop_new(void);
void cm_event_loop_drop(CmEventLoop* loop);

// ファイルディスクリプタを登録
int cm_event_loop_register(CmEventLoop* loop, int fd, CmEventType type, void* user_data);

// ファイルディスクリプタを解除
int cm_event_loop_unregister(CmEventLoop* loop, int fd);

// イベントを待機（タイムアウト: ミリ秒、-1で無限待機）
int cm_event_loop_poll(CmEventLoop* loop, int timeout_ms);

// イベントループを実行（タスクがなくなるまで）
void cm_event_loop_run(CmEventLoop* loop, CmExecutor* executor);

// ============================================================
// タイマー API
// ============================================================
typedef struct CmTimer {
    uint64_t expires_at;  // 満了時刻（ミリ秒）
    CmFuture* future;     // 関連するFuture
    bool repeating;       // リピートタイマー
    uint64_t interval;    // リピート間隔（ミリ秒）
} CmTimer;

// タイマーを作成
CmTimer* cm_timer_new(uint64_t delay_ms, bool repeating);
void cm_timer_drop(CmTimer* timer);

// 現在時刻を取得（ミリ秒）
uint64_t cm_now_ms(void);

// スリープFuture（指定ミリ秒待機）
CmFuture* cm_sleep_ms(uint64_t ms);

#endif  // CM_RUNTIME_EVENT_LOOP_H
