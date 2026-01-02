#pragma once

#include "block_monitor.hpp"
#include "codegen_monitor.hpp"
#include "output_monitor.hpp"

#include <iomanip>
#include <iostream>
#include <memory>

namespace cm::codegen {

// コンパイル全体を監視する統合ガードクラス
class CompilationGuard {
   private:
    // 各モニターのインスタンス
    std::unique_ptr<CodeGenMonitor> codegen_monitor;
    std::unique_ptr<BlockMonitor> block_monitor;
    std::unique_ptr<OutputMonitor> output_monitor;

    // デバッグ出力フラグ
    bool debug_enabled = false;

    // 統計情報収集フラグ
    bool collect_stats = false;

   public:
    // コンストラクタ
    CompilationGuard()
        : codegen_monitor(std::make_unique<CodeGenMonitor>()),
          block_monitor(std::make_unique<BlockMonitor>()),
          output_monitor(std::make_unique<OutputMonitor>()) {}

    // デストラクタ
    ~CompilationGuard() {
        if (collect_stats) {
            print_statistics();
        }
    }

    // === CodeGen監視 ===
    void begin_function_generation(const std::string& func_name, size_t code_hash) {
        if (debug_enabled) {
            std::cerr << "[GUARD] 関数生成開始: " << func_name << "\n";
        }
        codegen_monitor->begin_function(func_name, code_hash);
    }

    void end_function_generation(const std::string& func_name) {
        codegen_monitor->end_function(func_name);
    }

    // === Block監視 ===
    void enter_basic_block(const std::string& func_name, const std::string& block_name) {
        if (debug_enabled) {
            std::cerr << "[GUARD] ブロック進入: " << func_name << "::" << block_name << "\n";
        }
        block_monitor->enter_block(func_name, block_name);
    }

    void exit_basic_block() { block_monitor->exit_block(); }

    void add_instruction(const std::string& instruction) {
        block_monitor->add_instruction(instruction);
    }

    // === Output監視 ===
    void begin_output_file(const std::string& filename) {
        if (debug_enabled) {
            std::cerr << "[GUARD] ファイル書き込み開始: " << filename << "\n";
        }
        output_monitor->begin_file(filename);
    }

    void end_output_file() { output_monitor->end_file(); }

    void write_output(const std::string& data) { output_monitor->write_string(data); }

    void write_output_bytes(size_t bytes) { output_monitor->write_data(bytes); }

    void check_file_size(const std::string& filename) {
        output_monitor->check_actual_file_size(filename);
    }

    // === 設定メソッド ===
    void configure(size_t max_output_size_gb = 16,
                   size_t max_generations_per_func = 1000,  // 100から1000に増加
                   size_t max_block_visits = 100000) {      // 1000から100000に増加
        // OutputMonitor設定
        output_monitor->set_max_file_size(max_output_size_gb * 1024ULL * 1024 * 1024);
        output_monitor->set_max_total_output(max_output_size_gb * 2 * 1024ULL * 1024 * 1024);

        // CodeGenMonitor設定
        codegen_monitor->set_max_generation(max_generations_per_func);

        // BlockMonitor設定
        block_monitor->set_max_block_visits(max_block_visits);
    }

    // デバッグモードの設定
    void set_debug_mode(bool enabled) { debug_enabled = enabled; }

    // 統計収集の設定
    void set_collect_statistics(bool enabled) { collect_stats = enabled; }

    // === 統計情報 ===
    void print_statistics() const {
        std::cerr << "\n========== コンパイル統計情報 ==========\n";
        std::cerr << codegen_monitor->get_statistics();
        std::cerr << block_monitor->get_statistics();
        std::cerr << output_monitor->get_statistics();
        std::cerr << "=========================================\n";
    }

    // リセット（新しいコンパイル単位用）
    void reset() {
        codegen_monitor->reset();
        block_monitor->reset();
        output_monitor->reset();
    }

    // === エラーハンドリング ===
    void handle_infinite_loop_error(const std::exception& e) {
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

    // プログレス表示
    void show_progress(const std::string& phase, size_t current, size_t total) {
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
};

// グローバルインスタンス（thread_localで各スレッドに独立）
inline thread_local std::unique_ptr<CompilationGuard> global_compilation_guard;

// ヘルパー関数
inline CompilationGuard& get_compilation_guard() {
    if (!global_compilation_guard) {
        global_compilation_guard = std::make_unique<CompilationGuard>();
    }
    return *global_compilation_guard;
}

// RAII スタイルのガード
class ScopedFunctionGuard {
   private:
    std::string func_name;
    CompilationGuard* guard;

   public:
    ScopedFunctionGuard(const std::string& name, size_t hash)
        : func_name(name), guard(&get_compilation_guard()) {
        guard->begin_function_generation(func_name, hash);
    }

    ~ScopedFunctionGuard() { guard->end_function_generation(func_name); }
};

class ScopedBlockGuard {
   private:
    CompilationGuard* guard;

   public:
    ScopedBlockGuard(const std::string& func_name, const std::string& block_name)
        : guard(&get_compilation_guard()) {
        guard->enter_basic_block(func_name, block_name);
    }

    ~ScopedBlockGuard() { guard->exit_basic_block(); }
};

class ScopedOutputGuard {
   private:
    CompilationGuard* guard;

   public:
    ScopedOutputGuard(const std::string& filename) : guard(&get_compilation_guard()) {
        guard->begin_output_file(filename);
    }

    ~ScopedOutputGuard() { guard->end_output_file(); }
};

}  // namespace cm::codegen