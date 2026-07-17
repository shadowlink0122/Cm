#pragma once

#include "../../mir/nodes.hpp"
#include "../buffered_codegen.hpp"
#include "expr_tree.hpp"

#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cm::ast {
struct Program;
}

namespace cm::codegen::sv {

/// `module NAME;` ヘッダ宣言からトップモジュール名を取得する。
/// トップレベルの本体なしModuleDecl（最初のもの）を採用し、無ければ空文字を返す
std::string extract_top_module_name(const ast::Program& program);

/// SystemVerilog（IEEE 1800-2017）の予約語かどうかを判定する
bool is_sv_reserved_word(const std::string& name);

// SystemVerilog コード生成オプション
struct SVCodeGenOptions {
    std::string outputFile = "output.sv";
    std::string sourceFile;  // 入力ソースファイル（テストベンチ生成用）
    std::string
        topModule;  // `module NAME;` 宣言由来のトップモジュール名（空ならファイル名から推定）
    bool verbose = false;      // 詳細出力
    int indentSpaces = 4;      // インデント幅
    bool emitMemfile = false;  // 配列リテラル初期値を.hexファイルとして書き出す
    bool strictLint = false;   // lint_off抑止を一切出力しない（--sv-strict-lint）
    bool keepAlwaysFF = false;  // always_ff/always_comb等を保持（--sv-always-ff。
                                // 既定はGowin EDA互換のためalways @へ置換する）
    bool warnNba = false;  // posedge関数内で代入済み状態変数の参照を警告（--sv-warn-nba）
    bool emitConstraints = false;  // #[sv::pin]属性から.cst/.tclを生成（--emit-constraints）
    std::string devicePN;          // //! sv: device: の型番（.tcl生成に使用）
    std::string deviceVersion;     // //! sv: device: の版（例: C）
    std::vector<std::string> toolOptions;  // //! sv: option: の列（set_option -<name> 1）
};

// モジュールポート情報
struct SVPort {
    enum Direction { Input, Output, InOut };
    Direction direction;
    std::string name;
    std::string sv_type;  // "logic", "logic [7:0]" 等
    int bit_width = 1;
    std::string array_suffix;  // アンパックド次元 " [0:N-1]"（配列型ポート用）
    std::string init_value;  // 出力ポートの電源投入時初期値（空なら初期化子なし）
};

// SVモジュール情報
struct SVModule {
    std::string name;
    std::vector<SVPort> ports;
    std::vector<std::string> parameters;           // localparam宣言
    std::vector<std::string> header_parameters;    // module #(parameter ...) 宣言
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

    // SV予約語と衝突する識別子を検査し、衝突があれば例外で停止する
    void validateReservedIdentifiers(const mir::MirProgram& program) const;
    // --sv-warn-nba: posedge関数内で代入済み状態変数の参照を警告する
    void warnNbaReadback(const mir::MirProgram& program) const;

    // 生成されたSVコードを取得
    std::string getGeneratedCode() const { return generated_code_; }

   private:
    SVCodeGenOptions options_;
    std::string generated_code_;
    // #[sv::parameter] 付きconstの名前（幅の記号出力用）
    std::set<std::string> sv_param_names_;
    // #[test] 関数（テストベンチ生成で使用、宣言順）
    std::vector<const mir::MirFunction*> testbench_fns_;
    // プロセスのクロック信号名（テストベンチのクロック検出用）
    std::set<std::string> process_clock_names_;
    // テストベンチで使用するクロックポート名（generateTestbenchで決定）
    std::string tb_clk_name_;
    // テストベンチ生成中フラグ（#[test] からのDUT内部信号参照を dut. 階層参照へ解決）
    bool emitting_testbench_ = false;
    // テストベンチ対象モジュールのポート名 / 入力ポート名
    std::set<std::string> tb_port_names_;
    std::set<std::string> tb_input_names_;
    // モジュールスコープ信号名（内部レジスタ含む。dut. 階層参照の判定用）
    std::set<std::string> module_signal_names_;
    int indent_level_ = 0;
    std::unordered_map<std::string, int> global_string_lengths_;
    // IOインスタンス（#[input]/#[output]フィールドを持つ構造体のグローバル変数）。
    // インスタンス名 → フィールド名リスト（field_id順）。
    // フィールドはモジュールポートへ展開され、io.field アクセスはポート名へ写像される
    std::unordered_map<std::string, std::vector<std::string>> io_instance_fields_;

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

    // 式ツリー化 Phase 1: 単一定義テンポラリの式ツリー（関数ごとにリセット）
    std::unordered_map<mir::LocalId, SVExprPtr> temp_trees_;
    std::unordered_set<mir::LocalId> single_def_temps_;

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
    // === 式ツリー（式ツリー化 Phase 1）===
    // rvalueを式ツリーとして構築する。単一定義テンポラリの参照は
    // 記録済みのツリーを構造的にスプライスし、優先順位括弧は
    // プリンタが構造から決定する（テキスト置換の括弧補正を不要にする）
    SVExprPtr buildRvalueTree(const mir::MirRvalue& rvalue, const mir::MirFunction& func,
                              int target_width = 0);
    SVExprPtr buildOperandTree(const mir::MirOperand& op, const mir::MirFunction& func,
                               int target_width = 0);

    // 関数内で1回だけ代入されるコンパイラ生成テンポラリ（_tNNN）を収集する
    void collectSingleDefTemps(const mir::MirFunction& func);

    // 組み合わせブロックの代入完全性解析（式ツリー化 Phase 3）。
    // 関数内で書き込まれるモジュールレベル信号のうち、entryから各returnまでの
    // すべての制御パスで代入されるとは限らないものを返す（must-assignデータフロー）。
    // 空でなければラッチ推論となる
    std::vector<std::string> findIncompletelyAssignedSignals(const mir::MirFunction& func);

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
    std::string emitTestbenchStmt(const hir::HirStmt& stmt);
    // #[test] からの代入先を検証（入力ポート以外への代入をエラーにする）
    void validateTestbenchAssignTarget(const hir::HirExpr& lhs);

    // === XDC制約ファイル出力 ===
    std::string generateXDC(const mir::MirProgram& program);

    // 物理制約生成（constraints.cpp）
    struct CollectedPin {
        std::string port_name;
        std::string pin_loc;
        std::vector<std::pair<std::string, std::string>> params;
    };
    std::vector<CollectedPin> collectPins(const mir::MirProgram& program);
    std::string generateCST(const mir::MirProgram& program);
    std::string generateProjectTCL(const std::string& module_name, const std::string& sv_path,
                                   const std::string& cst_path);

    // === 非合成型チェック ===
    bool validateSynthesizableTypes(const mir::MirProgram& program);

    // === ファイル出力 ===
    void writeToFile(const std::string& content, const std::string& path);
};

// //! sv: device: / //! sv: option: ディレクティブの抽出結果
struct SvProjectDirectives {
    std::string device_pn;
    std::string device_version;
    std::vector<std::string> tool_options;
};
SvProjectDirectives parse_sv_project_directives(const std::string& source);

}  // namespace cm::codegen::sv
