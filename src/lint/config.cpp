// ============================================================
// Lint設定システム - 実装
// ============================================================

#include "config.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace cm {
namespace lint {

bool ConfigLoader::load(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    if (parse_yaml(buffer.str())) {
        config_path_ = filepath;
        loaded_ = true;
        return true;
    }
    return false;
}

bool ConfigLoader::find_and_load(const std::string& start_path) {
    fs::path current = fs::absolute(start_path);

    // 最大10レベルまで親ディレクトリを探索
    for (int i = 0; i < 10; ++i) {
        fs::path config_file = current / ".cmconfig.yml";
        if (fs::exists(config_file)) {
            return load(config_file.string());
        }

        // 親ディレクトリへ
        fs::path parent = current.parent_path();
        if (parent == current) {
            break;  // ルートに到達
        }
        current = parent;
    }

    return false;
}

bool ConfigLoader::is_disabled(const std::string& rule_id) const {
    auto it = rules_.find(rule_id);
    if (it != rules_.end()) {
        return it->second.level == RuleLevel::Disabled;
    }
    return false;
}

RuleLevel ConfigLoader::get_level(const std::string& rule_id) const {
    auto it = rules_.find(rule_id);
    if (it != rules_.end()) {
        return it->second.level;
    }
    return RuleLevel::Warning;  // デフォルト
}

void ConfigLoader::apply_preset(Preset preset) {
    current_preset_ = preset;

    // プリセットに基づいてルールを設定
    switch (preset) {
        case Preset::Minimal:
            // すべて無効
            rules_["W001"] = {RuleLevel::Disabled};
            rules_["L100"] = {RuleLevel::Disabled};
            rules_["L101"] = {RuleLevel::Disabled};
            rules_["L102"] = {RuleLevel::Disabled};
            rules_["L103"] = {RuleLevel::Disabled};
            break;

        case Preset::Recommended:
            // すべてwarning（デフォルト）
            rules_["W001"] = {RuleLevel::Warning};
            rules_["L100"] = {RuleLevel::Warning};
            rules_["L101"] = {RuleLevel::Warning};
            rules_["L102"] = {RuleLevel::Warning};
            rules_["L103"] = {RuleLevel::Warning};
            break;

        case Preset::Strict:
            // W001はwarning、L100-L103はerror
            rules_["W001"] = {RuleLevel::Warning};
            rules_["L100"] = {RuleLevel::Error};
            rules_["L101"] = {RuleLevel::Error};
            rules_["L102"] = {RuleLevel::Error};
            rules_["L103"] = {RuleLevel::Error};
            break;

        case Preset::None:
        default:
            // 何もしない
            break;
    }
}

Preset ConfigLoader::parse_preset(const std::string& preset_str) {
    if (preset_str == "minimal")
        return Preset::Minimal;
    if (preset_str == "recommended")
        return Preset::Recommended;
    if (preset_str == "strict")
        return Preset::Strict;
    return Preset::None;
}

bool ConfigLoader::parse_yaml(const std::string& content) {
    // 簡易YAMLパーサー
    // サポート形式:
    // lint:
    //   preset: recommended
    //   rules:
    //     W001: disabled
    //     L100: error

    std::istringstream stream(content);
    std::string line;

    bool in_lint_section = false;
    bool in_rules_section = false;

    while (std::getline(stream, line)) {
        // コメント行をスキップ
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        // インデントレベルを計算
        size_t indent = 0;
        for (char c : line) {
            if (c == ' ')
                indent++;
            else if (c == '\t')
                indent += 2;  // タブは2スペースとして扱う
            else
                break;
        }

        // セクション判定
        if (indent == 0) {
            in_lint_section = (trimmed == "lint:" || trimmed.substr(0, 5) == "lint:");
            in_rules_section = false;
        } else if (in_lint_section && indent >= 2 && indent < 4) {
            // preset: の処理
            if (trimmed.substr(0, 7) == "preset:") {
                std::string preset_str = trim(trimmed.substr(7));
                Preset preset = parse_preset(preset_str);
                if (preset != Preset::None) {
                    apply_preset(preset);
                }
            }
            in_rules_section = (trimmed == "rules:" || trimmed.substr(0, 6) == "rules:");
        } else if (in_rules_section && indent >= 4) {
            // ルール設定を解析: "W001: disabled"
            size_t colon_pos = trimmed.find(':');
            if (colon_pos != std::string::npos) {
                std::string rule_id = trim(trimmed.substr(0, colon_pos));
                std::string level_str = trim(trimmed.substr(colon_pos + 1));

                if (!rule_id.empty() && !level_str.empty()) {
                    RuleConfig config;
                    config.level = parse_level(level_str);
                    rules_[rule_id] = config;  // プリセットを上書き
                }
            }
        }
    }

    return true;  // 空の設定も有効
}

RuleLevel ConfigLoader::parse_level(const std::string& level_str) {
    if (level_str == "error")
        return RuleLevel::Error;
    if (level_str == "warning")
        return RuleLevel::Warning;
    if (level_str == "hint")
        return RuleLevel::Hint;
    if (level_str == "disabled" || level_str == "off")
        return RuleLevel::Disabled;
    return RuleLevel::Warning;  // デフォルト
}

std::string ConfigLoader::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

void ConfigLoader::parse_disable_comments(const std::string& source) {
    // コメントを解析して無効化行を登録
    // 対応形式:
    // // @cm-disable-next-line W001
    // // @cm-disable-next-line W001, L100
    // // @cm-disable-next-line  (全ルール)
    // // @cm-disable-line W001  (現在行)

    std::istringstream stream(source);
    std::string line;
    uint32_t line_num = 0;

    while (std::getline(stream, line)) {
        line_num++;

        // コメントを検索
        size_t comment_pos = line.find("//");
        if (comment_pos == std::string::npos) {
            continue;
        }

        std::string comment = line.substr(comment_pos + 2);
        std::string trimmed = trim(comment);

        // @cm-disable-next-line を検索
        const std::string DISABLE_NEXT = "@cm-disable-next-line";
        const std::string DISABLE_LINE = "@cm-disable-line";

        uint32_t target_line = 0;
        std::string rule_part;

        if (trimmed.substr(0, DISABLE_NEXT.length()) == DISABLE_NEXT) {
            target_line = line_num + 1;  // 次の行を無効化
            rule_part = trim(trimmed.substr(DISABLE_NEXT.length()));
        } else if (trimmed.substr(0, DISABLE_LINE.length()) == DISABLE_LINE) {
            target_line = line_num;  // 現在行を無効化
            rule_part = trim(trimmed.substr(DISABLE_LINE.length()));
        } else {
            continue;
        }

        // ルールIDを解析
        std::set<std::string> rules;
        if (rule_part.empty()) {
            // ルールID指定なし → 全ルール無効化
            rules.insert("*");
        } else {
            // カンマ区切りでルールIDを分割
            std::istringstream rule_stream(rule_part);
            std::string rule_id;
            while (std::getline(rule_stream, rule_id, ',')) {
                std::string trimmed_rule = trim(rule_id);
                if (!trimmed_rule.empty()) {
                    rules.insert(trimmed_rule);
                }
            }
        }

        // 無効化行に追加
        if (!rules.empty()) {
            auto& existing = disabled_lines_[target_line];
            existing.insert(rules.begin(), rules.end());
        }
    }
}

bool ConfigLoader::is_line_disabled(uint32_t line, const std::string& rule_id) const {
    auto it = disabled_lines_.find(line);
    if (it == disabled_lines_.end()) {
        return false;
    }

    const auto& rules = it->second;
    // "*" は全ルール無効化
    if (rules.count("*") > 0) {
        return true;
    }
    return rules.count(rule_id) > 0;
}

}  // namespace lint
}  // namespace cm
