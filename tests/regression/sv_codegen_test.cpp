// ============================================================
// SVコードジェネレータの単体テスト
// ============================================================
// Cmソース断片 → 生成SV文字列 のゴールデンテスト。
// 過去に統合テスト（lint）をすり抜けた「合法だが意味が違うSV」
// を出力する回帰（優先順位括弧・符号付き定数・ループ構造化等）をコード生成器のレベルで検証する。

#include "../../src/codegen/sv/codegen.hpp"
#include "../../src/codegen/sv/expr_tree.hpp"
#include "../../src/frontend/lexer/lexer.hpp"
#include "../../src/frontend/parser/parser.hpp"
#include "../../src/hir/lowering/lowering.hpp"
#include "../../src/mir/lowering/lowering.hpp"
#include "../../src/mir/passes/loop/const_unroll.hpp"
#include "../../src/mir/passes/scalar/folding.hpp"

#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

using namespace cm;

class SVCodegenTest : public ::testing::Test {
   protected:
    // テストケースのCmソースを読み込む（tests/unit/sv_cases/*.cm）。
    // これらはユニットテスト専用の断片で、preprocessor/型チェッカを通さない
    // 経路（Lexer→Parser→HIR→MIR→SVCodeGen）で使用する。
    // 統合テスト（tests/sv/、フルパイプライン+lint/シミュレーション検証）とは
    // 役割が異なり、生成SVテキストの性質（括弧・キャスト構文等）を検証する
    static std::string load_case(const std::string& name) {
        std::string path = std::string(CM_SV_CASE_DIR) + "/" + name + ".cm";
        std::ifstream file(path);
        EXPECT_TRUE(file.is_open()) << "テストケースを開けません: " << path;
        std::stringstream buf;
        buf << file.rdbuf();
        return buf.str();
    }

    // Cmソース → 生成SV文字列
    std::string compile_to_sv(const std::string& code, bool emit_memfile = false,
                              bool unroll_loops = false, bool strict_lint = false,
                              bool fold_constants = false) {
        Lexer lex(code, LexerPlatform::SV);
        std::vector<Token> tokens = lex.tokenize();
        Parser p(tokens);
        auto ast = p.parse();

        // 本番パイプラインと同様に `module NAME;` 宣言からトップ名を取得
        std::string top_module = codegen::sv::extract_top_module_name(ast);

        hir::HirLowering hir_lowering;
        auto hir = hir_lowering.lower(ast);

        mir::MirLowering mir_lowering;
        auto mir = mir_lowering.lower(hir);

        // SVターゲットの本番パイプラインと同じ定数ループ展開（オプトイン）
        if (unroll_loops) {
            mir::opt::unroll_constant_loops(mir);
        }
        // SVターゲットの本番パイプライン（O1以上）と同じ定数畳み込み・恒等式簡約（文数・CFG形状を保存するモード）
        if (fold_constants) {
            mir::opt::ConstantFolding folding(/*fold_terminators=*/false);
            folding.run_on_program(mir);
        }

        codegen::sv::SVCodeGenOptions options;
        options.outputFile = ::testing::TempDir() + "sv_codegen_test_out.sv";
        options.emitMemfile = emit_memfile;
        options.strictLint = strict_lint;
        options.topModule = top_module;
        codegen::sv::SVCodeGen gen(options);
        gen.compile(mir);
        return gen.getGeneratedCode();
    }

    // Cmソース → 生成テストベンチ文字列。
    // //! test: ディレクティブはケースファイル自体から読むため sourceFile を設定し、compile() が outputFile の隣へ書き出した <name>_tb.sv を読み戻す
    std::string compile_to_tb(const std::string& name) {
        std::string path = std::string(CM_SV_CASE_DIR) + "/" + name + ".cm";
        std::string code = load_case(name);

        Lexer lex(code, LexerPlatform::SV);
        std::vector<Token> tokens = lex.tokenize();
        Parser p(tokens);
        auto ast = p.parse();
        std::string top_module = codegen::sv::extract_top_module_name(ast);

        hir::HirLowering hir_lowering;
        auto hir = hir_lowering.lower(ast);
        mir::MirLowering mir_lowering;
        auto mir = mir_lowering.lower(hir);

        codegen::sv::SVCodeGenOptions options;
        options.outputFile = ::testing::TempDir() + "sv_tb_test_out.sv";
        options.sourceFile = path;
        options.topModule = top_module;
        codegen::sv::SVCodeGen gen(options);
        gen.compile(mir);

        std::ifstream tb(::testing::TempDir() + "sv_tb_test_out_tb.sv");
        EXPECT_TRUE(tb.is_open()) << "テストベンチが生成されていません";
        std::stringstream buf;
        buf << tb.rdbuf();
        return buf.str();
    }

    // 部分文字列の存在チェック（失敗時に生成SV全体を表示）
    void expect_contains(const std::string& sv, const std::string& needle) {
        EXPECT_NE(sv.find(needle), std::string::npos)
            << "生成SVに \"" << needle << "\" が含まれていません:\n"
            << sv;
    }

    void expect_not_contains(const std::string& sv, const std::string& needle) {
        EXPECT_EQ(sv.find(needle), std::string::npos)
            << "生成SVに \"" << needle << "\" が含まれてはいけません:\n"
            << sv;
    }
};

// ============================================================
// SV式ツリー（expr_tree）の優先順位プリンタ検証
// ============================================================
using codegen::sv::SVExpr;

// & は == より優先順位が低いため、左辺の & 式には括弧が必要
TEST(SVExprTreeTest, MaskCompareParens) {
    auto e = SVExpr::binary("==", SVExpr::binary("&", SVExpr::atom("a"), SVExpr::atom("32'd256")),
                            SVExpr::atom("32'd0"));
    EXPECT_EQ(e->to_string(), "(a & 32'd256) == 32'd0");
}

// 結合法則を満たす演算子は同順位の連鎖で括弧を省略できる
TEST(SVExprTreeTest, AssociativeChainNoParens) {
    auto left = SVExpr::binary("+", SVExpr::binary("+", SVExpr::atom("a"), SVExpr::atom("b")),
                               SVExpr::atom("c"));
    EXPECT_EQ(left->to_string(), "a + b + c");
    auto right = SVExpr::binary("+", SVExpr::atom("a"),
                                SVExpr::binary("+", SVExpr::atom("b"), SVExpr::atom("c")));
    EXPECT_EQ(right->to_string(), "a + b + c");
}

// 非結合演算子の右オペランドには括弧が必要（a - (b - c)）
TEST(SVExprTreeTest, NonAssociativeRightParens) {
    auto e = SVExpr::binary("-", SVExpr::atom("a"),
                            SVExpr::binary("-", SVExpr::atom("b"), SVExpr::atom("c")));
    EXPECT_EQ(e->to_string(), "a - (b - c)");
}

// 弱い演算子の上に強い演算子: 括弧が必要（(a + b) * c）
TEST(SVExprTreeTest, MulOverAddParens) {
    auto e = SVExpr::binary("*", SVExpr::binary("+", SVExpr::atom("a"), SVExpr::atom("b")),
                            SVExpr::atom("c"));
    EXPECT_EQ(e->to_string(), "(a + b) * c");
}

// 単項演算子の下の二項演算には括弧が必要（~(a | b)）
TEST(SVExprTreeTest, UnaryOverBinaryParens) {
    auto e = SVExpr::unary("~", SVExpr::binary("|", SVExpr::atom("a"), SVExpr::atom("b")));
    EXPECT_EQ(e->to_string(), "~(a | b)");
    // 原子への単項演算は括弧不要
    auto neg = SVExpr::unary("-u", SVExpr::atom("32'sd1"));
    EXPECT_EQ(neg->to_string(), "-32'sd1");
}

// 符号付き変数と整数定数の比較: 定数は 'sd（符号付き）で出力される。
// 32'd0 だと SV では unsigned 比較になり s < 0 が常に偽になる
TEST_F(SVCodegenTest, SignedConstantComparison) {
    const std::string code = load_case("expr/signed_constant_comparison");
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "32'sd0");
    expect_not_contains(sv, "s < 32'd0");
}

// ビットマスクと比較の組み合わせ: 括弧が保持される。
// SVでは == が & より優先されるため、括弧が消えると恒偽になる
TEST_F(SVCodegenTest, OperatorPrecedenceParens) {
    const std::string code = load_case("expr/operator_precedence_parens");
    std::string sv = compile_to_sv(code);
    // 単体テスト経路ではリテラルがint型（'sd）になるため両対応でチェック
    bool has_parens = sv.find("(a & 32'd256) == 32'd0") != std::string::npos ||
                      sv.find("(a & 32'sd256) == 32'sd0") != std::string::npos;
    EXPECT_TRUE(has_parens) << "優先順位の括弧が保持されていません:\n" << sv;
}

// 式の途中の縮小キャストはサイズキャスト N'(...) として出力される
TEST_F(SVCodegenTest, NarrowingCastEmission) {
    const std::string code = load_case("expr/narrowing_cast_emission");
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "8'(");
}

// 符号付き型の右シフトは算術シフト >>> で出力される
TEST_F(SVCodegenTest, ArithmeticShiftRight) {
    const std::string code = load_case("expr/arithmetic_shift_right");
    std::string sv = compile_to_sv(code);
    expect_contains(sv, ">>>");
}

// enumのビット幅はメンバー数ではなく最大タグ値から計算される
TEST_F(SVCodegenTest, EnumExplicitTagWidth) {
    const std::string code = load_case("expr/enum_explicit_tag_width");
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "7'd100");
    expect_not_contains(sv, "1'd100");
}

// モジュールレベル変数の宣言初期値はレジスタ初期値として出力される
TEST_F(SVCodegenTest, RegisterInitialValue) {
    const std::string code = load_case("memory/register_initial_value");
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "counter = 32'd42;");
}

// else if チェーンの全行が先頭のifと同じインデントで出力される（従来はelse if正規化がネスト2段目以降の "end else if" 行にインデント調整を適用せず、1段深くずれてSVの見た目が崩れていた）
TEST_F(SVCodegenTest, ElseIfChainIndent) {
    const std::string code = load_case("control/else_if_chain_indent");
    std::string sv = compile_to_sv(code);

    std::istringstream stream(sv);
    std::string line;
    size_t if_indent = std::string::npos;
    size_t elif_count = 0;
    while (std::getline(stream, line)) {
        size_t indent = line.find_first_not_of(' ');
        if (indent == std::string::npos) {
            continue;
        }
        std::string trimmed = line.substr(indent);
        if (if_indent == std::string::npos && trimmed.rfind("if (sel", 0) == 0) {
            if_indent = indent;
        }
        if (trimmed.rfind("end else if (", 0) == 0) {
            ++elif_count;
            EXPECT_EQ(indent, if_indent) << "misindented: " << line;
        }
    }
    EXPECT_GE(elif_count, 3u) << "else if チェーンが生成されていません";
}

// IOインスタンス（#[input]/#[output]フィールドを持つ構造体のグローバル変数）:
// フィールドがモジュールポートへ展開され、io.field アクセスはポート名へフラット化される。IO構造体自体はデータ型（typedef）として出力されない
TEST_F(SVCodegenTest, IoInstanceExpandsPortsAndFlattensAccess) {
    const std::string code = load_case("module/io_instance");
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "input logic [31:0] a");
    expect_contains(sv, "output logic [31:0] result");
    expect_contains(sv, "result <= a;");
    expect_not_contains(sv, "typedef struct packed");
    expect_not_contains(sv, "Io io;");
    expect_not_contains(sv, "io[");
}

// importされたモジュール（namespace）内で宣言されたextern structインスタンスの型名から名前空間修飾を除去する（従来は "hdmi_out::OSC osc_inst" のような
// 不正なSVが出力されlintエラーになった）
TEST_F(SVCodegenTest, NamespacedExternInstance) {
    const std::string code = load_case("module/namespaced_extern_instance");
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "OSC #(");
    EXPECT_EQ(sv.find("hdmi_out::OSC"), std::string::npos);
}

// ============================================================
// //! test: ディレクティブからのテストベンチ生成
// ============================================================

// 組み合わせ回路: 複数ディレクティブが順にTEST 1/TEST 2として生成され、入力設定・伝搬遅延待ち（#10）・出力表示が含まれる
TEST_F(SVCodegenTest, TestbenchDirectiveCombMultiCase) {
    std::string tb = compile_to_tb("testbench/directive_comb");
    expect_contains(tb, "a = 3;");
    expect_contains(tb, "b = 5;");
    expect_contains(tb, "#10;");
    expect_contains(tb, "TEST 1: sum=%0d");
    expect_contains(tb, "a = 10;");
    expect_contains(tb, "b = 20;");
    expect_contains(tb, "TEST 2: sum=%0d");
    // クロックポートが無いのでクロック生成は出力されない
    expect_not_contains(tb, "always #5");
}

// クロック回路: cycles+入力の複合形式が入力駆動とrepeat待ちに展開され、クロック生成が付与される
TEST_F(SVCodegenTest, TestbenchDirectiveCyclesWithInput) {
    std::string tb = compile_to_tb("testbench/directive_cycles");
    expect_contains(tb, "en = 1;");
    expect_contains(tb, "repeat(3) @(posedge clk);");
    expect_contains(tb, "TEST 1: count=%0d");
    expect_contains(tb, "always #5 clk = ~clk;");
}

// 出力ポートの宣言初期値が電源投入時初期値として出力される（従来は欠落し、条件付き代入のみの出力ポートがシミュレーションでXのまま残った）
TEST_F(SVCodegenTest, OutputPortInitialValue) {
    const std::string code = load_case("module/output_port_initial_value");
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "output logic [31:0] max_seen = 32'd7");
}

// `module NAME;` ヘッダ宣言がSVトップモジュール名に反映される（従来はファイル名由来の名前になり宣言が無視されていた）
TEST_F(SVCodegenTest, ModuleTopName) {
    const std::string code = load_case("module/module_top_name");
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "module my_top");
}

// SV予約語と衝突する識別子（program等）は明確なエラーで停止する（従来はそのまま出力され、iverilog等で構文エラーになっていた）
TEST_F(SVCodegenTest, ReservedIdentifierRejected) {
    const std::string code = load_case("errors/sv_reserved_identifier");
    EXPECT_THROW(compile_to_sv(code), std::runtime_error);
}

// 非対応構文（インラインアセンブリ）はSV007で明示エラーになる（従来は // unsupported statement として静かにコメント化されていた）
TEST_F(SVCodegenTest, InlineAsmRejectedWithSV007) {
    const std::string code = load_case("errors/sv007_inline_asm");
    EXPECT_THROW(compile_to_sv(code), std::runtime_error);
}

// #[test] 内の未対応呼び出し（ユーザー関数）はSV007で明示エラーになる（従来はコメントとして静かに握り潰されていた）
TEST_F(SVCodegenTest, TestbenchUnknownCallRejectedWithSV007) {
    const std::string code = load_case("errors/sv007_test_unknown_call");
    EXPECT_THROW(compile_to_sv(code), std::runtime_error);
}

// 定数畳み込み後に未使用となったlocalparamは出力されない
TEST_F(SVCodegenTest, UnusedLocalparamRemoved) {
    const std::string code = load_case("module/unused_localparam");
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "USED_CONST");
    expect_not_contains(sv, "UNUSED_CONST");
}

// 関数ローカル変数は名前付きalwaysブロック内に宣言される（モジュールスコープへホイストしない）
TEST_F(SVCodegenTest, LocalsDeclaredInsideNamedBlock) {
    const std::string code = load_case("process/block_local_decl");
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "begin : tick_blk");
    // ローカル宣言はブロックラベルより後（＝ブロック内）に現れる
    size_t label_pos = sv.find("begin : tick_blk");
    size_t decl_pos = sv.find("doubled");
    ASSERT_NE(label_pos, std::string::npos) << sv;
    if (decl_pos != std::string::npos) {
        EXPECT_GT(decl_pos, label_pos) << sv;
    }
}

// async関数のクロックが内部信号（OSC等で駆動）の場合、自動のclk/rstポートを注入しない（重複宣言になる不具合の修正）
TEST_F(SVCodegenTest, AsyncInternalClockNoAutoPorts) {
    const std::string code = load_case("module/async_internal_clock");
    std::string sv = compile_to_sv(code);
    expect_not_contains(sv, "input logic clk");
    expect_not_contains(sv, "input logic rst");
    // 内部信号としてのclk宣言（初期値付き）は1つだけ存在する
    size_t first = sv.find("logic clk");
    ASSERT_NE(first, std::string::npos) << sv;
    EXPECT_EQ(sv.find("logic clk", first + 1), std::string::npos) << sv;
}

// プロセス内ループはwhileループとして再構成され、ループ後のコードが到達可能な位置に出力される
TEST_F(SVCodegenTest, WhileLoopReconstruction) {
    const std::string code = load_case("control/while_loop_reconstruction");
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "while (");
    // 代入方式（ブロッキング/ノンブロッキング）は文脈依存のため "= total;" で両対応
    expect_contains(sv, "= total;");
}

// 配列リテラル初期値はinitialブロックとして出力される
TEST_F(SVCodegenTest, ArrayInitialBlock) {
    const std::string code = load_case("memory/array_initial_block");
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "initial begin");
    expect_contains(sv, "rom[0] = 10;");
    expect_contains(sv, "rom[3] = 40;");
}

// 定数トリップカウントのループは静的展開され while が残らない（generate/genvar相当。合成ツールは動的whileを展開できない）
TEST_F(SVCodegenTest, ConstantLoopUnroll) {
    const std::string code = load_case("control/constant_loop_unroll");
    std::string sv = compile_to_sv(code, /*emit_memfile=*/false, /*unroll_loops=*/true);
    expect_not_contains(sv, "while (");
    // 4回分の本体が直列に展開されている（XOR演算が4回出現）
    size_t xor_count = 0;
    for (size_t pos = sv.find(" ^ "); pos != std::string::npos; pos = sv.find(" ^ ", pos + 1)) {
        ++xor_count;
    }
    EXPECT_EQ(xor_count, 4u) << "本体が4回展開されていません:\n" << sv;
}

// #[sv::memfile] 属性付き配列は $readmemh のinitial文として出力される
TEST_F(SVCodegenTest, MemfileReadmemh) {
    const std::string code = load_case("memory/memfile_readmemh");
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "initial $readmemh(\"font.hex\", rom);");
    // memfile指定時は要素代入のinitialブロックは出力されない
    expect_not_contains(sv, "rom[0] = 10;");
}

// 初期値なしの #[sv::memfile] 配列（外部hex提供）でも $readmemh が出力される
TEST_F(SVCodegenTest, MemfileWithoutInitializer) {
    const std::string code = load_case("memory/memfile_without_initializer");
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "initial $readmemh(\"data.hex\", ram);");
    expect_contains(sv, "ram_style");
}

// --emit-memfile: 配列リテラル初期値が.hexファイルとして書き出される
TEST_F(SVCodegenTest, MemfileEmitHexFile) {
    const std::string code = load_case("memory/memfile_emit_hex_file");
    std::string sv = compile_to_sv(code, /*emit_memfile=*/true);
    expect_contains(sv, "initial $readmemh(\"emit_test.hex\", rom);");

    // 出力SVと同じディレクトリに.hexが書き出される
    std::ifstream hex(::testing::TempDir() + "emit_test.hex");
    ASSERT_TRUE(hex.is_open()) << "emit_test.hex が生成されていません";
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(hex, line)) {
        lines.push_back(line);
    }
    ASSERT_EQ(lines.size(), 4u);
    EXPECT_EQ(lines[0], "10");  // 16 → 0x10
    EXPECT_EQ(lines[1], "20");  // 32 → 0x20
    EXPECT_EQ(lines[2], "ff");  // 255 → 0xff
    EXPECT_EQ(lines[3], "00");  // 0 → 0x00
}

// assert() は即時アサーション assert (...) else $error(...); として出力される
TEST_F(SVCodegenTest, ImmediateAssertion) {
    const std::string code = load_case("verify/immediate_assertion");
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "assert (");
    expect_contains(sv, "else $error(\"assertion failed: value out of range\");");
}

// ラッチ推論（Phase 3）: if前にデフォルト代入があれば組み合わせ回路（従来の「if行数 vs else行数」テキスト判定では誤ってラッチ扱いだった）
TEST_F(SVCodegenTest, LatchInferenceDefaultAssignIsComb) {
    const std::string code = load_case("process/latch_inference_default_assign_is_comb");
    std::string sv = compile_to_sv(code);
    expect_not_contains(sv, "ラッチ推論");
}

// ラッチ推論（Phase 3）: if/elseがあっても片側でしか代入されない信号はラッチ（従来のテキスト判定ではif/elseが揃っていると見逃していた）
TEST_F(SVCodegenTest, LatchInferenceOneSidedAssignIsLatch) {
    const std::string code = load_case("process/latch_inference_one_sided_assign_is_latch");
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "ラッチ推論: lout が全パスで代入されません");
}

// 三項演算子の構造的判定（Phase 2b）: 同一変数への単一代入のif/elseはcond ? a : b に、else-ifチェーンは入れ子の三項に畳まれる
TEST_F(SVCodegenTest, StructuralTernaryChain) {
    const std::string code = load_case("expr/structural_ternary_chain");
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "r = (op == 32'sd0) ? a + b : (op == 32'sd1) ? a - b : a ^ b;");
    expect_not_contains(sv, "if (op");
}

// 複数文の分岐は三項化されずif/elseのまま出力される
TEST_F(SVCodegenTest, MultiStatementBranchStaysIfElse) {
    const std::string code = load_case("control/multi_statement_branch_stays_if_else");
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "if (sel == 32'sd1) begin");
    expect_contains(sv, "end else begin");
}

// lint_off抑止（項目9）: UNUSED/UNDRIVENは出力されず、WIDTH系のみ既定で抑止。
// --sv-strict-lint 相当では一切出力されない
TEST_F(SVCodegenTest, LintOffReduction) {
    const std::string code = load_case("verify/lint_off_reduction");
    std::string sv = compile_to_sv(code);
    expect_not_contains(sv, "lint_off UNUSED");
    expect_not_contains(sv, "lint_off UNDRIVEN");
    expect_contains(sv, "lint_off WIDTHTRUNC");
    expect_contains(sv, "lint_off WIDTHEXPAND");

    std::string strict = compile_to_sv(code, false, false, /*strict_lint=*/true);
    expect_not_contains(strict, "lint_off");
}

// #[sv::parameter]: module #(parameter) ヘッダと記号幅の出力
TEST_F(SVCodegenTest, ModuleParameterEmission) {
    const std::string code = load_case("module/module_parameter");
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "module sv_codegen_test_out #(");
    expect_contains(sv, "parameter WIDTH = 8");
    expect_contains(sv, "input logic [WIDTH-1:0] din");
    expect_contains(sv, "output logic [WIDTH-1:0] dout");
    // localparamとしては出力されない
    expect_not_contains(sv, "localparam");
}

// #[sv::tri]: トライステート駆動（inout tri + assign 'z）
TEST_F(SVCodegenTest, TristateEmission) {
    const std::string code = load_case("module/tristate");
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "inout tri sda");
    expect_contains(sv, "assign sda = sda_oe ? sda_out : 1'bz;");
}

// #[sv::sync]: CDC 2FF同期段の生成
TEST_F(SVCodegenTest, CdcSyncEmission) {
    const std::string code = load_case("module/cdc_sync");
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "(* async_reg = \"true\" *) logic btn_sync_meta1;");
    expect_contains(sv, "btn_sync_meta1 <= async_btn;");
    expect_contains(sv, "btn_sync <= btn_sync_meta1;");
}

// 配列型ポートはアンパックド次元を保持する
TEST_F(SVCodegenTest, ArrayPortDimension) {
    const std::string code = load_case("memory/array_port_dimension");
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "data [0:3]");
}

// 定数畳み込み・恒等式簡約（O1以上のSVパイプライン相当）:
// 2*3+4 → 10、x*1/x+0/<<0 は除去され、素の変数参照になる
TEST_F(SVCodegenTest, ConstantFoldingApplied) {
    const std::string code = load_case("expr/const_fold");
    std::string sv = compile_to_sv(code, /*emit_memfile=*/false, /*unroll_loops=*/false,
                                   /*strict_lint=*/false, /*fold_constants=*/true);
    expect_contains(sv, "32'd10");               // c = 2*3+4 が畳み込まれる
    expect_not_contains(sv, "32'sd2 * 32'sd3");  // 元の定数式が残らない
    expect_not_contains(sv, "* 32'sd1");         // x*1 が残らない
    expect_not_contains(sv, "+ 32'sd0");         // x+0 が残らない
    expect_not_contains(sv, "<< 32'sd0");        // x<<0 が残らない
    expect_contains(sv, "z = a;");               // (a+0)<<0 → a
}

// const宣言の定数式（乗算/除算・文字リテラルcast）は最適化レベルに関わらず
// const評価で畳み込まれ、localparamに確定値が出力される
TEST_F(SVCodegenTest, ConstDeclFolding) {
    const std::string code = load_case("expr/const_decl_fold");
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "FRAME = 32'd420000");
    expect_contains(sv, "HALF = 32'd210000");
    expect_contains(sv, "ZERO = 48");
}

// 派生const宣言の展開: 整数のみの式（除算・区間の和・2進リテラル）はlocalparamへ確定値で畳み込まれ、float混在式は式のままSV評価に委譲される
TEST_F(SVCodegenTest, ConstDeclDerivedExpressions) {
    const std::string code = load_case("expr/const_decl_derived");
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "CLK_FREQ = 32'd52500000");  // 210000000 / 4
    expect_contains(sv, "H_TOTAL = 32'd800");        // 各区間の和
    expect_contains(sv, "CTRL_00 = 32'd852");        // 0b1101010100
    expect_contains(sv, "CLK_FREQ * 0.02");          // float混在はSV側で評価
}

// O0相当（畳み込みなし）では従来どおり式がそのまま出力される（後方互換）
TEST_F(SVCodegenTest, ConstantFoldingNotAppliedByDefault) {
    const std::string code = load_case("expr/const_fold");
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "32'sd2 * 32'sd3 + 32'sd4");
}
