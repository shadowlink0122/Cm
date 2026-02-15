#pragma once

// ============================================================
// 診断カタログ - 全診断を一元管理
// ============================================================

#include "common/span.hpp"
#include "levels.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace cm {
namespace diagnostics {

/// 修正提案
struct FixSuggestion {
    Span span;
    std::string replacement;
    std::string description;
};

/// 診断定義
struct DiagnosticDefinition {
    std::string id;    // "E001", "W001", "L001"
    std::string name;  // "undefined-variable"
    DiagnosticLevel default_level;
    std::string message_template;  // "{0} is not defined"
    DetectionStage stage;
    bool is_fixable = false;

    DiagnosticDefinition() = default;
    DiagnosticDefinition(std::string id_, std::string name_, DiagnosticLevel level_,
                         std::string msg_, DetectionStage stage_, bool fixable_ = false)
        : id(std::move(id_)),
          name(std::move(name_)),
          default_level(level_),
          message_template(std::move(msg_)),
          stage(stage_),
          is_fixable(fixable_) {}
};

/// 診断インスタンス
struct Diagnostic {
    std::string id;
    std::string name;
    DiagnosticLevel level;
    Span span;
    std::string message;
    std::vector<FixSuggestion> fixes;

    Diagnostic() = default;
    Diagnostic(std::string id_, std::string name_, DiagnosticLevel level_, Span span_,
               std::string msg_)
        : id(std::move(id_)),
          name(std::move(name_)),
          level(level_),
          span(span_),
          message(std::move(msg_)) {}
};

/// 診断カタログ - 全診断を一元管理
class DiagnosticCatalog {
   public:
    static DiagnosticCatalog& instance() {
        static DiagnosticCatalog catalog;
        return catalog;
    }

    /// 診断定義を登録
    void register_definition(DiagnosticDefinition def) { definitions_[def.id] = std::move(def); }

    /// IDで定義を取得
    const DiagnosticDefinition* get(const std::string& id) const {
        auto it = definitions_.find(id);
        return it != definitions_.end() ? &it->second : nullptr;
    }

    /// 全定義を取得
    const std::unordered_map<std::string, DiagnosticDefinition>& all() const {
        return definitions_;
    }

   private:
    DiagnosticCatalog() { register_defaults(); }

    void register_defaults();

    std::unordered_map<std::string, DiagnosticDefinition> definitions_;
};

/// メッセージテンプレートをフォーマット
std::string format_message(const std::string& tmpl, const std::vector<std::string>& args);

}  // namespace diagnostics
}  // namespace cm
