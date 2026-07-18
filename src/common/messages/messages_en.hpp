#pragma once

// Cmコンパイラ メッセージカタログ（英語 = 原文）。message_list.def から enum と同順で生成されるため、
// MsgId の整数値でそのまま引ける（順序ずれはあり得ない）

#include "message_ids.hpp"

namespace cm::i18n {

inline constexpr const char* kMessagesEn[] = {
#define CM_MSG(id, text) text,
#include "message_list.def"
#undef CM_MSG
};

static_assert(sizeof(kMessagesEn) / sizeof(kMessagesEn[0]) == kMessageCount,
              "message_list.def と MsgId の件数が一致しません");

}  // namespace cm::i18n
