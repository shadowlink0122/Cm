// ============================================================
// Lint設定システム
// ============================================================
// .cmconfig.yml からルール設定を読み込む
// Phase 4: 設定ファイルサポート

#pragma once

#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>

namespace cm {
namespace lint {

// ルールレベル
enum class RuleLevel {
    Error,    // error
    Warning,  // warning
    Hint,     // hint
    Disabled  // disabled
};

// プリセット種別
enum class Preset {
    None,         // プリセットなし
    Minimal,      // 最小限（すべて無効）
    Recommended,  // 推奨（すべてwarning）
    Strict        // 厳格（lint=error）
};

// ルール設定
struct RuleConfig {
    RuleLevel level = RuleLevel::Warning;
};

// 設定ローダー
class ConfigLoader {
   public:
    // .cmconfig.yml を読み込み
    bool load(const std::string& filepath);

    // .cmconfig.yml を探す（カレントディレクトリから親に向かって）
    bool find_and_load(const std::string& start_path = ".");

    // ルールが無効化されているか
    bool is_disabled(const std::string& rule_id) const;

    // ルールのレベルを取得（デフォルトはwarning）
    RuleLevel get_level(const std::string& rule_id) const;

    // 設定が読み込まれているか
    bool is_loaded() const { return loaded_; }

    // 設定ファイルのパスを取得
    const std::string& config_path() const { return config_path_; }

    // プリセットを適用
    void apply_preset(Preset preset);

    // ソースコードからコメントを解析して無効化行を登録
    // @cm-disable-next-line [rule_id, ...]
    // @cm-disable-line [rule_id, ...]
    void parse_disable_comments(const std::string& source);

    // 指定行でルールが無効化されているかチェック
    bool is_line_disabled(uint32_t line, const std::string& rule_id) const;

    // 行ごとの無効化情報をクリア
    void clear_line_disables() { disabled_lines_.clear(); }

   private:
    // 簡易YAMLパーサー（key: value形式のみ）
    bool parse_yaml(const std::string& content);

    // レベル文字列をenumに変換
    static RuleLevel parse_level(const std::string& level_str);

    // プリセット文字列をenumに変換
    static Preset parse_preset(const std::string& preset_str);

    // 行をトリム
    static std::string trim(const std::string& str);

    std::unordered_map<std::string, RuleConfig> rules_;
    std::string config_path_;
    bool loaded_ = false;
    Preset current_preset_ = Preset::None;

    // 行ごとの無効化ルール (行番号 -> ルールIDセット)
    // "*" は全ルール無効化を意味する
    std::unordered_map<uint32_t, std::set<std::string>> disabled_lines_;
};

}  // namespace lint
}  // namespace cm
