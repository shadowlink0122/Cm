// ============================================================
// Lint設定システム
// ============================================================
// .cmconfig.yml からルール設定を読み込む
// Phase 4: 設定ファイルサポート

#pragma once

#include <fstream>
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

   private:
    // 簡易YAMLパーサー（key: value形式のみ）
    bool parse_yaml(const std::string& content);

    // レベル文字列をenumに変換
    static RuleLevel parse_level(const std::string& level_str);

    // 行をトリム
    static std::string trim(const std::string& str);

    std::unordered_map<std::string, RuleConfig> rules_;
    std::string config_path_;
    bool loaded_ = false;
};

}  // namespace lint
}  // namespace cm
