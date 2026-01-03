#pragma once

#include "../../nodes.hpp"  // MirFunction定義のため
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <thread>

namespace cm::mir::opt {

// タイムアウトガード：最適化処理のタイムアウト監視
class TimeoutGuard {
   private:
    std::atomic<bool> timeout_flag{false};
    std::atomic<bool> should_stop{false};
    std::thread monitor_thread;
    std::chrono::steady_clock::time_point start_time;
    std::chrono::seconds timeout_duration;

   public:
    // タイムアウト時間を指定してガードを作成
    explicit TimeoutGuard(std::chrono::seconds timeout = std::chrono::seconds(30))
        : timeout_duration(timeout), start_time(std::chrono::steady_clock::now()) {
        monitor_thread = std::thread([this]() {
            while (!should_stop.load()) {
                auto elapsed = std::chrono::steady_clock::now() - start_time;
                if (elapsed >= timeout_duration) {
                    timeout_flag.store(true);
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    }

    ~TimeoutGuard() {
        should_stop.store(true);
        if (monitor_thread.joinable()) {
            monitor_thread.join();
        }
    }

    // タイムアウトしたかチェック
    bool is_timeout() const { return timeout_flag.load(); }

    // 経過時間を取得
    std::chrono::milliseconds elapsed() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);
    }

    // タイムアウトチェック付きで処理を実行
    template <typename Func>
    bool execute_with_timeout(Func&& func, const std::string& pass_name = "") {
        if (is_timeout()) {
            if (!pass_name.empty()) {
                std::cerr << "[TIMEOUT] " << pass_name << " がタイムアウトしました\n";
            }
            return false;
        }

        bool result = func();

        if (is_timeout()) {
            if (!pass_name.empty()) {
                std::cerr << "[TIMEOUT] " << pass_name << " 実行中にタイムアウトしました\n";
            }
            return false;
        }

        return result;
    }
};

// 個別のパスのタイムアウト管理
class PassTimeoutManager {
   private:
    std::chrono::milliseconds max_pass_time;
    std::chrono::steady_clock::time_point pass_start;
    std::string current_pass;

   public:
    explicit PassTimeoutManager(std::chrono::milliseconds max_time = std::chrono::seconds(5))
        : max_pass_time(max_time) {}

    // パス開始時刻を記録
    void start_pass(const std::string& pass_name) {
        current_pass = pass_name;
        pass_start = std::chrono::steady_clock::now();
    }

    // パスのタイムアウトチェック
    bool check_pass_timeout() const {
        auto elapsed = std::chrono::steady_clock::now() - pass_start;
        if (elapsed > max_pass_time) {
            std::cerr << "[TIMEOUT] パス '" << current_pass << "' が "
                      << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
                      << "ms で時間切れ（制限: " << max_pass_time.count() << "ms）\n";
            return true;
        }
        return false;
    }

    // パス終了時の処理
    void end_pass(bool debug = false) {
        if (debug) {
            auto elapsed = std::chrono::steady_clock::now() - pass_start;
            std::cout << "[TIMING] " << current_pass << ": "
                      << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
                      << "ms\n";
        }
    }
};

// 複雑度制限
class ComplexityLimiter {
   private:
    size_t max_blocks;
    size_t max_statements;
    size_t max_locals;

   public:
    ComplexityLimiter(size_t blocks = 1000, size_t stmts = 10000, size_t locals = 500)
        : max_blocks(blocks), max_statements(stmts), max_locals(locals) {}

    // 関数の複雑度チェック
    bool is_too_complex(const MirFunction& func) const {
        if (func.basic_blocks.size() > max_blocks) {
            std::cerr << "[COMPLEXITY] 関数 '" << func.name
                      << "' のブロック数が多すぎます: " << func.basic_blocks.size()
                      << " (制限: " << max_blocks << ")\n";
            return true;
        }

        size_t total_statements = 0;
        for (const auto& block : func.basic_blocks) {
            if (block) {
                total_statements += block->statements.size();
            }
        }

        if (total_statements > max_statements) {
            std::cerr << "[COMPLEXITY] 関数 '" << func.name
                      << "' のステートメント数が多すぎます: " << total_statements
                      << " (制限: " << max_statements << ")\n";
            return true;
        }

        if (func.locals.size() > max_locals) {
            std::cerr << "[COMPLEXITY] 関数 '" << func.name
                      << "' のローカル変数が多すぎます: " << func.locals.size()
                      << " (制限: " << max_locals << ")\n";
            return true;
        }

        return false;
    }
};

}  // namespace cm::mir::opt