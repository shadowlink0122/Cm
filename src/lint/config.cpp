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

bool ConfigLoader::parse_yaml(const std::string& content) {
    // 簡易YAMLパーサー
    // サポート形式:
    // lint:
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
                    rules_[rule_id] = config;
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

}  // namespace lint
}  // namespace cm
