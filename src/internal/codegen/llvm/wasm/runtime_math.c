// Cm WASMランタイム: libm相当の数学関数
// wasmにはfrem命令が無く、LLVMは浮動小数の`%`をfmod/fmodfのライブラリコールへ落とす。
// wasm32-wasiは-nostdlibでlibmを持たないため、ここで自前定義してwasm-ldで解決させる（O3では定数畳み込みで消えるがO0では必須）。
// 実装はビット操作による正確な剰余（musl方式）。x - trunc(x/y)*y の近似は商が大きい場合に下位ビットを失うため採用しない

#include <stdint.h>

// fmod: doubleの正確な剰余（IEEE754 binary64のビット操作実装）
double fmod(double x, double y) {
    union {
        double f;
        uint64_t i;
    } ux = {x}, uy = {y};
    int ex = (int)(ux.i >> 52 & 0x7ff);
    int ey = (int)(uy.i >> 52 & 0x7ff);
    int sx = (int)(ux.i >> 63);
    uint64_t i;
    uint64_t uxi = ux.i;

    // y=0・yがNaN・xがInf/NaNはNaNを返す
    if (uy.i << 1 == 0 || (y != y) || ex == 0x7ff)
        return (x * y) / (x * y);
    if (uxi << 1 <= uy.i << 1) {
        if (uxi << 1 == uy.i << 1)
            return 0 * x;
        return x;
    }

    // xとyの仮数部を正規化する（非正規化数は指数を下げつつ左詰め）
    if (!ex) {
        for (i = uxi << 12; i >> 63 == 0; ex--, i <<= 1)
            ;
        uxi <<= -ex + 1;
    } else {
        uxi &= (uint64_t)-1 >> 12;
        uxi |= 1ULL << 52;
    }
    if (!ey) {
        for (i = uy.i << 12; i >> 63 == 0; ey--, i <<= 1)
            ;
        uy.i <<= -ey + 1;
    } else {
        uy.i &= (uint64_t)-1 >> 12;
        uy.i |= 1ULL << 52;
    }

    // 筆算方式でx mod yを求める
    for (; ex > ey; ex--) {
        i = uxi - uy.i;
        if (i >> 63 == 0) {
            if (i == 0)
                return 0 * x;
            uxi = i;
        }
        uxi <<= 1;
    }
    i = uxi - uy.i;
    if (i >> 63 == 0) {
        if (i == 0)
            return 0 * x;
        uxi = i;
    }
    for (; uxi >> 52 == 0; uxi <<= 1, ex--)
        ;

    // 指数を戻して結果をスケールする（指数が尽きたら非正規化数へ）
    if (ex > 0) {
        uxi -= 1ULL << 52;
        uxi |= (uint64_t)ex << 52;
    } else {
        uxi >>= -ex + 1;
    }
    uxi |= (uint64_t)sx << 63;
    ux.i = uxi;
    return ux.f;
}

// fmodf: floatの正確な剰余（IEEE754 binary32のビット操作実装）
float fmodf(float x, float y) {
    union {
        float f;
        uint32_t i;
    } ux = {x}, uy = {y};
    int ex = (int)(ux.i >> 23 & 0xff);
    int ey = (int)(uy.i >> 23 & 0xff);
    uint32_t sx = ux.i & 0x80000000u;
    uint32_t i;
    uint32_t uxi = ux.i;

    if (uy.i << 1 == 0 || (y != y) || ex == 0xff)
        return (x * y) / (x * y);
    if (uxi << 1 <= uy.i << 1) {
        if (uxi << 1 == uy.i << 1)
            return 0 * x;
        return x;
    }

    if (!ex) {
        for (i = uxi << 9; i >> 31 == 0; ex--, i <<= 1)
            ;
        uxi <<= -ex + 1;
    } else {
        uxi &= (uint32_t)-1 >> 9;
        uxi |= 1U << 23;
    }
    if (!ey) {
        for (i = uy.i << 9; i >> 31 == 0; ey--, i <<= 1)
            ;
        uy.i <<= -ey + 1;
    } else {
        uy.i &= (uint32_t)-1 >> 9;
        uy.i |= 1U << 23;
    }

    for (; ex > ey; ex--) {
        i = uxi - uy.i;
        if (i >> 31 == 0) {
            if (i == 0)
                return 0 * x;
            uxi = i;
        }
        uxi <<= 1;
    }
    i = uxi - uy.i;
    if (i >> 31 == 0) {
        if (i == 0)
            return 0 * x;
        uxi = i;
    }
    for (; uxi >> 23 == 0; uxi <<= 1, ex--)
        ;

    if (ex > 0) {
        uxi -= 1U << 23;
        uxi |= (uint32_t)ex << 23;
    } else {
        uxi >>= -ex + 1;
    }
    uxi |= sx;
    ux.i = uxi;
    return ux.f;
}
