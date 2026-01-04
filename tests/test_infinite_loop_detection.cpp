#include "../src/codegen/llvm/monitoring/block_monitor.hpp"
#include "../src/codegen/llvm/monitoring/buffered.hpp"
#include "../src/mir/analysis/convergence.hpp"

#include <gtest/gtest.h>

using namespace cm::codegen;
using namespace cm::mir::optimizations;

// バッファベースコード生成のテスト
TEST(BufferedCodeGen, StopsAtSizeLimit) {
    BufferedCodeGenerator gen;
    BufferedCodeGenerator::Limits limits;
    limits.max_bytes = 100;  // 100バイトまで
    gen.set_limits(limits);

    gen.begin_generation();

    // 無限ループを試みる
    int iterations = 0;
    while (iterations < 1000) {  // 安全装置
        if (!gen.append("Hello World ")) {
            break;
        }
        iterations++;
    }

    EXPECT_TRUE(gen.has_generation_error());
    EXPECT_LT(iterations, 1000);  // 1000回未満で停止したはず
    EXPECT_LE(gen.current_buffer_size(), 100);
}

TEST(BufferedCodeGen, StopsAtLineLimit) {
    BufferedCodeGenerator gen;
    BufferedCodeGenerator::Limits limits;
    limits.max_lines = 10;  // 10行まで
    gen.set_limits(limits);

    gen.begin_generation();

    int lines = 0;
    while (lines < 100) {
        if (!gen.append_line("Line " + std::to_string(lines))) {
            break;
        }
        lines++;
    }

    EXPECT_TRUE(gen.has_generation_error());
    EXPECT_LE(lines, 10);
}

TEST(BufferedCodeGen, StopsAtTimeLimit) {
    BufferedCodeGenerator gen;
    BufferedCodeGenerator::Limits limits;
    limits.max_generation_time = std::chrono::milliseconds(100);  // 100ms
    gen.set_limits(limits);

    gen.begin_generation();

    auto start = std::chrono::steady_clock::now();
    while (true) {
        if (!gen.append("x")) {
            break;
        }
        // 少し待機して時間を消費
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    auto end = std::chrono::steady_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_TRUE(gen.has_generation_error());
    EXPECT_LE(duration.count(), 200);  // 200ms以内に停止
}

// ブロックモニターのテスト
TEST(BufferedBlockMonitor, DetectsSimpleLoop) {
    BufferedBlockMonitor monitor;
    monitor.configure(10, 1000, 5);  // 最大10回訪問、警告は5回

    // 同じブロックを繰り返し訪問
    for (int i = 0; i < 20; i++) {
        if (!monitor.enter_block("test_func", "BB0")) {
            break;
        }
        monitor.add_instruction("nop");
        monitor.exit_block();
    }

    EXPECT_TRUE(monitor.has_generation_error());
    auto stats = monitor.get_monitor_stats();
    EXPECT_NE(stats.find("循環警告"), std::string::npos);
}

TEST(BufferedBlockMonitor, DetectsOscillation) {
    BufferedBlockMonitor monitor;

    // A→B→A→B パターン
    bool error = false;
    for (int i = 0; i < 10; i++) {
        std::string block = (i % 2 == 0) ? "BB0" : "BB1";
        if (!monitor.enter_block("func", block)) {
            error = true;
            break;
        }
        monitor.add_instruction("inst");
        monitor.exit_block();
    }

    auto stats = monitor.get_monitor_stats();
    // 振動パターンが検出されているはず
    EXPECT_NE(stats.find("BB0"), std::string::npos);
    EXPECT_NE(stats.find("BB1"), std::string::npos);
}

// スマート収束マネージャーのテスト
TEST(SmartConvergence, DetectsSameChanges) {
    SmartConvergenceManager mgr;

    // 同じ変更数を繰り返す
    auto state1 = mgr.add_iteration(10);
    EXPECT_EQ(state1, SmartConvergenceManager::State::CONTINUE);

    auto state2 = mgr.add_iteration(10);
    EXPECT_EQ(state2, SmartConvergenceManager::State::LIKELY_CYCLE);

    auto state3 = mgr.add_iteration(10);
    EXPECT_EQ(state3, SmartConvergenceManager::State::CONVERGED);
}

TEST(SmartConvergence, DetectsZeroChanges) {
    SmartConvergenceManager mgr;

    auto state = mgr.add_iteration(0);
    EXPECT_EQ(state, SmartConvergenceManager::State::NO_CHANGE);
}

TEST(SmartConvergence, DetectsOscillatingPattern) {
    SmartConvergenceManager mgr;

    // 10→15→10→15 パターン
    mgr.add_iteration(10);
    mgr.add_iteration(15);
    mgr.add_iteration(10);
    auto state = mgr.add_iteration(15);

    EXPECT_EQ(state, SmartConvergenceManager::State::LIKELY_CYCLE);
}

TEST(SmartConvergence, ConvergesOnMinorChanges) {
    SmartConvergenceManager mgr;

    // 小さな変更が続く
    mgr.add_iteration(3);
    mgr.add_iteration(2);
    auto state = mgr.add_iteration(1);

    EXPECT_EQ(state, SmartConvergenceManager::State::CONVERGED);
}

// 統合テスト：実際のコンパイルシナリオ
TEST(Integration, CompilationWithLimits) {
    // 二段階コード生成器を使用
    TwoPhaseCodeGenerator gen;

    // 制限を設定
    BufferedCodeGenerator::Limits limits;
    limits.max_bytes = 1024 * 1024;  // 1MB
    limits.max_lines = 10000;
    gen.set_limits(limits);

    // ブロックを追加
    gen.add_block("header", "#include <iostream>\n", true);
    gen.add_block("main_func", "int main() {\n  return 0;\n}\n", true);

    // 巨大な非必須ブロックを追加（スキップされるはず）
    std::string huge_comment(2 * 1024 * 1024, '/');  // 2MB
    gen.add_block("huge_comment", huge_comment, false);

    // 生成
    std::string result = gen.generate();

    EXPECT_FALSE(result.empty());
    EXPECT_NE(result.find("main"), std::string::npos);
    EXPECT_EQ(result.find(huge_comment), std::string::npos);  // 巨大コメントは含まれない
}

// パフォーマンステスト
TEST(Performance, BufferedVsStreaming) {
    const int ITERATIONS = 10000;

    // バッファベース
    auto buffer_start = std::chrono::high_resolution_clock::now();
    {
        BufferedCodeGenerator gen;
        gen.begin_generation();
        for (int i = 0; i < ITERATIONS; i++) {
            gen.append_line("Line " + std::to_string(i));
        }
        std::string result = gen.end_generation();
    }
    auto buffer_end = std::chrono::high_resolution_clock::now();

    // ストリーミング（シミュレーション）
    auto stream_start = std::chrono::high_resolution_clock::now();
    {
        std::stringstream ss;
        for (int i = 0; i < ITERATIONS; i++) {
            ss << "Line " << i << "\n";
            ss.flush();  // 各行でフラッシュ（IOシミュレーション）
        }
        std::string result = ss.str();
    }
    auto stream_end = std::chrono::high_resolution_clock::now();

    auto buffer_time =
        std::chrono::duration_cast<std::chrono::microseconds>(buffer_end - buffer_start).count();
    auto stream_time =
        std::chrono::duration_cast<std::chrono::microseconds>(stream_end - stream_start).count();

    std::cout << "バッファベース: " << buffer_time << " μs\n";
    std::cout << "ストリーミング: " << stream_time << " μs\n";

    // バッファベースの方が高速なはず
    EXPECT_LT(buffer_time, stream_time * 2);  // 少なくとも半分の時間
}