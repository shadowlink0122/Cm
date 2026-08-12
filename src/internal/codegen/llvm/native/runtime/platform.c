// Cm Language Runtime - Native Platform Implementation
// Uses standard C library functions

#include "../../../common/runtime/platform.h"

#ifndef CM_NO_STD
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

// ============================================================
// String Operations
// ============================================================

#ifndef CM_NO_STD

size_t cm_strlen(const char* str) {
    return str ? strlen(str) : 0;
}

char* cm_strcpy(char* dst, const char* src) {
    return strcpy(dst, src);
}

char* cm_strncpy(char* dst, const char* src, size_t n) {
    return strncpy(dst, src, n);
}

char* cm_strcat(char* dst, const char* src) {
    return strcat(dst, src);
}

// Note: cm_strcmp and cm_strncmp are implemented in format.c
// as no_std-compatible versions

#endif  // !CM_NO_STD

// ============================================================
// I/O Operations
// ============================================================

#ifndef CM_NO_STD

void cm_write_stdout(const char* str, size_t len) {
    fwrite(str, 1, len, stdout);
    fflush(stdout);  // JIT実行時に出力が確実に表示されるようにflush
}

void cm_write_stderr(const char* str, size_t len) {
    fwrite(str, 1, len, stderr);
    fflush(stderr);
}

#endif  // !CM_NO_STD

// ============================================================
// OS連携（セルフホスト準備 第2段・第3段）
// ============================================================

#ifndef CM_NO_STD

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include <limits.h>
#include <stdint.h>
#include <unistd.h>

/// 実行ファイルの絶対パスを返す（失敗時NULL）。戻りは内部静的バッファ（Cm側で即コピーする）
const char* cm_current_exe(void) {
    static char resolved[PATH_MAX];
    static int cached = 0;
    if (cached)
        return resolved[0] ? resolved : NULL;
    cached = 1;
    resolved[0] = '\0';
#ifdef __APPLE__
    char raw[PATH_MAX];
    uint32_t size = sizeof(raw);
    if (_NSGetExecutablePath(raw, &size) == 0) {
        if (!realpath(raw, resolved))
            resolved[0] = '\0';
    }
#else
    ssize_t n = readlink("/proc/self/exe", resolved, sizeof(resolved) - 1);
    if (n > 0) {
        resolved[n] = '\0';
    } else {
        resolved[0] = '\0';
    }
#endif
    return resolved[0] ? resolved : NULL;
}

// コマンドライン引数（cm_args_initでエントリから保存し、std::env::args()が参照する）
static int cm_saved_argc = 0;
static char** cm_saved_argv = NULL;

void cm_args_init(int argc, char** argv) {
    cm_saved_argc = argc;
    cm_saved_argv = argv;
}

int cm_arg_count(void) {
    return cm_saved_argv ? cm_saved_argc : 0;
}

const char* cm_arg_get(int i) {
    if (!cm_saved_argv || i < 0 || i >= cm_saved_argc)
        return NULL;
    return cm_saved_argv[i];
}

#endif  // !CM_NO_STD
