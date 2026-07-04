// ============================================================
// CompilationGuard 実装
// ============================================================

#include "compilation_guard.hpp"

#include <iomanip>
#include <iostream>

namespace cm::codegen {

CompilationGuard::CompilationGuard()
    : codegen_monitor(std::make_unique<CodeGenMonitor>()),
      block_monitor(std::make_unique<BlockMonitor>()),
      output_monitor(std::make_unique<OutputMonitor>()) {}

CompilationGuard::~CompilationGuard() {
    if (collect_stats) {
        print_statistics();
    }
}

// === CodeGen監視 ===

void CompilationGuard::begin_function_generation(const std::string& func_name, size_t code_hash) {
    if (debug_enabled) {
        std::cerr << "[GUARD] 関数生成開始: " << func_name << "\n";
    }
    codegen_monitor->begin_function(func_name, code_hash);
}

void CompilationGuard::end_function_generation(const std::string& func_name) {
    codegen_monitor->end_function(func_name);
}

// === Block監視 ===

void CompilationGuard::enter_basic_block(const std::string& func_name,
                                         const std::string& block_name) {
    if (debug_enabled) {
        std::cerr << "[GUARD] ブロック進入: " << func_name << "::" << block_name << "\n";
    }
    block_monitor->enter_block(func_name, block_name);
}

void CompilationGuard::exit_basic_block() {
    block_monitor->exit_block();
}

void CompilationGuard::add_instruction(const std::string& instruction) {
    block_monitor->add_instruction(instruction);
}

// === Output監視 ===

void CompilationGuard::begin_output_file(const std::string& filename) {
    if (debug_enabled) {
        std::cerr << "[GUARD] ファイル書き込み開始: " << filename << "\n";
    }
    output_monitor->begin_file(filename);
}

void CompilationGuard::end_output_file() {
    output_monitor->end_file();
}

void CompilationGuard::write_output(const std::string& data) {
    output_monitor->write_string(data);
}

void CompilationGuard::write_output_bytes(size_t bytes) {
    output_monitor->write_data(bytes);
}

void CompilationGuard::check_file_size(const std::string& filename) {
    output_monitor->check_actual_file_size(filename);
}

// === 設定メソッド ===

void CompilationGuard::configure(size_t max_output_size_gb, size_t max_generations_per_func,
                                 size_t max_block_visits) {
    // OutputMonitor設定
    output_monitor->set_max_file_size(max_output_size_gb * 1024ULL * 1024 * 1024);
    output_monitor->set_max_total_output(max_output_size_gb * 2 * 1024ULL * 1024 * 1024);

    // CodeGenMonitor設定
    codegen_monitor->set_max_generation(max_generations_per_func);

    // BlockMonitor設定
    block_monitor->set_max_block_visits(max_block_visits);
}

// === 統計情報 ===

void CompilationGuard::print_statistics() const {
    std::cerr << "\n========== コンパイル統計情報 ==========\n";
    std::cerr << codegen_monitor->get_statistics();
    std::cerr << block_monitor->get_statistics();
    std::cerr << output_monitor->get_statistics();
    std::cerr << "=========================================\n";
}

void CompilationGuard::reset() {
    codegen_monitor->reset();
    block_monitor->reset();
    output_monitor->reset();
}

// === エラーハンドリング ===

void CompilationGuard::handle_infinite_loop_error(const std::exception& e) {
    std::cerr << "\n[エラー] 無限ループが検出されました:\n";
    std::cerr << "  " << e.what() << "\n";

    // 現在の統計情報を出力
    print_statistics();

    // デバッグ情報の提案
    std::cerr << "\nデバッグのヒント:\n";
    std::cerr << "  1. -O0 オプションで最適化を無効にしてみてください\n";
    std::cerr << "  2. --debug オプションで詳細ログを確認してください\n";
    std::cerr << "  3. 特定の最適化パスが原因の可能性があります\n";
}

// === ユーティリティ ===

void CompilationGuard::show_progress(const std::string& phase, size_t current, size_t total) {
    if (!debug_enabled)
        return;

    int percentage = (current * 100) / total;
    int bar_width = 50;
    int filled = (bar_width * current) / total;

    std::cerr << "\r[" << phase << "] [";
    for (int i = 0; i < bar_width; ++i) {
        if (i < filled)
            std::cerr << "=";
        else if (i == filled)
            std::cerr << ">";
        else
            std::cerr << " ";
    }
    std::cerr << "] " << std::setw(3) << percentage << "%";

    if (current >= total) {
        std::cerr << "\n";
    }
    std::cerr.flush();
}

// スレッドごとの共有インスタンス
// （以前はヘッダーの inline thread_local グローバル変数だったものを
//   翻訳単位内の関数ローカルstaticへ移動し、グローバル可変状態を隠蔽）
CompilationGuard& get_compilation_guard() {
    thread_local std::unique_ptr<CompilationGuard> guard;
    if (!guard) {
        guard = std::make_unique<CompilationGuard>();
    }
    return *guard;
}

}  // namespace cm::codegen
