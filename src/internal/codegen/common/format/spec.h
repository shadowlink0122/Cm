// Cm Language Runtime - Format Specification Parser
// Shared logic for parsing format specifiers like {:x}, {:.2f}, {:>10}

#ifndef CM_FORMAT_SPEC_H
#define CM_FORMAT_SPEC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Format specifier types
typedef enum {
    CM_FMT_DEFAULT = 0,  // Default formatting
    CM_FMT_HEX_LOWER,    // :x - lowercase hex
    CM_FMT_HEX_UPPER,    // :X - uppercase hex
    CM_FMT_BINARY,       // :b - binary
    CM_FMT_OCTAL,        // :o - octal
    CM_FMT_EXP_LOWER,    // :e - scientific notation (lowercase)
    CM_FMT_EXP_UPPER,    // :E - scientific notation (uppercase)
    CM_FMT_PRECISION     // :.Nf - precision
} CmFormatType;

// Alignment types
typedef enum {
    CM_ALIGN_NONE = 0,
    CM_ALIGN_LEFT,   // :<
    CM_ALIGN_RIGHT,  // :>
    CM_ALIGN_CENTER  // :^
} CmAlignType;

// Parsed format specification
typedef struct {
    CmFormatType type;
    CmAlignType align;
    char fill_char;  // Fill character for alignment (default: ' ')
    int width;       // Minimum width (0 = no width)
    int precision;   // Precision for floats (-1 = default)
    int zero_pad;    // Zero padding flag
} CmFormatSpec;

// Initialize format spec with defaults
static inline void cm_format_spec_init(CmFormatSpec* spec) {
    spec->type = CM_FMT_DEFAULT;
    spec->align = CM_ALIGN_NONE;
    spec->fill_char = ' ';
    spec->width = 0;
    spec->precision = -1;
    spec->zero_pad = 0;
}

// Parse format specifier from string
// Input: pointer to characters after ':' in "{:...}"
// Returns: number of characters consumed
static inline size_t cm_parse_format_spec(const char* spec_str, size_t spec_len,
                                          CmFormatSpec* out) {
    cm_format_spec_init(out);

    if (spec_len == 0 || !spec_str) {
        return 0;
    }

    size_t i = 0;

    // Check for fill character and alignment
    // Pattern: [fill][align] where align is <, >, or ^
    if (spec_len >= 2) {
        char c1 = spec_str[0];
        char c2 = spec_str[1];

        if (c2 == '<' || c2 == '>' || c2 == '^') {
            // c1 is fill character
            out->fill_char = c1;
            if (c1 == '0')
                out->zero_pad = 1;
            out->align = (c2 == '<')   ? CM_ALIGN_LEFT
                         : (c2 == '>') ? CM_ALIGN_RIGHT
                                       : CM_ALIGN_CENTER;
            i = 2;
        } else if (c1 == '<' || c1 == '>' || c1 == '^') {
            out->align = (c1 == '<')   ? CM_ALIGN_LEFT
                         : (c1 == '>') ? CM_ALIGN_RIGHT
                                       : CM_ALIGN_CENTER;
            i = 1;
        }
    } else if (spec_len >= 1) {
        char c1 = spec_str[0];
        if (c1 == '<' || c1 == '>' || c1 == '^') {
            out->align = (c1 == '<')   ? CM_ALIGN_LEFT
                         : (c1 == '>') ? CM_ALIGN_RIGHT
                                       : CM_ALIGN_CENTER;
            i = 1;
        }
    }

    // Parse width
    while (i < spec_len && spec_str[i] >= '0' && spec_str[i] <= '9') {
        out->width = out->width * 10 + (spec_str[i] - '0');
        i++;
    }

    // Parse precision (.N)
    if (i < spec_len && spec_str[i] == '.') {
        i++;
        out->precision = 0;
        while (i < spec_len && spec_str[i] >= '0' && spec_str[i] <= '9') {
            out->precision = out->precision * 10 + (spec_str[i] - '0');
            i++;
        }
    }

    // Parse type specifier
    if (i < spec_len) {
        switch (spec_str[i]) {
            case 'x':
                out->type = CM_FMT_HEX_LOWER;
                i++;
                break;
            case 'X':
                out->type = CM_FMT_HEX_UPPER;
                i++;
                break;
            case 'b':
                out->type = CM_FMT_BINARY;
                i++;
                break;
            case 'o':
                out->type = CM_FMT_OCTAL;
                i++;
                break;
            case 'e':
                out->type = CM_FMT_EXP_LOWER;
                i++;
                break;
            case 'E':
                out->type = CM_FMT_EXP_UPPER;
                i++;
                break;
            case 'f':
                out->type = CM_FMT_PRECISION;
                i++;
                break;
            default:
                break;
        }
    }

    return i;
}

// Find placeholder in format string
// Returns: start position of '{', or -1 if not found
// Sets end_pos to position of '}'
static inline int cm_find_placeholder(const char* format, size_t len, size_t* end_pos) {
    for (size_t i = 0; i < len; i++) {
        if (format[i] == '{') {
            // Check for escaped brace {{
            if (i + 1 < len && format[i + 1] == '{') {
                continue;
            }
            // Find closing brace
            for (size_t j = i + 1; j < len; j++) {
                if (format[j] == '}') {
                    *end_pos = j;
                    return (int)i;
                }
            }
        }
    }
    return -1;
}

// Extract format spec from placeholder
// Input: "{:spec}" or "{name:spec}"
// Returns: pointer to spec (after ':'), or NULL if no spec
static inline const char* cm_extract_spec(const char* placeholder, size_t len, size_t* spec_len) {
    for (size_t i = 0; i < len; i++) {
        if (placeholder[i] == ':') {
            *spec_len = len - i - 1;
            return &placeholder[i + 1];
        }
    }
    *spec_len = 0;
    return NULL;
}

#ifdef __cplusplus
}
#endif

#endif  // CM_FORMAT_SPEC_H
