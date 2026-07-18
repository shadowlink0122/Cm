#pragma once

#include "block_monitor.hpp"
#include "codegen_monitor.hpp"
#include "output_monitor.hpp"

#include <memory>
#include <string>

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
    CompilationGuard();
    ~CompilationGuard();

    // === CodeGen監視 ===
    void begin_function_generation(const std::string& func_name, size_t code_hash);
    void end_function_generation(const std::string& func_name);

    // === Block監視 ===
    void enter_basic_block(const std::string& func_name, const std::string& block_name);
    void exit_basic_block();
    void add_instruction(const std::string& instruction);

    // === Output監視 ===
    void begin_output_file(const std::string& filename);
    void end_output_file();
    void write_output(const std::string& data);
    void write_output_bytes(size_t bytes);
    void check_file_size(const std::string& filename);

    // === 設定メソッド ===
    void configure(size_t max_output_size_gb = 16,
                   size_t max_generations_per_func = 1000,  // 100から1000に増加
                   size_t max_block_visits = 100000);       // 1000から100000に増加

    // デバッグモードの設定
    void set_debug_mode(bool enabled) { debug_enabled = enabled; }

    // 統計収集の設定
    void set_collect_statistics(bool enabled) { collect_stats = enabled; }

    // === 統計情報 ===
    void print_statistics() const;

    // リセット（新しいコンパイル単位用）
    void reset();

    // === エラーハンドリング ===
    void handle_infinite_loop_error(const std::exception& e);

    // === ユーティリティ ===
    // プログレス表示
    void show_progress(const std::string& phase, size_t current, size_t total);
};

// スレッドごとの共有インスタンスを取得（実装は compilation_guard.cpp）
CompilationGuard& get_compilation_guard();

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
