/// @file runtime_file.c
/// @brief ファイル操作ランタイム関数

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// メモリアロケータ（runtime_alloc.hから）
extern void* cm_alloc(size_t size);
extern void cm_dealloc(void* ptr);

// 最大ファイルサイズ（10MB）
#define CM_MAX_FILE_SIZE (10 * 1024 * 1024)

/// ファイルを開く（ハンドルを返す）
void* cm_file_open(const char* path, const char* mode) {
    if (!path || !mode)
        return NULL;
    return (void*)fopen(path, mode);
}

/// ファイルを閉じる
void cm_file_close(void* handle) {
    if (handle) {
        fclose((FILE*)handle);
    }
}

/// ファイル全体を読み込み（文字列として返す）
/// @return 成功時は文字列ポインタ（呼び出し側が解放）、失敗時はNULLまたは空文字列
char* cm_file_read_all(const char* path) {
    if (!path) {
        char* empty = (char*)cm_alloc(1);
        empty[0] = '\0';
        return empty;
    }

    FILE* file = fopen(path, "rb");
    if (!file) {
        char* empty = (char*)cm_alloc(1);
        empty[0] = '\0';
        return empty;
    }

    // ファイルサイズを取得
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // サイズ制限チェック
    if (size < 0 || size > CM_MAX_FILE_SIZE) {
        fclose(file);
        char* empty = (char*)cm_alloc(1);
        empty[0] = '\0';
        return empty;
    }

    // メモリ確保
    char* content = (char*)cm_alloc(size + 1);
    if (!content) {
        fclose(file);
        return NULL;
    }

    // 読み込み
    size_t read_size = fread(content, 1, size, file);
    content[read_size] = '\0';

    fclose(file);
    return content;
}

/// ファイルに書き込み（上書き）
bool cm_file_write_all(const char* path, const char* content) {
    if (!path || !content)
        return false;

    FILE* file = fopen(path, "wb");
    if (!file)
        return false;

    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, file);

    fclose(file);
    return written == len;
}

/// ファイルに追記
bool cm_file_append(const char* path, const char* content) {
    if (!path || !content)
        return false;

    FILE* file = fopen(path, "ab");
    if (!file)
        return false;

    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, file);

    fclose(file);
    return written == len;
}

/// ファイルの存在確認
bool cm_file_exists(const char* path) {
    if (!path)
        return false;
    return access(path, F_OK) == 0;
}

/// ファイル削除
bool cm_file_remove(const char* path) {
    if (!path)
        return false;
    return remove(path) == 0;
}

/// ファイルサイズを取得
long cm_file_size(const char* path) {
    if (!path)
        return -1;

    struct stat st;
    if (stat(path, &st) != 0)
        return -1;

    return st.st_size;
}

/// 1行読み込み（stdin）
char* cm_read_line(void) {
    char buffer[4096];

    if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
        char* empty = (char*)cm_alloc(1);
        empty[0] = '\0';
        return empty;
    }

    // 改行を削除
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
        len--;
    }

    // 文字列をコピー
    char* result = (char*)cm_alloc(len + 1);
    strcpy(result, buffer);
    return result;
}

/// 整数読み込み（stdin）
int cm_read_int(void) {
    int value = 0;
    if (scanf("%d", &value) != 1) {
        return 0;
    }
    // 残りの改行を消費
    int c;
    while ((c = getchar()) != '\n' && c != EOF)
        ;
    return value;
}

/// 文字読み込み（stdin）
char cm_read_char(void) {
    int c = getchar();
    // 残りの改行を消費
    if (c != '\n') {
        int next;
        while ((next = getchar()) != '\n' && next != EOF)
            ;
    }
    return (char)(c == EOF ? '\0' : c);
}
