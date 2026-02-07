// channel_runtime.cpp - Cm チャネル stdバッキング実装
// pthread_mutex + pthread_cond ベースのバウンデッドチャネル（リングバッファ）

#include <cstdint>
#include <cstdlib>
#include <pthread.h>

// チャネル内部構造
struct CmChannel {
    int64_t* buffer;           // リングバッファ
    int capacity;              // 最大要素数
    int count;                 // 現在の要素数
    int head;                  // 読み出し位置
    int tail;                  // 書き込み位置
    pthread_mutex_t mutex;     // 排他制御
    pthread_cond_t not_full;   // バッファ空きあり条件
    pthread_cond_t not_empty;  // バッファにデータあり条件
    bool closed;               // クローズフラグ
};

extern "C" {

// チャネル作成
// capacity: バッファサイズ（0以下の場合は1に補正）
// 戻り値: チャネルハンドル（opaqueポインタ）
int64_t cm_channel_create(int32_t capacity) {
    if (capacity <= 0)
        capacity = 1;

    auto* ch = static_cast<CmChannel*>(malloc(sizeof(CmChannel)));
    if (!ch)
        return 0;

    ch->buffer = static_cast<int64_t*>(malloc(sizeof(int64_t) * capacity));
    if (!ch->buffer) {
        free(ch);
        return 0;
    }

    ch->capacity = capacity;
    ch->count = 0;
    ch->head = 0;
    ch->tail = 0;
    ch->closed = false;

    pthread_mutex_init(&ch->mutex, nullptr);
    pthread_cond_init(&ch->not_full, nullptr);
    pthread_cond_init(&ch->not_empty, nullptr);

    return reinterpret_cast<int64_t>(ch);
}

// チャネルにデータ送信（ブロッキング）
// 戻り値: 0=成功, -1=チャネルがクローズ済み
int32_t cm_channel_send(int64_t handle, int64_t value) {
    auto* ch = reinterpret_cast<CmChannel*>(handle);
    if (!ch)
        return -1;

    pthread_mutex_lock(&ch->mutex);

    // バッファが満杯の間は待機
    while (ch->count >= ch->capacity && !ch->closed) {
        pthread_cond_wait(&ch->not_full, &ch->mutex);
    }

    if (ch->closed) {
        pthread_mutex_unlock(&ch->mutex);
        return -1;
    }

    // リングバッファに書き込み
    ch->buffer[ch->tail] = value;
    ch->tail = (ch->tail + 1) % ch->capacity;
    ch->count++;

    // 受信待ちスレッドを起こす
    pthread_cond_signal(&ch->not_empty);
    pthread_mutex_unlock(&ch->mutex);

    return 0;
}

// チャネルからデータ受信（ブロッキング）
// value: 受信したデータを格納するポインタ
// 戻り値: 0=成功, -1=チャネルがクローズ済みかつ空
int32_t cm_channel_recv(int64_t handle, int64_t* value) {
    auto* ch = reinterpret_cast<CmChannel*>(handle);
    if (!ch || !value)
        return -1;

    pthread_mutex_lock(&ch->mutex);

    // バッファが空の間は待機（クローズされていなければ）
    while (ch->count == 0 && !ch->closed) {
        pthread_cond_wait(&ch->not_empty, &ch->mutex);
    }

    // クローズ済みかつ空 → 終了
    if (ch->count == 0 && ch->closed) {
        pthread_mutex_unlock(&ch->mutex);
        return -1;
    }

    // リングバッファから読み出し
    *value = ch->buffer[ch->head];
    ch->head = (ch->head + 1) % ch->capacity;
    ch->count--;

    // 送信待ちスレッドを起こす
    pthread_cond_signal(&ch->not_full);
    pthread_mutex_unlock(&ch->mutex);

    return 0;
}

// ノンブロッキング送信
// 戻り値: 0=成功, -1=クローズ済み, -2=バッファ満杯
int32_t cm_channel_try_send(int64_t handle, int64_t value) {
    auto* ch = reinterpret_cast<CmChannel*>(handle);
    if (!ch)
        return -1;

    pthread_mutex_lock(&ch->mutex);

    if (ch->closed) {
        pthread_mutex_unlock(&ch->mutex);
        return -1;
    }

    if (ch->count >= ch->capacity) {
        pthread_mutex_unlock(&ch->mutex);
        return -2;
    }

    ch->buffer[ch->tail] = value;
    ch->tail = (ch->tail + 1) % ch->capacity;
    ch->count++;

    pthread_cond_signal(&ch->not_empty);
    pthread_mutex_unlock(&ch->mutex);

    return 0;
}

// ノンブロッキング受信
// 戻り値: 0=成功, -1=クローズ済みかつ空, -2=空（未クローズ）
int32_t cm_channel_try_recv(int64_t handle, int64_t* value) {
    auto* ch = reinterpret_cast<CmChannel*>(handle);
    if (!ch || !value)
        return -1;

    pthread_mutex_lock(&ch->mutex);

    if (ch->count == 0) {
        pthread_mutex_unlock(&ch->mutex);
        return ch->closed ? -1 : -2;
    }

    *value = ch->buffer[ch->head];
    ch->head = (ch->head + 1) % ch->capacity;
    ch->count--;

    pthread_cond_signal(&ch->not_full);
    pthread_mutex_unlock(&ch->mutex);

    return 0;
}

// チャネルをクローズ
void cm_channel_close(int64_t handle) {
    auto* ch = reinterpret_cast<CmChannel*>(handle);
    if (!ch)
        return;

    pthread_mutex_lock(&ch->mutex);
    ch->closed = true;
    // 全待機スレッドを起こす
    pthread_cond_broadcast(&ch->not_full);
    pthread_cond_broadcast(&ch->not_empty);
    pthread_mutex_unlock(&ch->mutex);
}

// チャネル破棄
void cm_channel_destroy(int64_t handle) {
    auto* ch = reinterpret_cast<CmChannel*>(handle);
    if (!ch)
        return;

    pthread_mutex_destroy(&ch->mutex);
    pthread_cond_destroy(&ch->not_full);
    pthread_cond_destroy(&ch->not_empty);
    free(ch->buffer);
    free(ch);
}

// チャネル内の要素数を取得
int32_t cm_channel_len(int64_t handle) {
    auto* ch = reinterpret_cast<CmChannel*>(handle);
    if (!ch)
        return 0;

    pthread_mutex_lock(&ch->mutex);
    int32_t len = ch->count;
    pthread_mutex_unlock(&ch->mutex);

    return len;
}

// チャネルがクローズされているか
int32_t cm_channel_is_closed(int64_t handle) {
    auto* ch = reinterpret_cast<CmChannel*>(handle);
    if (!ch)
        return 1;

    pthread_mutex_lock(&ch->mutex);
    int32_t closed = ch->closed ? 1 : 0;
    pthread_mutex_unlock(&ch->mutex);

    return closed;
}

}  // extern "C"
