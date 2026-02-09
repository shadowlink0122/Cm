// thread_runtime.cpp - Cm スレッド stdバッキング実装
// pthread APIをextern "C"でラップ

#include <cstdint>
#include <pthread.h>
#include <unistd.h>

extern "C" {

// ============================================================
// スレッド生成・管理
// ============================================================

// スレッドを生成
// fn: スレッドで実行する関数ポインタ (void*(*)(void*))
// arg: 関数に渡す引数
// 戻り値: pthread_t（スレッドID）をuintptrとして返す
uint64_t cm_thread_create(void* fn, void* arg) {
    pthread_t thread_id = 0;
    typedef void* (*thread_func_t)(void*);
    auto func = reinterpret_cast<thread_func_t>(fn);
    int ret = pthread_create(&thread_id, nullptr, func, arg);
    if (ret != 0) {
        return 0;
    }
    return reinterpret_cast<uint64_t>(thread_id);
}

// スレッドの終了を待機し、結果を取得
// thread_id: スレッドID
// retval: スレッド関数の戻り値を格納するポインタ
// 戻り値: 成功時0、失敗時エラーコード
int cm_thread_join(uint64_t thread_id, void** retval) {
    return pthread_join(reinterpret_cast<pthread_t>(thread_id), retval);
}

// スレッドをデタッチ（バックグラウンド実行）
void cm_thread_detach(uint64_t thread_id) {
    pthread_detach(reinterpret_cast<pthread_t>(thread_id));
}

// 現在のスレッドIDを取得
uint64_t cm_thread_self() {
    return reinterpret_cast<uint64_t>(pthread_self());
}

// ============================================================
// スリープ
// ============================================================

// マイクロ秒単位スリープ
void cm_thread_sleep_us(uint64_t microseconds) {
    usleep(static_cast<useconds_t>(microseconds));
}

// 複数スレッドを一括待機
// handles: スレッドIDの配列
// count: 配列の要素数
void cm_thread_join_all(uint64_t* handles, int32_t count) {
    for (int32_t i = 0; i < count; i++) {
        pthread_join(reinterpret_cast<pthread_t>(handles[i]), nullptr);
    }
}

// 引数付きスレッド生成
uint64_t cm_thread_spawn_with_arg(void* fn, void* arg) {
    pthread_t thread_id = 0;
    typedef void* (*thread_func_t)(void*);
    auto func = reinterpret_cast<thread_func_t>(fn);
    int ret = pthread_create(&thread_id, nullptr, func, arg);
    if (ret != 0) {
        return 0;
    }
    return reinterpret_cast<uint64_t>(thread_id);
}

}  // extern "C"
