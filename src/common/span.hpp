#pragma once

#include <cstdint>
#include <string>

namespace cm {

/// ソースコード内の位置情報
struct Span {
    uint32_t start;  // 開始オフセット（バイト）
    uint32_t end;    // 終了オフセット（バイト）

    static Span empty() { return Span{0, 0}; }

    Span merge(const Span& other) const {
        return Span{std::min(start, other.start), std::max(end, other.end)};
    }

    uint32_t length() const { return end - start; }
    bool is_empty() const { return start == end; }
};

/// 行・列情報（エラー表示用）
struct LineColumn {
    uint32_t line;    // 1-indexed
    uint32_t column;  // 1-indexed
};

}  // namespace cm
