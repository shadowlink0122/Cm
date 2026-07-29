/// @file runtime_file.c
/// @brief ファイル操作ランタイム関数

#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
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
/// remove(3)ではなくunlink(2)を使用する: Cm側のstd::fsがremoveという名前の
/// 関数をエクスポートするため、ネイティブリンク時にlibcのremoveが隠蔽されて
/// 無限再帰（cm_file_remove→Cmのremove→cm_file_remove）になるのを防ぐ
bool cm_file_remove(const char* path) {
    if (!path)
        return false;
    return unlink(path) == 0;
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

// ============================================================
// ディレクトリ列挙・バイナリ安全I/O（セルフホスト準備 第2段）
// ============================================================

// CmSliceレイアウト（runtime_slice.cと同一。ヘッダ共有を避けるためのローカル定義）
typedef struct {
    void* data;
    int64_t len;
    int64_t cap;
    int64_t elem_size;
} CmFileSlice;

/// ディレクトリを開く（direntをCm側へ出さないためのハンドルシム）。失敗時NULL
void* cm_dir_open(const char* path) {
    if (!path)
        return NULL;
    return (void*)opendir(path);
}

/// 次のエントリ名を返す。"."と".."はスキップし、終端はNULL
/// 戻りポインタは次のcm_dir_next/cm_dir_close呼び出しまで有効（Cm側で即コピーする）
const char* cm_dir_next(void* handle) {
    if (!handle)
        return NULL;
    struct dirent* ent;
    while ((ent = readdir((DIR*)handle)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        return ent->d_name;
    }
    return NULL;
}

/// ディレクトリハンドルを閉じる
void cm_dir_close(void* handle) {
    if (handle) {
        closedir((DIR*)handle);
    }
}

/// ファイル全体をバイト列（utiny[]スライス）として読む。失敗時は空スライス
/// 長さはstatで取るため埋め込みNUL・非UTF-8データも欠損しない
void* cm_file_read_bytes(const char* path) {
    CmFileSlice* slice = (CmFileSlice*)cm_alloc(sizeof(CmFileSlice));
    slice->data = NULL;
    slice->len = 0;
    slice->cap = 0;
    slice->elem_size = 1;
    if (!path)
        return slice;

    FILE* file = fopen(path, "rb");
    if (!file)
        return slice;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size < 0 || size > CM_MAX_FILE_SIZE) {
        fclose(file);
        return slice;
    }
    if (size > 0) {
        char* buf = (char*)cm_alloc(size);
        size_t read_size = fread(buf, 1, size, file);
        slice->data = buf;
        slice->len = (int64_t)read_size;
        slice->cap = (int64_t)size;
    }
    fclose(file);
    return slice;
}

/// バイト列（utiny[]スライス）をファイルへ書き込む（上書き）。長さ明示のため埋め込みNULで切れない
bool cm_file_write_bytes(const char* path, void* slice_ptr) {
    if (!path || !slice_ptr)
        return false;
    CmFileSlice* slice = (CmFileSlice*)slice_ptr;
    FILE* file = fopen(path, "wb");
    if (!file)
        return false;
    size_t written = 0;
    if (slice->data && slice->len > 0) {
        written = fwrite(slice->data, 1, (size_t)slice->len, file);
    }
    fclose(file);
    return written == (size_t)(slice->len > 0 ? slice->len : 0);
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
