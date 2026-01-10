/// @file runtime_file.h
/// @brief ファイル操作ランタイム関数宣言

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ファイル操作
void* cm_file_open(const char* path, const char* mode);
void cm_file_close(void* handle);
char* cm_file_read_all(const char* path);
bool cm_file_write_all(const char* path, const char* content);
bool cm_file_append(const char* path, const char* content);
bool cm_file_exists(const char* path);
bool cm_file_remove(const char* path);
long cm_file_size(const char* path);

// stdin入力
char* cm_read_line(void);
int cm_read_int(void);
char cm_read_char(void);

#ifdef __cplusplus
}
#endif
