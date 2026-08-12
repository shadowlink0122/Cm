// Cm Language Runtime - Low-Level I/O Functions
// v0.13.0: Reader/Writer interface support

#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef CM_NO_STD
#include <errno.h>
#include <stdio.h>
#endif

// ============================================================
// Low-Level Read/Write (file descriptor based)
// ============================================================

/// ファイルディスクリプタから読み取り
/// @param fd ファイルディスクリプタ
/// @param buf バッファポインタ
/// @param len 読み取りバイト数
/// @return 読み取ったバイト数、エラー時は-1
int cm_io_read(int fd, uint8_t* buf, int len) {
    if (fd < 0 || !buf || len <= 0) {
        return -1;
    }
    ssize_t n = read(fd, buf, (size_t)len);
    return (int)n;
}

/// ファイルディスクリプタに書き込み
/// @param fd ファイルディスクリプタ
/// @param buf バッファポインタ
/// @param len 書き込みバイト数
/// @return 書き込んだバイト数、エラー時は-1
int cm_io_write(int fd, const uint8_t* buf, int len) {
    if (fd < 0 || !buf || len <= 0) {
        return -1;
    }
    ssize_t n = write(fd, buf, (size_t)len);
    return (int)n;
}

// ============================================================
// File Open/Close
// ============================================================

/// ファイルを開く
/// @param path ファイルパス
/// @param flags オープンフラグ（O_RDONLY, O_WRONLY, O_RDWR等）
/// @param mode 作成時のパーミッション
/// @return ファイルディスクリプタ、エラー時は-1
int cm_io_open(const char* path, int flags, int mode) {
    if (!path) {
        return -1;
    }
    return open(path, flags, mode);
}

/// 読み取り専用で開く
int cm_io_open_read(const char* path) {
    return cm_io_open(path, O_RDONLY, 0);
}

/// 書き込み用に作成（既存は上書き）
int cm_io_open_write(const char* path) {
    return cm_io_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
}

/// 追記用に開く
int cm_io_open_append(const char* path) {
    return cm_io_open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
}

/// ファイルを閉じる
/// @param fd ファイルディスクリプタ
/// @return 成功時0、エラー時-1
int cm_io_close(int fd) {
    if (fd < 0) {
        return -1;
    }
    return close(fd);
}

// ============================================================
// Seek
// ============================================================

/// シーク操作
/// @param fd ファイルディスクリプタ
/// @param offset オフセット
/// @param whence 基準位置（SEEK_SET=0, SEEK_CUR=1, SEEK_END=2）
/// @return 新しい位置、エラー時は-1
int64_t cm_io_seek(int fd, int64_t offset, int whence) {
    if (fd < 0) {
        return -1;
    }
    return (int64_t)lseek(fd, (off_t)offset, whence);
}

// ============================================================
// File Metadata
// ============================================================

/// ファイルサイズを取得
int64_t cm_io_file_size(int fd) {
    if (fd < 0) {
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st) != 0) {
        return -1;
    }
    return (int64_t)st.st_size;
}

/// ファイルサイズをパスから取得
int64_t cm_io_file_size_path(const char* path) {
    if (!path) {
        return -1;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }
    return (int64_t)st.st_size;
}

// ============================================================
// Error Code
// ============================================================

#ifndef CM_NO_STD
/// 最後のエラーコードを取得
int cm_io_errno(void) {
    return errno;
}

/// エラーコードをリセット
void cm_io_clear_errno(void) {
    errno = 0;
}
#endif

// ============================================================
// Flush
// ============================================================

#ifndef CM_NO_STD
/// 標準出力をフラッシュ
void cm_io_flush_stdout(void) {
    fflush(stdout);
}

/// 標準エラー出力をフラッシュ
void cm_io_flush_stderr(void) {
    fflush(stderr);
}
#endif

/// ファイルディスクリプタをfsync
int cm_io_fsync(int fd) {
    if (fd < 0) {
        return -1;
    }
    return fsync(fd);
}

// ============================================================
// Standard Input
// ============================================================

#ifndef CM_NO_STD
#include <errno.h>
#include <stdlib.h>
#include <string.h>

// 内部: 1行読み取り（静的バッファ）
static char* io_read_line(void) {
    static char buffer[4096];
    if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
        buffer[0] = '\0';
        return NULL;
    }
    // 改行を除去
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
    }
    return buffer;
}

/// 標準入力から1行読み取り（後方互換性）
char* cm_io_input(void) {
    char* line = io_read_line();
    if (line == NULL) {
        static char empty[1] = "";
        return empty;
    }
    return line;
}

/// 標準入力から文字列読み取り（エラーハンドリング付き）
/// @param error 出力: 0=成功, 1=EOF/エラー
char* cm_io_input_string(int* error) {
    char* line = io_read_line();
    if (line == NULL) {
        *error = 1;
        static char empty[1] = "";
        return empty;
    }
    *error = 0;
    return line;
}

/// 標準入力から整数読み取り（後方互換性）
int cm_io_input_int(void) {
    char* line = io_read_line();
    if (line == NULL)
        return 0;
    return (int)atoi(line);
}

/// 標準入力からlong読み取り（エラーハンドリング付き）
/// @param error 出力: 0=成功, 1=パースエラー
int64_t cm_io_input_long(int* error) {
    char* line = io_read_line();
    if (line == NULL || line[0] == '\0') {
        *error = 1;
        return 0;
    }

    char* endptr;
    errno = 0;
    int64_t value = strtoll(line, &endptr, 10);

    // パースエラーチェック
    if (errno != 0 || endptr == line || *endptr != '\0') {
        *error = 1;
        return 0;
    }

    *error = 0;
    return value;
}

/// 標準入力からdouble読み取り（エラーハンドリング付き）
/// @param error 出力: 0=成功, 1=パースエラー
double cm_io_input_double(int* error) {
    char* line = io_read_line();
    if (line == NULL || line[0] == '\0') {
        *error = 1;
        return 0.0;
    }

    char* endptr;
    errno = 0;
    double value = strtod(line, &endptr);

    // パースエラーチェック
    if (errno != 0 || endptr == line || *endptr != '\0') {
        *error = 1;
        return 0.0;
    }

    *error = 0;
    return value;
}

/// 標準入力からbool読み取り（エラーハンドリング付き）
/// "true", "1", "yes" -> true, "false", "0", "no" -> false
/// @param error 出力: 0=成功, 1=パースエラー
bool cm_io_input_bool(int* error) {
    char* line = io_read_line();
    if (line == NULL || line[0] == '\0') {
        *error = 1;
        return false;
    }

    // 小文字に変換して比較
    if (strcmp(line, "true") == 0 || strcmp(line, "1") == 0 || strcmp(line, "yes") == 0) {
        *error = 0;
        return true;
    }
    if (strcmp(line, "false") == 0 || strcmp(line, "0") == 0 || strcmp(line, "no") == 0) {
        *error = 0;
        return false;
    }

    *error = 1;
    return false;
}
#endif
