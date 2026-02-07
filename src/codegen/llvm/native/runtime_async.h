// ============================================================
// Cm Language Runtime - Async Support
// v0.13.0: 非同期実行ランタイム
// ============================================================

#ifndef CM_RUNTIME_ASYNC_H
#define CM_RUNTIME_ASYNC_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

// ============================================================
// Poll状態（非同期操作の結果）
// ============================================================
typedef enum {
    CM_POLL_PENDING = 0,  // 未完了
    CM_POLL_READY = 1     // 完了
} CmPollState;

// ============================================================
// Future構造体
// ポーリング可能な非同期操作を表現
// ============================================================
typedef struct CmFuture {
    void* state;                                   // ステートマシン状態
    CmPollState (*poll)(struct CmFuture*, void*);  // ポーリング関数
    void (*drop)(struct CmFuture*);                // デストラクタ
    void* result;                                  // 結果値へのポインタ
    size_t result_size;                            // 結果値のサイズ
} CmFuture;

// ============================================================
// Waker構造体
// タスクを再スケジュールするためのウェイクアップ機構
// ============================================================
typedef struct CmWaker {
    void* data;                                       // ウェイカーデータ
    void (*wake)(struct CmWaker*);                    // ウェイクアップ関数
    void (*wake_by_ref)(const struct CmWaker*);       // 参照でウェイクアップ
    struct CmWaker* (*clone)(const struct CmWaker*);  // クローン
    void (*drop)(struct CmWaker*);                    // デストラクタ
} CmWaker;

// ============================================================
// Context構造体
// 非同期操作のコンテキスト
// ============================================================
typedef struct CmContext {
    CmWaker* waker;
} CmContext;

// ============================================================
// タスク構造体
// エグゼキュータで実行されるタスク
// ============================================================
typedef struct CmTask {
    CmFuture* future;
    bool completed;
    struct CmTask* next;  // リンクドリスト用
} CmTask;

// ============================================================
// エグゼキュータ構造体
// シングルスレッド非同期エグゼキュータ
// ============================================================
typedef struct CmExecutor {
    CmTask* tasks;    // タスクキュー（リンクドリスト）
    CmTask* current;  // 現在実行中のタスク
    bool running;
} CmExecutor;

// ============================================================
// グローバルエグゼキュータ
// ============================================================
extern CmExecutor* cm_global_executor;

// ============================================================
// Runtime API
// ============================================================

// エグゼキュータの作成と破棄
CmExecutor* cm_executor_new(void);
void cm_executor_drop(CmExecutor* executor);

// Futureをブロッキング実行（同期的に完了まで待機）
void* cm_block_on(CmFuture* future);

// int64_tを返すFutureをブロッキング実行
int64_t cm_block_on_i64(CmFuture* future);

// タスクをスポーン
void cm_spawn(CmExecutor* executor, CmFuture* future);

// 全タスクを実行
void cm_run_until_complete(CmExecutor* executor);

// ============================================================
// ヘルパー関数
// ============================================================

// 即座に完了するFutureを作成
CmFuture* cm_ready_future_i64(int64_t value);

// Result用のヘルパー
void* __result_unwrap(void* result);

#endif  // CM_RUNTIME_ASYNC_H
