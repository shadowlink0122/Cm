// Cm Language Runtime - Format Core Implementation
// Portable formatting logic using platform abstraction

#ifndef CM_FORMAT_CORE_H
#define CM_FORMAT_CORE_H

#include "format_spec.h"
#include "runtime_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Integer to String Conversion (Portable)
// ============================================================
static inline size_t cm_int_to_buffer(int value, char* buffer) {
    int is_negative = 0;
    unsigned int uval;

    if (value < 0) {
        is_negative = 1;
        // Handle INT_MIN specially
        if (value == -2147483648) {
            const char* int_min = "-2147483648";
            size_t i = 0;
            while (int_min[i]) {
                buffer[i] = int_min[i];
                i++;
            }
            return i;
        }
        uval = (unsigned int)(-value);
    } else {
        uval = (unsigned int)value;
    }

    char temp[32];
    int i = 0;
    do {
        temp[i++] = '0' + (uval % 10);
        uval /= 10;
    } while (uval > 0);

    size_t j = 0;
    if (is_negative) {
        buffer[j++] = '-';
    }

    while (i > 0) {
        buffer[j++] = temp[--i];
    }

    return j;
}

static inline size_t cm_uint_to_buffer(unsigned int value, char* buffer) {
    char temp[32];
    int i = 0;
    do {
        temp[i++] = '0' + (value % 10);
        value /= 10;
    } while (value > 0);

    size_t j = 0;
    while (i > 0) {
        buffer[j++] = temp[--i];
    }

    return j;
}

static inline size_t cm_int64_to_buffer(long long value, char* buffer) {
    int is_negative = 0;
    unsigned long long uval;

    if (value < 0) {
        is_negative = 1;
        uval = (unsigned long long)(-value);
    } else {
        uval = (unsigned long long)value;
    }

    char temp[32];
    int i = 0;
    do {
        temp[i++] = '0' + (uval % 10);
        uval /= 10;
    } while (uval > 0);

    size_t j = 0;
    if (is_negative) {
        buffer[j++] = '-';
    }

    while (i > 0) {
        buffer[j++] = temp[--i];
    }

    return j;
}

// ============================================================
// Integer Format Variants (Portable)
// ============================================================
static inline size_t cm_int_to_hex_buffer(long long value, char* buffer, int uppercase) {
    const char* hex_chars = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    unsigned long long uval = (unsigned long long)value;

    if (uval == 0) {
        buffer[0] = '0';
        return 1;
    }

    char temp[32];
    int i = 0;
    while (uval > 0) {
        temp[i++] = hex_chars[uval % 16];
        uval /= 16;
    }

    size_t j = 0;
    while (i > 0) {
        buffer[j++] = temp[--i];
    }

    return j;
}

static inline size_t cm_int_to_binary_buffer(long long value, char* buffer) {
    unsigned long long uval = (unsigned long long)value;

    if (uval == 0) {
        buffer[0] = '0';
        return 1;
    }

    char temp[64];
    int i = 0;
    while (uval > 0) {
        temp[i++] = (uval % 2) ? '1' : '0';
        uval /= 2;
    }

    size_t j = 0;
    while (i > 0) {
        buffer[j++] = temp[--i];
    }

    return j;
}

static inline size_t cm_int_to_octal_buffer(long long value, char* buffer) {
    unsigned long long uval = (unsigned long long)value;

    if (uval == 0) {
        buffer[0] = '0';
        return 1;
    }

    char temp[32];
    int i = 0;
    while (uval > 0) {
        temp[i++] = '0' + (uval % 8);
        uval /= 8;
    }

    size_t j = 0;
    while (i > 0) {
        buffer[j++] = temp[--i];
    }

    return j;
}

// ============================================================
// Apply Padding/Alignment (Portable)
// ============================================================
static inline char* cm_apply_alignment(const char* value, size_t val_len,
                                       const CmFormatSpec* spec) {
    if (spec->width == 0 || (size_t)spec->width <= val_len) {
        // No padding needed
        char* result = (char*)cm_alloc(val_len + 1);
        if (result) {
            cm_memcpy(result, value, val_len);
            result[val_len] = '\0';
        }
        return result;
    }

    size_t width = (size_t)spec->width;
    char* result = (char*)cm_alloc(width + 1);
    if (!result)
        return NULL;

    size_t padding = width - val_len;
    char fill = spec->fill_char;

    switch (spec->align) {
        case CM_ALIGN_LEFT:
            cm_memcpy(result, value, val_len);
            cm_memset(result + val_len, fill, padding);
            break;
        case CM_ALIGN_RIGHT:
        case CM_ALIGN_NONE:  // Default is right-align
            cm_memset(result, fill, padding);
            cm_memcpy(result + padding, value, val_len);
            break;
        case CM_ALIGN_CENTER: {
            size_t left_pad = padding / 2;
            size_t right_pad = padding - left_pad;
            cm_memset(result, fill, left_pad);
            cm_memcpy(result + left_pad, value, val_len);
            cm_memset(result + left_pad + val_len, fill, right_pad);
            break;
        }
    }

    result[width] = '\0';
    return result;
}

// ============================================================
// Escape Processing (Portable)
// ============================================================
static inline char* cm_unescape_braces_impl(const char* str) {
    if (!str)
        return NULL;

    size_t len = cm_strlen(str);
    char* result = (char*)cm_alloc(len + 1);
    if (!result)
        return NULL;

    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '{' && i + 1 < len && str[i + 1] == '{') {
            result[j++] = '{';
            i++;
        } else if (str[i] == '}' && i + 1 < len && str[i + 1] == '}') {
            result[j++] = '}';
            i++;
        } else {
            result[j++] = str[i];
        }
    }
    result[j] = '\0';
    return result;
}

// ============================================================
// String Concatenation (Portable)
// ============================================================
static inline char* cm_string_concat_impl(const char* left, const char* right) {
    if (!left)
        left = "";
    if (!right)
        right = "";

    size_t len1 = cm_strlen(left);
    size_t len2 = cm_strlen(right);
    char* result = (char*)cm_alloc(len1 + len2 + 1);

    if (result) {
        cm_memcpy(result, left, len1);
        cm_memcpy(result + len1, right, len2);
        result[len1 + len2] = '\0';
    }

    return result;
}

// ============================================================
// Format Replace (Portable)
// ============================================================
static inline char* cm_format_replace_impl(const char* format, const char* value) {
    if (!format)
        return NULL;
    if (!value)
        value = "";

    size_t fmt_len = cm_strlen(format);
    size_t end_pos;
    int start = cm_find_placeholder(format, fmt_len, &end_pos);

    if (start < 0) {
        // No placeholder, copy format as-is
        return cm_strdup(format);
    }

    size_t val_len = cm_strlen(value);
    size_t placeholder_len = end_pos - (size_t)start + 1;
    size_t result_len = fmt_len - placeholder_len + val_len;

    char* result = (char*)cm_alloc(result_len + 1);
    if (!result)
        return NULL;

    // Copy prefix
    cm_memcpy(result, format, (size_t)start);
    // Copy value
    cm_memcpy(result + start, value, val_len);
    // Copy suffix
    cm_memcpy(result + start + val_len, format + end_pos + 1, fmt_len - end_pos - 1);
    result[result_len] = '\0';

    return result;
}

#ifdef __cplusplus
}
#endif

#endif  // CM_FORMAT_CORE_H
