// ============================================================
// バッファベースコード生成の実装
// ============================================================
// buffered.hpp で宣言された非テンプレートメンバ関数の実装。

#include "internal/codegen/buffered.hpp"

#include "internal/base/i18n.hpp"

#include <algorithm>
#include <iostream>
#include <string>

namespace cm::codegen {

// ============================================================
// BufferedCodeGenerator
// ============================================================

void BufferedCodeGenerator::begin_generation() {
    buffer.str("");
    buffer.clear();
    lines.clear();
    stats = GenerationStats{};
    has_error = false;
    error_message.clear();
    start_time = std::chrono::high_resolution_clock::now();
}

bool BufferedCodeGenerator::append_line(const std::string& line) {
    // サイズチェック
    if (!check_limits()) {
        return false;
    }

    lines.push_back(line);
    buffer << line << "\n";

    stats.total_lines++;
    stats.total_bytes += line.size() + 1;  // +1 for newline

    return true;
}

bool BufferedCodeGenerator::append(const std::string& content) {
    // サイズチェック
    if (!check_limits()) {
        return false;
    }

    buffer << content;
    stats.total_bytes += content.size();

    // 行数をカウント
    size_t newlines = std::count(content.begin(), content.end(), '\n');
    stats.total_lines += newlines;

    return true;
}

bool BufferedCodeGenerator::check_limits() {
    // 時間制限チェック
    auto elapsed = std::chrono::high_resolution_clock::now() - start_time;
    if (elapsed > limits.max_generation_time) {
        set_error(i18n::msg(i18n::MsgId::CodegenCodeGenerationTimeExceededThe));
        return false;
    }

    // サイズ制限チェック
    if (stats.total_bytes > limits.max_bytes) {
        set_error(i18n::msg(i18n::MsgId::CodegenGeneratedCodeSizeExceededThe));
        stats.exceeded_limit = true;
        return false;
    }

    if (stats.total_lines > limits.max_lines) {
        set_error(i18n::msg(i18n::MsgId::CodegenGeneratedCodeLineCountExceeded));
        stats.exceeded_limit = true;
        return false;
    }

    // 警告閾値チェック
    if (stats.total_bytes > limits.warning_threshold_bytes) {
        if (!stats.exceeded_limit) {  // 一度だけ警告
            std::cerr << i18n::msgf(i18n::MsgId::CodegenCodegenWarningGeneratedCodeExceeds,
                                    (stats.total_bytes / (1024 * 1024)));
        }
    }

    return true;
}

std::string BufferedCodeGenerator::end_generation() {
    auto end_time = std::chrono::high_resolution_clock::now();
    stats.generation_time =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    stats.max_buffer_size = buffer.str().size();

    if (has_error) {
        return "";  // エラー時は空文字列
    }

    return buffer.str();
}

std::string BufferedCodeGenerator::get_generated_code() {
    if (has_error) {
        return "";
    }
    return buffer.str();
}

void BufferedCodeGenerator::set_error(const std::string& msg) {
    has_error = true;
    error_message = msg;
    std::cerr << i18n::msgf(i18n::MsgId::CodegenCodegenError, msg);
}

// ============================================================
// TwoPhaseCodeGenerator
// ============================================================

bool TwoPhaseCodeGenerator::add_block(const std::string& name, const std::string& content,
                                      bool is_critical) {
    size_t size = content.size();

    // 推定サイズチェック
    if (total_estimated_size + size > limits.max_bytes) {
        if (is_critical) {
            set_error(i18n::msgf(i18n::MsgId::CodegenRequiredBlockCannotBeAdded, name));
            return false;
        }
        // 非必須ブロックはスキップ
        return true;
    }

    blocks.push_back({name, content, size, is_critical});
    total_estimated_size += size;
    return true;
}

std::string TwoPhaseCodeGenerator::generate() {
    begin_generation();

    // ブロックを順番に追加
    for (const auto& block : blocks) {
        if (!append("// === " + block.name + " ===\n")) {
            break;
        }
        if (!append(block.content)) {
            if (block.is_critical) {
                set_error(
                    i18n::msgf(i18n::MsgId::CodegenRequiredBlockGenerationFailed, block.name));
                return "";
            }
            // 非必須ブロックはスキップして続行
            continue;
        }
        if (!append("\n")) {
            break;
        }
    }

    return end_generation();
}

// ============================================================
// ScopedCodeSection
// ============================================================

ScopedCodeSection::ScopedCodeSection(BufferedCodeGenerator& g, const std::string& name)
    : gen(g), section_name(name) {
    start_size = gen.current_buffer_size();
    gen.append_line("// BEGIN: " + section_name);
}

ScopedCodeSection::~ScopedCodeSection() {
    if (!committed) {
        // エラー時はロールバック（概念的に）
        std::cerr << i18n::msgf(i18n::MsgId::CodegenCodegenSectionWasNotCommitted, section_name);
    }
}

void ScopedCodeSection::commit() {
    gen.append_line("// END: " + section_name);
    committed = true;
}

}  // namespace cm::codegen
