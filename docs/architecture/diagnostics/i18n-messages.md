# 診断メッセージのi18n構成

Cmコンパイラのユーザー向けメッセージは、`enum MsgId × enum Lang`の2次元テーブル`kMessages[msg][lang]`へ全文を集約するC++内カタログで管理される。
本文書は、このテーブル設計、`msgf`と`{0}`プレースホルダによる書式化、断片連結を禁止する理由、および言語・メッセージ追加の手順を記述する。

## 概要

- メッセージIDは`enum class MsgId`（src/internal/base/messages/message_ids.hpp:20-330）、言語は`enum class Lang`（message_ids.hpp:12-16）で列挙され、本文は`kMessages[kMessageCount][kLangCount]`（src/internal/base/messages/messages.cpp:12）に「行=MsgIdの宣言順、列=Langの順（En, Ja）」で並ぶ。
- 定義ファイル（.def）やYAMLの外部カタログは採用していない。IDはenumなのでタイプミスはコンパイルエラーになり、行の過不足は配列サイズ`kMessageCount`との不一致としてコンパイル時に検出される（messages.cpp:1-3）。
- アクセサは`i18n::msg`（テンプレート取得）と`i18n::msgf`（プレースホルダ置換）で、いずれもsrc/internal/base/i18n.hppのインライン実装である。
- 英語が原文であり、訳の無い言語は`nullptr`にすると英語へフォールバックする（i18n.hpp:91-98）。全メッセージに英語本文が存在することは`static_assert`で保証される（messages.cpp:918-933）。
- 診断カタログ（E/W/L番号の体系。[correctness-lints.md](correctness-lints.md)参照）も本文をIDで持たず、`DiagnosticDefinition::message_id`として`MsgId`を参照し、`DiagnosticEngine::report`が現在言語のテンプレートを引いてフォーマットする（src/internal/diagnostics/catalog.hpp:32、src/internal/diagnostics/engine.cpp:33-35）。

## データ構造とアルゴリズム

### 2次元テーブルと解決

```cpp
// src/internal/base/messages/messages.cpp:12-16（抜粋）
constexpr const char* kMessages[kMessageCount][kLangCount] = {
    // ===== cli =====
    // CliAsyncAwaitIsOnlySupported
    {"error: async/await is only supported on the JS target\n",
     "エラー: async/awaitはJSターゲット専用の機能です\n"},
```

解決は現在言語→英語の2段フォールバックである。

```cpp
// src/internal/base/i18n.hpp:91-98
inline const char* msg(MsgId id) {
    const size_t row = static_cast<size_t>(id);
    const char* text = kMessages[row][static_cast<size_t>(current_lang())];
    if (text) {
        return text;
    }
    return kMessages[row][static_cast<size_t>(Lang::En)];
}
```

現在言語はプロセス全体で共有される関数ローカルstatic（i18n.hpp:42-45）で、優先順位は「環境変数`CM_LANG` → `.cmconfig.yml`の`language` → CLIの`--lang=`」の後勝ちである（src/cmd/cm/main.cpp:25-49、src/cmd/cm/options.cpp:73、231）。

### msgfとプレースホルダ書式

テンプレート中の`{0}` `{1}` ...を可変長引数で置換する。引数は文字列・文字・数値を`detail::to_display`で文字列化する（i18n.hpp:22-39）。

```cpp
// src/internal/base/i18n.hpp:101-114（要約）
template <typename... Args>
inline std::string msgf(MsgId id, Args&&... args) {
    std::string text = msg(id);
    const std::string values[] = {detail::to_display(std::forward<Args>(args))...};
    for (size_t i = 0; i < sizeof...(Args); ++i) {
        const std::string placeholder = "{" + std::to_string(i) + "}";
        // text内の全出現を values[i] で置換
    }
    return text;
}
```

診断カタログ側にも同じ`{N}`書式の`format_message`があり（src/internal/diagnostics/catalog.cpp:17-28）、`DiagnosticEngine::report`が`i18n::msg(def->message_id)`で引いたテンプレートへ適用する（engine.cpp:33-35）。

### 断片連結（msgf/{0}での文の組み立て）を禁止する理由

「`"変数 " + name + " は未定義"`のように文を断片で連結する」「1つの文を複数のMsgIdに分割して順に出力する」方式は禁止である。

- 言語によって語順が異なるため、断片の結合順を固定すると訳文が成立しない。`{0}`プレースホルダはテンプレート側が挿入位置を決めるので、言語ごとの語順（例: TypeGenericFunctionArgumentCountMismatchの英日で`{1}`/`{2}`の位置が異なる。messages.cpp:891-893）に対応できる。
- 断片単位のIDはカタログ上で意味を成さず、翻訳者（および機械検証）が完全な文を確認できない。
- 断片連結はメッセージの全文検索・整合チェック（英語本文の存在検証等）をすり抜ける。

### メッセージIDの命名とカテゴリ

`MsgId`は`Cli`/`Codegen`/`Diag`/`Fmt`/`Js`/`Lint`/`Module`/`Nostd`/`Parse`/`Sv`/`Type`/`Import`のプレフィックスでカテゴリ分けされ、カテゴリ内はコメント見出し（`// ===== cli =====`等）で区切られる（message_ids.hpp:20-330）。
`DiagE001`〜`DiagL402`はE/W/L診断コードの本文専用IDで、診断カタログの定義（src/internal/diagnostics/definitions/）から参照される。
ヘルプ本文のような長文はテーブルに置かず、`src/cli/help_<lang>.txt`をビルド時埋め込みした`textdata::kCatalogs`から言語コードで引く（i18n.hpp:80-88、src/internal/base/text_data.hpp.in）。

## 実装箇所

| ファイル | 役割 |
|---|---|
| src/internal/base/messages/message_ids.hpp | `Lang`・`MsgId`の列挙と`kLangCount`/`kMessageCount` |
| src/internal/base/messages/messages.hpp | `kMessages`テーブルのextern宣言 |
| src/internal/base/messages/messages.cpp | 本文テーブルの定義と英語本文存在の`static_assert` |
| src/internal/base/i18n.hpp | `msg`/`msgf`/言語切替（`set_language_from_string`・`language_code`）・ヘルプ本文解決 |
| src/internal/base/text_data.hpp.in | ヘルプ本文カタログのビルド時生成テンプレート |
| src/internal/diagnostics/catalog.hpp・catalog.cpp | 診断定義が`MsgId`を参照する接続点と`format_message` |
| src/internal/diagnostics/engine.cpp | `report`での言語解決とフォーマット |
| src/cmd/cm/main.cpp・options.cpp | `CM_LANG`/`.cmconfig.yml`/`--lang=`による言語決定 |

## 落とし穴とケア

- **メッセージ追加の手順**: (1) message_ids.hppの該当カテゴリ位置へIDを1つ追加し、(2) messages.cppの**同じ位置**へ`{英語, 日本語}`の行を追加する。行=宣言順の対応が唯一の紐付けなので、位置がずれると別IDの本文を返す（コンパイルは通る）。行を挟む位置を必ずID挿入位置と一致させ、直前のコメント（`// CliXxx`）でIDを明記すること。
- **行の過不足はコンパイル時に落ちる**: 要素数が`kMessageCount`と合わなければ配列初期化エラー、英語本文が`nullptr`なら`static_assert`（messages.cpp:932-933）で落ちる。この検証を弱める変更（テーブルの動的構築等）をしてはならない。
- **言語追加の手順**: (1) `Lang`へ列挙子を`Count`の前に追加し、(2) messages.cppの全行へ列を追加（訳が無い分は`nullptr`で英語フォールバック）、(3) i18n.hppの`set_language_from_string`と`language_code`へコードを追加、(4) ヘルプ本文`src/cli/help_<lang>.txt`を追加する。列順は`Lang`の宣言順と一致させる。
- **`msg`の戻り値は`const char*`のテンプレート**: プレースホルダ入りのまま表示してはならず、引数を取るメッセージは必ず`msgf`を通す。逆に`msgf`へテンプレートに無い余剰引数を渡しても置換されず残らないが、引数不足だと`{N}`がそのまま表示される。
- **ソース内へ英文をハードコードしない**: `std::cerr << "error: ..."`の直書きはカタログの言語切替・検証の対象外になる。ユーザー向け文言は必ずMsgIdを新設して`msg`/`msgf`経由で出す。
- **回帰テスト**: tests/unit/i18n_test.cpp（言語切替・テーブル解決・英語フォールバックの単体検証）と、tests/i18n/run_tests.sh（実バイナリで`CM_LANG=ja`等を切り替えて診断文言を検証する機能テスト）。

## 関連資料

- [correctness-lints.md](correctness-lints.md) — E/W/L診断コード体系と`MsgId`の接続
- [../../archive/v0.17.0/diagnostics/misc-diagnostics-and-low-priority.md](../../archive/v0.17.0/diagnostics/misc-diagnostics-and-low-priority.md) — 診断まわりの監査所見と対応記録
- [../../archive/v0.16.2/04_message_i18n.md](../../archive/v0.16.2/04_message_i18n.md) — i18n基盤の当初設計（実装済みアーカイブ）
