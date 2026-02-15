#pragma once

#include "nodes.hpp"

#include <sstream>
#include <unordered_set>

namespace cm::mir {

// ============================================================
// MIRプリンター - 安全でロバストなデバッグ出力
// ============================================================
// 特徴:
// - 循環参照検出による無限ループ防止
// - 深度制限による再帰オーバーフロー防止
// - stringstreamによる一括出力でブロッキング回避
// ============================================================

class MirPrinter {
   public:
    struct Options {
        bool show_types = true;   // 型情報を表示
        bool show_spans = false;  // ソース位置を表示
        bool verbose = false;     // 詳細モード
        int max_type_depth = 5;   // 型の再帰表示深度
    };

    MirPrinter() : options_{} {}
    explicit MirPrinter(Options opts) : options_(opts) {}

    // MIRプログラムを文字列に変換
    std::string to_string(const MirProgram& program);

    // MIRプログラムを出力
    void print(const MirProgram& program, std::ostream& out);

   private:
    Options options_;
    // 循環参照検出用
    mutable std::unordered_set<const void*> visited_types_;

    // 安全な型文字列変換
    std::string safe_type_to_string(hir::TypePtr type, int depth = 0) const;

    // 出力メソッド
    void print_function(const MirFunction& func, std::ostream& out);
    void print_block(const BasicBlock& block, const MirFunction& func, std::ostream& out);
    void print_statement(const MirStatement& stmt, std::ostream& out);
    void print_terminator(const MirTerminator& term, std::ostream& out);
    void print_place(const MirPlace& place, std::ostream& out);
    void print_operand(const MirOperand& op, std::ostream& out);
    void print_rvalue(const MirRvalue& rv, std::ostream& out);
    void print_constant(const MirConstant& c, std::ostream& out);

    // ヘルパー関数
    static std::string binary_op_to_string(MirBinaryOp op);
    static std::string unary_op_to_string(MirUnaryOp op);
    static std::string aggregate_kind_to_string(const AggregateKind& kind);
    static std::string escape_string(const std::string& s);
};

}  // namespace cm::mir
