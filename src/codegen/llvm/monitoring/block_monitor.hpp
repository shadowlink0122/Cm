#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace cm::codegen {

// ブロックレベル監視クラス
// コード生成中の無限ループ（同一ブロックへの過剰訪問・命令の周期的生成）を検出する
class BlockMonitor {
   private:
    // ブロック情報の構造体
    struct BlockInfo {
        size_t visit_count = 0;            // 訪問回数
        size_t instruction_count = 0;      // 生成された命令数
        size_t last_hash = 0;              // 最後に生成されたコードのハッシュ
        std::vector<size_t> hash_history;  // ハッシュ履歴
    };

    // 関数ごとのブロック情報
    std::unordered_map<std::string, std::unordered_map<std::string, BlockInfo>> function_blocks;

    // 現在処理中の関数とブロック
    std::string current_function;
    std::string current_block;

    // 設定値
    size_t max_block_visits = 10000;  // ブロックの最大訪問回数（O3最適化では複数回訪問が正常）
    size_t max_instructions_per_block =
        100000;  // ブロックごとの最大命令数（大規模なスライス操作のため）
    size_t max_duplicate_instructions = 1000;  // 同じ命令の最大連続生成数（配列初期化などで必要）

    // 連続する同一命令のカウント
    std::unordered_map<size_t, size_t> consecutive_instruction_count;

    // 命令パターンの検出

    // 周期的パターンの検出

   public:
    // ブロック処理の開始
    void enter_block(const std::string& func_name, const std::string& block_name);

    // ブロック処理の終了
    void exit_block();

    // 命令の追加を記録
    void add_instruction(const std::string& instruction_text);

    // 設定の更新
    void set_max_block_visits(size_t max_visits) { max_block_visits = max_visits; }

    void set_max_instructions(size_t max_inst) { max_instructions_per_block = max_inst; }

    // 統計情報の取得
    std::string get_statistics() const;

    // リセット
    void reset();
};

}  // namespace cm::codegen
