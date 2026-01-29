// ============================================================
// Cm Language Runtime - Async Implementation
// v0.13.0: 非同期実行ランタイム
// ============================================================

#include "runtime_async.h"

#include <stdio.h>
#include <string.h>

// ============================================================
// グローバルエグゼキュータ
// ============================================================
CmExecutor* cm_global_executor = NULL;

// ============================================================
// エグゼキュータの実装
// ============================================================

CmExecutor* cm_executor_new(void) {
    CmExecutor* executor = (CmExecutor*)malloc(sizeof(CmExecutor));
    if (!executor)
        return NULL;

    executor->tasks = NULL;
    executor->current = NULL;
    executor->running = false;

    return executor;
}

void cm_executor_drop(CmExecutor* executor) {
    if (!executor)
        return;

    // 全タスクを解放
    CmTask* task = executor->tasks;
    while (task) {
        CmTask* next = task->next;
        if (task->future && task->future->drop) {
            task->future->drop(task->future);
        }
        free(task);
        task = next;
    }

    free(executor);
}

// ============================================================
// block_on - Futureを同期的に実行
// ============================================================

void* cm_block_on(CmFuture* future) {
    if (!future)
        return NULL;

    // シンプルなダミーWakerを作成
    CmWaker waker = {.data = NULL, .wake = NULL, .wake_by_ref = NULL, .clone = NULL, .drop = NULL};

    CmContext context = {.waker = &waker};

    // Futureが完了するまでポーリング
    while (1) {
        CmPollState state = future->poll(future, &context);

        if (state == CM_POLL_READY) {
            void* result = future->result;

            // Futureをクリーンアップ
            if (future->drop) {
                future->drop(future);
            }

            return result;
        }

        // PENDING状態なら再試行
        // 注: 実際のasyncランタイムではここでイベントループが待機する
    }
}

int64_t cm_block_on_i64(CmFuture* future) {
    if (!future)
        return 0;

    CmWaker waker = {0};
    CmContext context = {.waker = &waker};

    while (1) {
        CmPollState state = future->poll(future, &context);

        if (state == CM_POLL_READY) {
            int64_t result = 0;
            if (future->result && future->result_size >= sizeof(int64_t)) {
                result = *(int64_t*)future->result;
            }

            if (future->drop) {
                future->drop(future);
            }

            return result;
        }
    }
}

// ============================================================
// spawn - タスクをエグゼキュータに追加
// ============================================================

void cm_spawn(CmExecutor* executor, CmFuture* future) {
    if (!executor || !future)
        return;

    CmTask* task = (CmTask*)malloc(sizeof(CmTask));
    if (!task)
        return;

    task->future = future;
    task->completed = false;
    task->next = executor->tasks;
    executor->tasks = task;
}

// ============================================================
// run_until_complete - 全タスクを実行
// ============================================================

void cm_run_until_complete(CmExecutor* executor) {
    if (!executor)
        return;

    executor->running = true;

    CmWaker waker = {0};
    CmContext context = {.waker = &waker};

    while (executor->running) {
        bool all_completed = true;

        CmTask* task = executor->tasks;
        while (task) {
            if (!task->completed && task->future) {
                executor->current = task;

                CmPollState state = task->future->poll(task->future, &context);

                if (state == CM_POLL_READY) {
                    task->completed = true;
                    if (task->future->drop) {
                        task->future->drop(task->future);
                    }
                } else {
                    all_completed = false;
                }
            }

            task = task->next;
        }

        if (all_completed) {
            executor->running = false;
        }
    }

    executor->current = NULL;
}

// ============================================================
// ready_future - 即座に完了するFuture
// ============================================================

typedef struct {
    int64_t value;
} ReadyFutureState;

static CmPollState ready_future_poll(CmFuture* future, void* ctx) {
    (void)ctx;
    if (!future)
        return CM_POLL_READY;

    // 結果を設定
    ReadyFutureState* state = (ReadyFutureState*)future->state;
    if (state) {
        future->result = &state->value;
        future->result_size = sizeof(int64_t);
    }

    return CM_POLL_READY;
}

static void ready_future_drop(CmFuture* future) {
    if (!future)
        return;
    if (future->state) {
        free(future->state);
    }
    free(future);
}

CmFuture* cm_ready_future_i64(int64_t value) {
    CmFuture* future = (CmFuture*)malloc(sizeof(CmFuture));
    if (!future)
        return NULL;

    ReadyFutureState* state = (ReadyFutureState*)malloc(sizeof(ReadyFutureState));
    if (!state) {
        free(future);
        return NULL;
    }

    state->value = value;

    future->state = state;
    future->poll = ready_future_poll;
    future->drop = ready_future_drop;
    future->result = &state->value;
    future->result_size = sizeof(int64_t);

    return future;
}

// ============================================================
// Result unwrap ヘルパー
// 暫定実装: 単純にポインタを返す
// ============================================================

void* __result_unwrap(void* result) {
    // TODO: 実際のResult構造体をアンラップ
    // 現時点では単純にポインタを返す
    return result;
}

// ============================================================
// グローバルエグゼキュータの初期化/終了
// ============================================================

__attribute__((constructor)) static void cm_async_init(void) {
    cm_global_executor = cm_executor_new();
}

__attribute__((destructor)) static void cm_async_fini(void) {
    if (cm_global_executor) {
        cm_executor_drop(cm_global_executor);
        cm_global_executor = NULL;
    }
}
