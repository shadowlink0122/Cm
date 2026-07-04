// ============================================================
// SVコードジェネレータの単体テスト
// ============================================================
// Cmソース断片 → 生成SV文字列 のゴールデンテスト。
// 過去に統合テスト（lint）をすり抜けた「合法だが意味が違うSV」
// を出力する回帰（優先順位括弧・符号付き定数・ループ構造化等）を
// コード生成器のレベルで検証する。

#include "../../src/codegen/sv/codegen.hpp"
#include "../../src/frontend/lexer/lexer.hpp"
#include "../../src/frontend/parser/parser.hpp"
#include "../../src/hir/lowering/lowering.hpp"
#include "../../src/mir/lowering/lowering.hpp"

#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

using namespace cm;

class SVCodegenTest : public ::testing::Test {
   protected:
    // Cmソース → 生成SV文字列
    std::string compile_to_sv(const std::string& code, bool emit_memfile = false) {
        Lexer lex(code);
        std::vector<Token> tokens = lex.tokenize();
        Parser p(tokens);
        auto ast = p.parse();

        hir::HirLowering hir_lowering;
        auto hir = hir_lowering.lower(ast);

        mir::MirLowering mir_lowering;
        auto mir = mir_lowering.lower(hir);

        codegen::sv::SVCodeGenOptions options;
        options.outputFile = ::testing::TempDir() + "sv_codegen_test_out.sv";
        options.emitMemfile = emit_memfile;
        codegen::sv::SVCodeGen gen(options);
        gen.compile(mir);
        return gen.getGeneratedCode();
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

// 符号付き変数と整数定数の比較: 定数は 'sd（符号付き）で出力される。
// 32'd0 だと SV では unsigned 比較になり s < 0 が常に偽になる
TEST_F(SVCodegenTest, SignedConstantComparison) {
    const std::string code = R"(
        #[input] int s;
        #[output] uint neg = 0;

        void check() {
            if (s < 0) {
                neg = 1;
            } else {
                neg = 0;
            }
        }
    )";
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "32'sd0");
    expect_not_contains(sv, "s < 32'd0");
}

// ビットマスクと比較の組み合わせ: 括弧が保持される。
// SVでは == が & より優先されるため、括弧が消えると恒偽になる
TEST_F(SVCodegenTest, OperatorPrecedenceParens) {
    const std::string code = R"(
        #[input] uint a;
        #[output] uint flag = 0;

        void check() {
            if ((a & 256) == 0) {
                flag = 1;
            } else {
                flag = 0;
            }
        }
    )";
    std::string sv = compile_to_sv(code);
    // 単体テスト経路ではリテラルがint型（'sd）になるため両対応でチェック
    bool has_parens = sv.find("(a & 32'd256) == 32'd0") != std::string::npos ||
                      sv.find("(a & 32'sd256) == 32'sd0") != std::string::npos;
    EXPECT_TRUE(has_parens) << "優先順位の括弧が保持されていません:\n" << sv;
}

// 式の途中の縮小キャストはサイズキャスト N'(...) として出力される
TEST_F(SVCodegenTest, NarrowingCastEmission) {
    const std::string code = R"(
        #[input] uint a;
        #[output] uint wide = 0;

        void calc() {
            wide = ((a + 300) as utiny) + 1000;
        }
    )";
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "8'(");
}

// 符号付き型の右シフトは算術シフト >>> で出力される
TEST_F(SVCodegenTest, ArithmeticShiftRight) {
    const std::string code = R"(
        #[input] int s;
        #[output] int shr2 = 0;

        void shift() {
            shr2 = s >> 2;
        }
    )";
    std::string sv = compile_to_sv(code);
    expect_contains(sv, ">>>");
}

// enumのビット幅はメンバー数ではなく最大タグ値から計算される
TEST_F(SVCodegenTest, EnumExplicitTagWidth) {
    const std::string code = R"(
        enum Status {
            IDLE = 0,
            ERROR = 100
        }

        #[input] uint sel;
        #[output] uint code = 0;

        void pick() {
            if (sel == 1) {
                code = Status::ERROR as uint;
            } else {
                code = Status::IDLE as uint;
            }
        }
    )";
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "7'd100");
    expect_not_contains(sv, "1'd100");
}

// モジュールレベル変数の宣言初期値はレジスタ初期値として出力される
TEST_F(SVCodegenTest, RegisterInitialValue) {
    const std::string code = R"(
        #[input] bool clk = false;
        #[output] uint out = 0;

        uint counter = 42;

        void tick(posedge clk) {
            out = counter;
            counter = counter + 1;
        }
    )";
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "counter = 32'd42;");
}

// プロセス内ループはwhileループとして再構成され、
// ループ後のコードが到達可能な位置に出力される
TEST_F(SVCodegenTest, WhileLoopReconstruction) {
    const std::string code = R"(
        #[input] bool clk = false;
        #[output] uint sum = 0;

        void accumulate(posedge clk) {
            uint total = 0;
            for (uint i = 0; i < 4; i = i + 1) {
                total = total + i;
            }
            sum = total;
        }
    )";
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "while (");
    // 代入方式（ブロッキング/ノンブロッキング）は文脈依存のため "= total;" で両対応
    expect_contains(sv, "= total;");
}

// 配列リテラル初期値はinitialブロックとして出力される
TEST_F(SVCodegenTest, ArrayInitialBlock) {
    const std::string code = R"(
        #[input] bool clk = false;
        #[input] uint idx;
        #[output] utiny out = 0;

        utiny[4] rom = [10, 20, 30, 40];

        void read(posedge clk) {
            if (idx < 4) {
                out = rom[idx];
            }
        }
    )";
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "initial begin");
    expect_contains(sv, "rom[0] = 10;");
    expect_contains(sv, "rom[3] = 40;");
}

// #[sv::memfile] 属性付き配列は $readmemh のinitial文として出力される
TEST_F(SVCodegenTest, MemfileReadmemh) {
    const std::string code = R"(
        #[input] bool clk = false;
        #[input] uint idx;
        #[output] utiny out = 0;

        #[sv::memfile("font.hex")]
        utiny[4] rom = [10, 20, 30, 40];

        void read(posedge clk) {
            if (idx < 4) {
                out = rom[idx];
            }
        }
    )";
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "initial $readmemh(\"font.hex\", rom);");
    // memfile指定時は要素代入のinitialブロックは出力されない
    expect_not_contains(sv, "rom[0] = 10;");
}

// 初期値なしの #[sv::memfile] 配列（外部hex提供）でも $readmemh が出力される
TEST_F(SVCodegenTest, MemfileWithoutInitializer) {
    const std::string code = R"(
        #[input] bool clk = false;
        #[input] uint idx;
        #[output] utiny out = 0;

        #[sv::bram]
        #[sv::memfile("data.hex")]
        utiny[8] ram;

        void read(posedge clk) {
            if (idx < 8) {
                out = ram[idx];
            }
        }
    )";
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "initial $readmemh(\"data.hex\", ram);");
    expect_contains(sv, "ram_style");
}

// --emit-memfile: 配列リテラル初期値が.hexファイルとして書き出される
TEST_F(SVCodegenTest, MemfileEmitHexFile) {
    const std::string code = R"(
        #[input] bool clk = false;
        #[input] uint idx;
        #[output] utiny out = 0;

        #[sv::memfile("emit_test.hex")]
        utiny[4] rom = [16, 32, 255, 0];

        void read(posedge clk) {
            if (idx < 4) {
                out = rom[idx];
            }
        }
    )";
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
    const std::string code = R"(
        #[input] bool clk = false;
        #[input] uint value;
        #[output] uint out = 0;

        void check(posedge clk) {
            assert(value < 100, "value out of range");
            out = value;
        }
    )";
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "assert (");
    expect_contains(sv, "else $error(\"assertion failed: value out of range\");");
}

// 配列型ポートはアンパックド次元を保持する
TEST_F(SVCodegenTest, ArrayPortDimension) {
    const std::string code = R"(
        #[input] bool clk = false;
        #[input] uint idx;
        #[output] uint[4] data;

        void update(posedge clk) {
            if (idx < 4) {
                data[idx] = idx;
            }
        }
    )";
    std::string sv = compile_to_sv(code);
    expect_contains(sv, "data [0:3]");
}
