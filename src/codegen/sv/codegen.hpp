#pragma once

#include "../../mir/nodes.hpp"
#include "../buffered_codegen.hpp"

#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cm::codegen::sv {

// SystemVerilog コード生成オプション
struct SVCodeGenOptions {
    std::string outputFile = "output.sv";
    std::string sourceFile;    // 入力ソースファイル（テストベンチ生成用）
    bool verbose = false;      // 詳細出力
    int indentSpaces = 4;      // インデント幅
    bool emitMemfile = false;  // 配列リテラル初期値を.hexファイルとして書き出す
};

// モジュールポート情報
struct SVPort {
    enum Direction { Input, Output, InOut };
    Direction direction;
    std::string name;
    std::string sv_type;  // "logic", "logic [7:0]" 等
    int bit_width = 1;
    std::string array_suffix;  // アンパックド次元 " [0:N-1]"（配列型ポート用）
};

// SVモジュール情報
struct SVModule {
    std::string name;
    std::vector<SVPort> ports;
    std::vector<std::string> parameters;           // parameter宣言
    std::vector<std::string> type_declarations;    // typedef enum/struct packed 宣言
    std::vector<std::string> always_ff_blocks;     // always_ff ブロック
    std::vector<std::string> always_comb_blocks;   // always_comb ブロック
    std::vector<std::string> always_latch_blocks;  // always_latch ブロック
    std::vector<std::string> assign_statements;    // assign 文
    std::vector<std::string> function_blocks;      // function automatic ブロック
    std::vector<std::string> wire_declarations;    // 内部ワイヤ宣言
    std::vector<std::string> reg_declarations;     // 内部レジスタ宣言
    std::vector<std::string> instance_blocks;      // extern struct インスタンス化文
    std::vector<std::string> initial_blocks;  // initial ブロック（シミュレーション用）
};

// SystemVerilog コードジェネレータ
class SVCodeGen : public BufferedCodeGenerator {
   public:
    explicit SVCodeGen(const SVCodeGenOptions& options = {});

    // MIRプログラムからSystemVerilogを生成
    void compile(const mir::MirProgram& program);

    // 生成されたSVコードを取得
    std::string getGeneratedCode() const { return generated_code_; }

   private:
    SVCodeGenOptions options_;
    std::string generated_code_;
    int indent_level_ = 0;
    std::unordered_map<std::string, int> global_string_lengths_;

    // モジュール情報
    std::vector<SVModule> modules_;

    // whileループ再構成中のexitブロックIDスタック
    // （ループ本体内からexitへの分岐をループ脱出として出力するために使用）
    std::vector<size_t> loop_exit_stack_;

    // ループを囲む名前付きブロックの名前スタック。
    // ループ脱出は break（SV-2005キーワード）ではなく
    // disable <名前>（Verilog-1995互換）で出力する。
    // 古いIcarus Verilog（v11以前）やGowin系ツールはbreak未対応のため
    std::vector<std::string> loop_name_stack_;
    int loop_name_counter_ = 0;

    // 現在出力中の関数のループヘッダ→ラッチ一覧
    // （DominatorTree構築は高コストのため関数ごとに1回だけ計算してキャッシュ）
    std::unordered_map<size_t, std::vector<size_t>> current_loop_latches_;

    // === 型マッピング ===
    // Cm型 → SV型文字列（packed dimension のみ）
    std::string mapType(const hir::TypePtr& type) const;
    // ビット幅を取得
    int getBitWidth(const hir::TypePtr& type) const;
    // 配列型のアンパックドディメンションサフィックスを生成
    // 例: uint[1024] → " [0:1023]", bit[8] → "" (packedとして処理済み)
    std::string getArraySuffix(const hir::TypePtr& type) const;

    // === コード出力ヘルパー ===
    void emit(const std::string& code);
    void emitLine(const std::string& code);
    void emitIndented(const std::string& code);
    void increaseIndent();
    void decreaseIndent();
    std::string indent() const;

    // === モジュール生成 ===
    void emitFileHeader();
    void emitModule(const SVModule& mod);
    void emitPortList(const std::vector<SVPort>& ports);

    // === MIR解析 ===
    // MIRからSVモジュール情報を抽出
    void analyzeMIR(const mir::MirProgram& program);
    // 関数からalways_ff/always_combブロックを生成
    void analyzeFunction(const mir::MirFunction& func, SVModule& mod);
    // 基本ブロックから文を生成
    std::string emitBlock(const mir::BasicBlock& block, const mir::MirFunction& func);
    // 文を生成
    std::string emitStatement(const mir::MirStatement& stmt, const mir::MirFunction& func);
    // 式（オペランド）を生成
    std::string emitOperand(const mir::MirOperand& operand, const mir::MirFunction& func,
                            int target_width = 0);
    // 右辺値を生成
    std::string emitRvalue(const mir::MirRvalue& rvalue, const mir::MirFunction& func,
                           int target_width = 0);
    // Place（左辺値）を生成
    std::string emitPlace(const mir::MirPlace& place, const mir::MirFunction& func);

    // === CFG走査ベースのSV出力 ===
    // 基本ブロックを再帰的に走査してif/else等を構造化出力
    void emitBlockRecursive(const mir::MirFunction& func, size_t block_id,
                            std::set<size_t>& visited, std::ostringstream& ss,
                            size_t merge_block = SIZE_MAX);
    // ターミネータをSVに変換
    // current_block: このターミネータを持つブロックのID（ループヘッダ検出用）
    void emitTerminator(const mir::MirTerminator& term, const mir::MirFunction& func,
                        std::set<size_t>& visited, std::ostringstream& ss, size_t merge_block,
                        size_t current_block = SIZE_MAX);
    // 2つの分岐先が合流するブロックを探す
    size_t findMergeBlock(const mir::MirFunction& func, size_t then_block, size_t else_block);

    // === 配列初期値 ===
    // 配列リテラル初期値をinitialブロックとして生成
    std::string buildArrayInitial(const mir::MirGlobalVar& gv, const std::string& var_name);

    // #[sv::memfile("path.hex")] 属性からパスを取り出す（無ければ空文字列）
    static std::string getMemfilePath(const mir::MirGlobalVar& gv);

    // 配列初期値のinitial出力（memfile属性があれば $readmemh、無ければ要素代入）
    std::string buildArrayInitialOrReadmem(const mir::MirGlobalVar& gv,
                                           const std::string& var_name);

    // --emit-memfile: 配列リテラル初期値を.hexファイルとして書き出す
    void emitMemfileIfRequested(const mir::MirGlobalVar& gv, const std::string& memfile_path);

    // === 定数リテラル ===
    std::string emitConstant(const mir::MirConstant& constant, const hir::TypePtr& type,
                             int target_width = 0);

    // === HIR式/文（assign文、initial block用） ===
    std::string emitHirExpr(const hir::HirExpr& expr);
    std::string emitHirStmt(const hir::HirStmt& stmt);

    // === テストベンチ自動生成 ===
    std::string generateTestbench(const SVModule& mod);

    // === XDC制約ファイル出力 ===
    std::string generateXDC(const mir::MirProgram& program);

    // === 非合成型チェック ===
    bool validateSynthesizableTypes(const mir::MirProgram& program);

    // === ファイル出力 ===
    void writeToFile(const std::string& content, const std::string& path);
};

}  // namespace cm::codegen::sv
