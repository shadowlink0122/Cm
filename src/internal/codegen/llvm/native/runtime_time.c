// ============================================================
// Cm Language Runtime - 時刻関数
// std::core::time の cm_now_ms を提供する（R9）。
// runtime_event_loop.c にも同名実装があるが、イベントループはconstructor/destructorが
// 終了時の既存チャネル解放とヒープ配置の相互作用でクラッシュを誘発するため、
// コアランタイムには時刻関数のみを包含する（イベントループはasyncのnative対応時に統合を再検討）
// ============================================================

#include <stdint.h>
#include <time.h>

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

// 現在時刻を取得（ミリ秒・モノトニック）
uint64_t cm_now_ms(void) {
#ifdef __APPLE__
    static mach_timebase_info_data_t timebase = {0};
    if (timebase.denom == 0) {
        mach_timebase_info(&timebase);
    }
    uint64_t t = mach_absolute_time();
    return (t * timebase.numer / timebase.denom) / 1000000ULL;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000L);
#endif
}
