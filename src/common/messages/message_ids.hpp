#pragma once

// メッセージID（message_list.def から生成される列挙。IDと英語原文の対応はdefファイルが単一ソース）

#include <cstddef>

namespace cm::i18n {

enum class MsgId : int {
#define CM_MSG(id, text) id,
#include "message_list.def"
#undef CM_MSG
    Count,
};

inline constexpr size_t kMessageCount = static_cast<size_t>(MsgId::Count);

}  // namespace cm::i18n
