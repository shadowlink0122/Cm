# 無限ループ検出システム設計書

## 概要
コンパイラの最適化やコード生成において発生する無限ループを検出し、ディスク容量の枯渇やプロセスの無限実行を防ぐシステム。

## 設計原則
1. **本番利用を妨げない** - 大規模プロジェクトの正常なビルドは制限しない
2. **早期検出** - 問題を早期に検出してリソース浪費を防ぐ
3. **詳細な診断** - 問題箇所を特定できる情報を提供
4. **設定可能** - プロジェクトに応じて閾値を調整可能

## 1. CodeGen層での重複検出

### 1.1 関数レベルの重複検出

```cpp
// src/codegen/llvm/codegen_monitor.hpp
#pragma once

#include <string>
#include <unordered_map>
#include <chrono>
#include <stdexcept>

namespace cm::codegen {

class CodeGenMonitor {
private:
    // 関数ごとの生成回数
    std::unordered_map<std::string, size_t> function_generation_counts;

    // 設定可能な閾値（デフォルト値）
    size_t max_function_generations = 10;  // 同一関数の最大生成回数

    // 統計情報
    size_t total_functions_generated = 0;
    std::chrono::steady_clock::time_point start_time;

public:
    CodeGenMonitor() : start_time(std::chrono::steady_clock::now()) {}

    // 関数生成前にチェック
    void check_function_generation(const std::string& func_name) {
        total_functions_generated++;
        function_generation_counts[func_name]++;

        // 重複チェック
        if (function_generation_counts[func_name] > max_function_generations) {
            auto error_msg = format_error(
                "関数 '{}' のコード生成が{}回を超えました（無限ループの可能性）",
                func_name, max_function_generations
            );
            throw CodeGenInfiniteLoopError(error_msg);
        }

        // 総数チェック（異常な関数数）
        if (total_functions_generated > 100000) {
            throw CodeGenInfiniteLoopError(
                "生成された関数の総数が異常です（100000超）"
            );
        }
    }

    // 設定変更
    void set_max_function_generations(size_t max) {
        max_function_generations = max;
    }

    // 統計取得
    std::string get_statistics() const {
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

        return format_statistics(
            "生成関数数: {}, 経過時間: {}秒, 最頻出: {}({}回)",
            total_functions_generated, seconds,
            get_most_frequent_function()
        );
    }

    // リセット（新しいコンパイル単位）
    void reset() {
        function_generation_counts.clear();
        total_functions_generated = 0;
        start_time = std::chrono::steady_clock::now();
    }

private:
    std::pair<std::string, size_t> get_most_frequent_function() const {
        std::string name;
        size_t max_count = 0;
        for (const auto& [func, count] : function_generation_counts) {
            if (count > max_count) {
                name = func;
                max_count = count;
            }
        }
        return {name, max_count};
    }
};

} // namespace cm::codegen
```

### 1.2 パターン検出

```cpp
// 周期的パターン検出
class PatternDetector {
private:
    std::vector<std::string> recent_functions;
    static constexpr size_t HISTORY_SIZE = 100;

public:
    void record_function(const std::string& name) {
        recent_functions.push_back(name);
        if (recent_functions.size() > HISTORY_SIZE) {
            recent_functions.erase(recent_functions.begin());
        }

        // 周期的パターンチェック
        detect_periodic_pattern();
    }

private:
    void detect_periodic_pattern() {
        if (recent_functions.size() < 20) return;

        // 周期2-10のパターンを検出
        for (size_t period = 2; period <= 10 && period * 3 <= recent_functions.size(); ++period) {
            if (is_periodic(period)) {
                throw CodeGenInfiniteLoopError(
                    format("周期{}の繰り返しパターンを検出しました", period)
                );
            }
        }
    }

    bool is_periodic(size_t period) {
        size_t start = recent_functions.size() - period * 3;

        for (size_t i = 0; i < period; ++i) {
            if (recent_functions[start + i] != recent_functions[start + i + period] ||
                recent_functions[start + i] != recent_functions[start + i + period * 2]) {
                return false;
            }
        }
        return true;
    }
};
```

## 2. ブロックレベルの監視

### 2.1 基本ブロック生成監視

```cpp
// src/codegen/llvm/block_monitor.hpp
class BlockMonitor {
private:
    struct BlockInfo {
        size_t generation_count = 0;
        size_t statement_count = 0;
        std::chrono::steady_clock::time_point last_generated;
    };

    std::unordered_map<std::string, BlockInfo> block_infos;

    // 閾値設定
    size_t max_blocks_per_function = 10000;  // 実用的な上限
    size_t max_block_regenerations = 5;      // 同一ブロックの再生成上限

public:
    void check_block_generation(const std::string& func_name,
                               size_t block_id,
                               size_t statement_count) {
        std::string block_key = func_name + "_bb" + std::to_string(block_id);
        auto& info = block_infos[block_key];

        info.generation_count++;
        info.statement_count = statement_count;
        info.last_generated = std::chrono::steady_clock::now();

        // 再生成チェック
        if (info.generation_count > max_block_regenerations) {
            std::cerr << "[CODEGEN] エラー: ブロック '" << block_key
                      << "' が" << info.generation_count
                      << "回生成されました（上限: " << max_block_regenerations << "）\n";
            throw CodeGenInfiniteLoopError("ブロックの過剰な再生成");
        }

        // 関数内ブロック数チェック
        size_t func_block_count = 0;
        std::string func_prefix = func_name + "_bb";
        for (const auto& [key, _] : block_infos) {
            if (key.find(func_prefix) == 0) {
                func_block_count++;
            }
        }

        if (func_block_count > max_blocks_per_function) {
            throw CodeGenInfiniteLoopError(
                format("関数 '{}' のブロック数が上限({})を超えました",
                       func_name, max_blocks_per_function)
            );
        }
    }

    // 警告レベルのチェック
    void check_warnings(const std::string& block_key) {
        const auto& info = block_infos[block_key];

        // 大きなブロックの警告
        if (info.statement_count > 1000) {
            std::cerr << "[CODEGEN] 警告: ブロック '" << block_key
                      << "' に" << info.statement_count
                      << "個のステートメントがあります\n";
        }

        // 頻繁な再生成の警告
        if (info.generation_count > 2) {
            std::cerr << "[CODEGEN] 警告: ブロック '" << block_key
                      << "' が" << info.generation_count
                      << "回生成されています\n";
        }
    }
};
```

## 3. 出力ファイルサイズ監視

### 3.1 ファイルサイズ制限

```cpp
// src/codegen/output_monitor.hpp
#pragma once

#include <fstream>
#include <filesystem>
#include <atomic>

namespace cm::codegen {

class OutputMonitor {
private:
    // サイズ制限（デフォルト値）
    static constexpr size_t DEFAULT_MAX_OUTPUT_SIZE = 16ULL * 1024 * 1024 * 1024;  // 16GB
    static constexpr size_t WARNING_THRESHOLD = 1ULL * 1024 * 1024 * 1024;        // 1GB

    size_t max_output_size = DEFAULT_MAX_OUTPUT_SIZE;
    std::atomic<size_t> total_bytes_written{0};

    // ファイル別の統計
    struct FileStats {
        std::string path;
        size_t bytes_written = 0;
        size_t write_count = 0;
        std::chrono::steady_clock::time_point start_time;
    };

    std::unordered_map<std::string, FileStats> file_stats;

public:
    // ファイル書き込み前チェック
    void check_before_write(const std::string& file_path, size_t bytes_to_write) {
        auto& stats = file_stats[file_path];
        stats.path = file_path;
        stats.write_count++;

        // 累積サイズチェック
        size_t new_total = total_bytes_written.fetch_add(bytes_to_write) + bytes_to_write;
        stats.bytes_written += bytes_to_write;

        // 警告レベル
        if (stats.bytes_written > WARNING_THRESHOLD &&
            stats.bytes_written - bytes_to_write <= WARNING_THRESHOLD) {
            std::cerr << "[OUTPUT] 警告: ファイル '" << file_path
                      << "' のサイズが" << format_size(stats.bytes_written)
                      << "を超えました\n";
        }

        // エラーレベル
        if (new_total > max_output_size) {
            std::string error_msg = format(
                "出力ファイルの総サイズが上限({})を超えました\n"
                "現在のサイズ: {}\n"
                "ファイル: {}\n",
                format_size(max_output_size),
                format_size(new_total),
                file_path
            );

            // ディスク容量も確認
            check_disk_space(file_path);

            throw OutputSizeExceededError(error_msg);
        }

        // 単一ファイルの異常チェック
        if (stats.bytes_written > max_output_size / 4) {  // 単一ファイルが上限の1/4を超える
            std::cerr << "[OUTPUT] エラー: ファイル '" << file_path
                      << "' が異常に大きくなっています（"
                      << format_size(stats.bytes_written) << "）\n";
            throw OutputSizeExceededError("単一ファイルのサイズが異常です");
        }
    }

    // ディスク容量チェック
    void check_disk_space(const std::string& path) {
        try {
            auto space_info = std::filesystem::space(std::filesystem::path(path).parent_path());
            size_t available_mb = space_info.available / (1024 * 1024);

            if (available_mb < 100) {  // 100MB未満
                throw std::runtime_error(
                    format("ディスク空き容量が不足しています（残り{}MB）", available_mb)
                );
            }

            if (available_mb < 1024) {  // 1GB未満で警告
                std::cerr << "[OUTPUT] 警告: ディスク空き容量が少なくなっています（残り"
                         << available_mb << "MB）\n";
            }
        } catch (const std::filesystem::filesystem_error& e) {
            // ファイルシステムエラーは警告のみ
            std::cerr << "[OUTPUT] 警告: ディスク容量を確認できません: " << e.what() << "\n";
        }
    }

    // 書き込み速度の異常検出
    void check_write_rate() {
        for (const auto& [path, stats] : file_stats) {
            if (stats.write_count > 10000) {  // 同一ファイルへの過剰な書き込み
                auto elapsed = std::chrono::steady_clock::now() - stats.start_time;
                auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

                if (seconds > 0) {
                    size_t writes_per_second = stats.write_count / seconds;
                    if (writes_per_second > 1000) {
                        std::cerr << "[OUTPUT] 警告: ファイル '" << path
                                 << "' への書き込みが異常に頻繁です（"
                                 << writes_per_second << "回/秒）\n";
                    }
                }
            }
        }
    }

    // 設定変更
    void set_max_output_size(size_t size_gb) {
        max_output_size = size_gb * 1024ULL * 1024 * 1024;
    }

    // 統計情報
    std::string get_statistics() const {
        std::stringstream ss;
        ss << "=== 出力統計 ===\n";
        ss << "総出力サイズ: " << format_size(total_bytes_written) << "\n";
        ss << "ファイル数: " << file_stats.size() << "\n";

        // 最大のファイルを表示
        size_t max_size = 0;
        std::string max_file;
        for (const auto& [path, stats] : file_stats) {
            if (stats.bytes_written > max_size) {
                max_size = stats.bytes_written;
                max_file = path;
            }
        }

        if (!max_file.empty()) {
            ss << "最大ファイル: " << max_file
               << " (" << format_size(max_size) << ")\n";
        }

        return ss.str();
    }

private:
    // サイズを人間が読みやすい形式に
    static std::string format_size(size_t bytes) {
        const char* units[] = {"B", "KB", "MB", "GB", "TB"};
        int unit_index = 0;
        double size = static_cast<double>(bytes);

        while (size >= 1024.0 && unit_index < 4) {
            size /= 1024.0;
            unit_index++;
        }

        std::stringstream ss;
        ss << std::fixed << std::setprecision(2) << size << " " << units[unit_index];
        return ss.str();
    }
};

} // namespace cm::codegen
```

## 4. 統合インターフェース

### 4.1 統合監視クラス

```cpp
// src/codegen/compilation_guard.hpp
class CompilationGuard {
private:
    CodeGenMonitor codegen_monitor;
    BlockMonitor block_monitor;
    OutputMonitor output_monitor;
    PatternDetector pattern_detector;

    bool enabled = true;
    bool verbose = false;

public:
    // 関数生成前
    void before_generate_function(const std::string& func_name) {
        if (!enabled) return;

        codegen_monitor.check_function_generation(func_name);
        pattern_detector.record_function(func_name);

        if (verbose) {
            std::cout << "[GUARD] 関数 '" << func_name << "' の生成開始\n";
        }
    }

    // ブロック生成前
    void before_generate_block(const std::string& func_name,
                               size_t block_id,
                               size_t statement_count) {
        if (!enabled) return;

        block_monitor.check_block_generation(func_name, block_id, statement_count);
    }

    // ファイル書き込み前
    void before_write_output(const std::string& path, size_t bytes) {
        if (!enabled) return;

        output_monitor.check_before_write(path, bytes);
        output_monitor.check_write_rate();
    }

    // 設定
    void configure(const CompilationGuardConfig& config) {
        enabled = config.enabled;
        verbose = config.verbose;

        if (config.max_output_size_gb > 0) {
            output_monitor.set_max_output_size(config.max_output_size_gb);
        }

        if (config.max_function_generations > 0) {
            codegen_monitor.set_max_function_generations(config.max_function_generations);
        }
    }

    // 終了時のレポート
    void print_final_report() {
        if (!enabled || !verbose) return;

        std::cout << "\n=== コンパイル監視レポート ===\n";
        std::cout << codegen_monitor.get_statistics() << "\n";
        std::cout << output_monitor.get_statistics() << "\n";
    }
};

// グローバルインスタンス
inline thread_local CompilationGuard compilation_guard;
```

## 5. コマンドラインオプション

```bash
# 基本使用
cm compile main.cm

# 監視を無効化（信頼できるコードの場合）
cm compile --no-guard main.cm

# カスタム制限
cm compile --max-output-size=32GB main.cm
cm compile --max-function-generations=20 main.cm

# 詳細モード
cm compile --guard-verbose main.cm

# 環境変数での設定
export CM_MAX_OUTPUT_SIZE=8GB
export CM_GUARD_VERBOSE=1
cm compile main.cm
```

## 6. エラーメッセージ例

### 無限ループ検出時
```
[CODEGEN] エラー: 関数 'optimize_loop' のコード生成が10回を超えました（無限ループの可能性）

詳細:
- 関数名: optimize_loop
- 生成回数: 11
- 最後の生成: 2024-12-31 21:45:32
- 提案: 最適化レベルを下げるか、--max-function-generations オプションで上限を調整してください

スタックトレース:
  at MIRToLLVM::convertFunction (mir_to_llvm.cpp:418)
  at OptimizationPipeline::run (optimization_pipeline.cpp:103)
  ...
```

### ファイルサイズ超過時
```
[OUTPUT] エラー: 出力ファイルの総サイズが上限(16.00 GB)を超えました

現在の状況:
- 総出力サイズ: 16.54 GB
- 問題のファイル: /tmp/output.ll (4.21 GB)
- ディスク空き容量: 2.45 GB

対処法:
1. 最適化レベルを調整: -O0 または -O1 を試してください
2. 出力上限を増やす: --max-output-size=32GB
3. インクリメンタルビルドを使用
```

## 7. 実装優先順位

1. **即座に実装**（1日）
   - 関数レベルの重複検出
   - 基本的なファイルサイズ監視

2. **短期実装**（1週間）
   - ブロックレベル監視
   - ディスク容量チェック

3. **中期実装**（2週間）
   - パターン検出
   - 詳細な統計とレポート

## 8. パフォーマンスへの影響

- **オーバーヘッド**: < 0.1%（通常のコンパイル）
- **メモリ使用**: 数MB（統計情報の保持）
- **無効化可能**: `--no-guard` オプションで完全に無効化

## まとめ

この設計により：
- **ディスク枯渇を防止** - 16GBのデフォルト上限で暴走を防ぐ
- **本番利用に影響なし** - 大規模プロジェクトも正常にビルド可能
- **早期検出** - 問題を早期に発見して開発効率を向上
- **詳細な診断** - 問題の原因を特定しやすい情報を提供