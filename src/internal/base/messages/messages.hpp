#pragma once

// Cmコンパイラ メッセージカタログ（table[メッセージ][言語]）の宣言。本文テーブルの定義は messages.cpp で管理する
// テンプレートの {0} {1} ... は i18n::msgf のプレースホルダ。訳が無い言語は nullptr にすると英語へフォールバックする

#include "internal/base/messages/ids.hpp"

namespace cm::i18n {

// 本文テーブル（行 = MsgId の宣言順、列 = Lang の順（En, Ja））。定義と検証用static_assertは messages.cpp を参照
extern const char* const kMessages[kMessageCount][kLangCount];

}  // namespace cm::i18n
