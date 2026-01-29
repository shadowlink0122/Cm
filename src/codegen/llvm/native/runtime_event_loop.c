// ============================================================
// Cm Language Runtime - Event Loop Implementation
// v0.13.0: イベントループ実装
// ============================================================

#include "runtime_event_loop.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

// ============================================================
// グローバルイベントループ
// ============================================================
CmEventLoop* cm_global_event_loop = NULL;

// ============================================================
// 現在時刻を取得（ミリ秒）
// ============================================================
uint64_t cm_now_ms(void) {
#ifdef __APPLE__
    static mach_timebase_info_data_t timebase = {0};
    if (timebase.denom == 0) {
        mach_timebase_info(&timebase);
    }
    uint64_t now = mach_absolute_time();
    return (now * timebase.numer) / (timebase.denom * 1000000);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
#endif
}

// ============================================================
// イベントループの作成
// ============================================================
CmEventLoop* cm_event_loop_new(void) {
    CmEventLoop* loop = (CmEventLoop*)malloc(sizeof(CmEventLoop));
    if (!loop)
        return NULL;

    memset(loop, 0, sizeof(CmEventLoop));

#ifdef CM_USE_KQUEUE
    loop->kq = kqueue();
    if (loop->kq < 0) {
        free(loop);
        return NULL;
    }
#elif defined(CM_USE_EPOLL)
    loop->epfd = epoll_create1(0);
    if (loop->epfd < 0) {
        free(loop);
        return NULL;
    }
#else
    loop->capacity = 16;
    loop->fds = (struct pollfd*)malloc(sizeof(struct pollfd) * loop->capacity);
    if (!loop->fds) {
        free(loop);
        return NULL;
    }
    loop->nfds = 0;
#endif

    loop->pending_capacity = 16;
    loop->pending_events = (CmEvent*)malloc(sizeof(CmEvent) * loop->pending_capacity);
    loop->pending_count = 0;
    loop->running = false;

    return loop;
}

// ============================================================
// イベントループの破棄
// ============================================================
void cm_event_loop_drop(CmEventLoop* loop) {
    if (!loop)
        return;

#ifdef CM_USE_KQUEUE
    if (loop->kq >= 0)
        close(loop->kq);
#elif defined(CM_USE_EPOLL)
    if (loop->epfd >= 0)
        close(loop->epfd);
#else
    if (loop->fds)
        free(loop->fds);
#endif

    if (loop->pending_events)
        free(loop->pending_events);
    free(loop);
}

// ============================================================
// ファイルディスクリプタを登録
// ============================================================
int cm_event_loop_register(CmEventLoop* loop, int fd, CmEventType type, void* user_data) {
    if (!loop || fd < 0)
        return -1;

#ifdef CM_USE_KQUEUE
    struct kevent ev;
    int filter = 0;
    if (type & CM_EVENT_READ)
        filter = EVFILT_READ;
    else if (type & CM_EVENT_WRITE)
        filter = EVFILT_WRITE;

    EV_SET(&ev, fd, filter, EV_ADD | EV_ENABLE, 0, 0, user_data);
    return kevent(loop->kq, &ev, 1, NULL, 0, NULL);

#elif defined(CM_USE_EPOLL)
    struct epoll_event ev;
    ev.events = 0;
    if (type & CM_EVENT_READ)
        ev.events |= EPOLLIN;
    if (type & CM_EVENT_WRITE)
        ev.events |= EPOLLOUT;
    ev.data.ptr = user_data;
    return epoll_ctl(loop->epfd, EPOLL_CTL_ADD, fd, &ev);

#else
    // poll: 配列に追加
    if (loop->nfds >= loop->capacity) {
        int new_cap = loop->capacity * 2;
        struct pollfd* new_fds =
            (struct pollfd*)realloc(loop->fds, sizeof(struct pollfd) * new_cap);
        if (!new_fds)
            return -1;
        loop->fds = new_fds;
        loop->capacity = new_cap;
    }

    loop->fds[loop->nfds].fd = fd;
    loop->fds[loop->nfds].events = 0;
    if (type & CM_EVENT_READ)
        loop->fds[loop->nfds].events |= POLLIN;
    if (type & CM_EVENT_WRITE)
        loop->fds[loop->nfds].events |= POLLOUT;
    loop->fds[loop->nfds].revents = 0;
    loop->nfds++;
    return 0;
#endif
}

// ============================================================
// ファイルディスクリプタを解除
// ============================================================
int cm_event_loop_unregister(CmEventLoop* loop, int fd) {
    if (!loop || fd < 0)
        return -1;

#ifdef CM_USE_KQUEUE
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    kevent(loop->kq, &ev, 1, NULL, 0, NULL);
    EV_SET(&ev, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    return kevent(loop->kq, &ev, 1, NULL, 0, NULL);

#elif defined(CM_USE_EPOLL)
    return epoll_ctl(loop->epfd, EPOLL_CTL_DEL, fd, NULL);

#else
    // poll: 配列から削除
    for (int i = 0; i < loop->nfds; i++) {
        if (loop->fds[i].fd == fd) {
            memmove(&loop->fds[i], &loop->fds[i + 1], sizeof(struct pollfd) * (loop->nfds - i - 1));
            loop->nfds--;
            return 0;
        }
    }
    return -1;
#endif
}

// ============================================================
// イベントを待機
// ============================================================
int cm_event_loop_poll(CmEventLoop* loop, int timeout_ms) {
    if (!loop)
        return -1;

#ifdef CM_USE_KQUEUE
    struct kevent events[16];
    struct timespec ts;
    struct timespec* tsp = NULL;

    if (timeout_ms >= 0) {
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (timeout_ms % 1000) * 1000000;
        tsp = &ts;
    }

    int n = kevent(loop->kq, NULL, 0, events, 16, tsp);
    if (n < 0)
        return -1;

    loop->pending_count = 0;
    for (int i = 0; i < n && loop->pending_count < loop->pending_capacity; i++) {
        CmEvent* ev = &loop->pending_events[loop->pending_count++];
        ev->fd = (int)events[i].ident;
        ev->type = (events[i].filter == EVFILT_READ) ? CM_EVENT_READ : CM_EVENT_WRITE;
        ev->user_data = events[i].udata;
        if (events[i].flags & EV_ERROR)
            ev->type |= CM_EVENT_ERROR;
    }
    return loop->pending_count;

#elif defined(CM_USE_EPOLL)
    struct epoll_event events[16];
    int n = epoll_wait(loop->epfd, events, 16, timeout_ms);
    if (n < 0)
        return -1;

    loop->pending_count = 0;
    for (int i = 0; i < n && loop->pending_count < loop->pending_capacity; i++) {
        CmEvent* ev = &loop->pending_events[loop->pending_count++];
        ev->fd = 0;  // epollはfdを直接返さない
        ev->type = 0;
        if (events[i].events & EPOLLIN)
            ev->type |= CM_EVENT_READ;
        if (events[i].events & EPOLLOUT)
            ev->type |= CM_EVENT_WRITE;
        if (events[i].events & EPOLLERR)
            ev->type |= CM_EVENT_ERROR;
        ev->user_data = events[i].data.ptr;
    }
    return loop->pending_count;

#else
    int n = poll(loop->fds, loop->nfds, timeout_ms);
    if (n < 0)
        return -1;

    loop->pending_count = 0;
    for (int i = 0; i < loop->nfds && loop->pending_count < loop->pending_capacity; i++) {
        if (loop->fds[i].revents) {
            CmEvent* ev = &loop->pending_events[loop->pending_count++];
            ev->fd = loop->fds[i].fd;
            ev->type = 0;
            if (loop->fds[i].revents & POLLIN)
                ev->type |= CM_EVENT_READ;
            if (loop->fds[i].revents & POLLOUT)
                ev->type |= CM_EVENT_WRITE;
            if (loop->fds[i].revents & POLLERR)
                ev->type |= CM_EVENT_ERROR;
        }
    }
    return loop->pending_count;
#endif
}

// ============================================================
// イベントループを実行
// ============================================================
void cm_event_loop_run(CmEventLoop* loop, CmExecutor* executor) {
    if (!loop || !executor)
        return;

    loop->running = true;

    while (loop->running) {
        // まずタスクを実行
        bool has_pending_tasks = false;
        CmWaker waker = {0};
        CmContext context = {.waker = &waker};

        CmTask* task = executor->tasks;
        while (task) {
            if (!task->completed && task->future) {
                CmPollState state = task->future->poll(task->future, &context);
                if (state == CM_POLL_READY) {
                    task->completed = true;
                    if (task->future->drop) {
                        task->future->drop(task->future);
                    }
                } else {
                    has_pending_tasks = true;
                }
            }
            task = task->next;
        }

        // 全タスク完了時は終了
        if (!has_pending_tasks) {
            loop->running = false;
            break;
        }

        // イベントを待機（短いタイムアウト）
        cm_event_loop_poll(loop, 10);
    }
}

// ============================================================
// Sleep Future
// ============================================================
typedef struct {
    uint64_t expires_at;
    int64_t result;
} SleepFutureState;

static CmPollState sleep_future_poll(CmFuture* future, void* ctx) {
    (void)ctx;
    if (!future)
        return CM_POLL_READY;

    SleepFutureState* state = (SleepFutureState*)future->state;
    if (!state)
        return CM_POLL_READY;

    uint64_t now = cm_now_ms();
    if (now >= state->expires_at) {
        future->result = &state->result;
        future->result_size = sizeof(int64_t);
        return CM_POLL_READY;
    }

    return CM_POLL_PENDING;
}

static void sleep_future_drop(CmFuture* future) {
    if (!future)
        return;
    if (future->state)
        free(future->state);
    free(future);
}

CmFuture* cm_sleep_ms(uint64_t ms) {
    CmFuture* future = (CmFuture*)malloc(sizeof(CmFuture));
    if (!future)
        return NULL;

    SleepFutureState* state = (SleepFutureState*)malloc(sizeof(SleepFutureState));
    if (!state) {
        free(future);
        return NULL;
    }

    state->expires_at = cm_now_ms() + ms;
    state->result = 0;

    future->state = state;
    future->poll = sleep_future_poll;
    future->drop = sleep_future_drop;
    future->result = NULL;
    future->result_size = 0;

    return future;
}

// ============================================================
// グローバルイベントループの初期化/終了
// ============================================================
__attribute__((constructor)) static void cm_event_loop_init(void) {
    cm_global_event_loop = cm_event_loop_new();
}

__attribute__((destructor)) static void cm_event_loop_fini(void) {
    if (cm_global_event_loop) {
        cm_event_loop_drop(cm_global_event_loop);
        cm_global_event_loop = NULL;
    }
}
