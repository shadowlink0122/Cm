// ============================================================
// SVテストベンチ生成 - #[test] 関数と //! test: ディレクティブから
// 自動テストベンチ（*_tb.sv）を生成する
// ============================================================

#include "../../frontend/ast/typedef.hpp"
#include "codegen.hpp"

#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

namespace cm::codegen::sv {

std::string SVCodeGen::generateTestbench(const SVModule& mod) {
    std::ostringstream ss;

    // ソースファイルから//! test:コメントをパース
    struct TestCase {
        std::vector<std::pair<std::string, std::string>> inputs;    // {name, value}
        std::vector<std::pair<std::string, std::string>> expected;  // {name, value}
        int cycles = 0;  // async用: クロックサイクル数
    };
    std::vector<TestCase> test_cases;

    if (!options_.sourceFile.empty()) {
        std::ifstream src(options_.sourceFile);
        if (src.is_open()) {
            std::string line;
            while (std::getline(src, line)) {
                // "//! test:" プレフィックスを検出
                auto pos = line.find("//! test:");
                if (pos == std::string::npos)
                    continue;
                std::string test_spec = line.substr(pos + 9);

                TestCase tc;
                // "->" で入力と期待出力を分割
                auto arrow = test_spec.find("->");
                std::string input_part, output_part;
                if (arrow != std::string::npos) {
                    input_part = test_spec.substr(0, arrow);
                    output_part = test_spec.substr(arrow + 2);
                } else {
                    input_part = test_spec;
                }

                // 入力パース: "a=3, b=5" or "cycles=5"
                std::istringstream iss(input_part);
                std::string token;
                while (std::getline(iss, token, ',')) {
                    // 空白除去
                    size_t s = token.find_first_not_of(' ');
                    size_t e = token.find_last_not_of(' ');
                    if (s == std::string::npos)
                        continue;
                    token = token.substr(s, e - s + 1);

                    auto eq = token.find('=');
                    if (eq != std::string::npos) {
                        std::string name = token.substr(0, eq);
                        std::string val = token.substr(eq + 1);
                        // 名前と値の空白除去
                        size_t ns = name.find_first_not_of(' ');
                        size_t ne = name.find_last_not_of(' ');
                        if (ns != std::string::npos)
                            name = name.substr(ns, ne - ns + 1);
                        size_t vs = val.find_first_not_of(' ');
                        size_t ve = val.find_last_not_of(' ');
                        if (vs != std::string::npos)
                            val = val.substr(vs, ve - vs + 1);

                        if (name == "cycles") {
                            tc.cycles = std::stoi(val);
                        } else {
                            tc.inputs.push_back({name, val});
                        }
                    }
                }

                // 期待出力パース: "result=8, out=0"
                if (!output_part.empty()) {
                    std::istringstream oss(output_part);
                    while (std::getline(oss, token, ',')) {
                        size_t s = token.find_first_not_of(' ');
                        size_t e = token.find_last_not_of(' ');
                        if (s == std::string::npos)
                            continue;
                        token = token.substr(s, e - s + 1);

                        auto eq = token.find('=');
                        if (eq != std::string::npos) {
                            std::string name = token.substr(0, eq);
                            std::string val = token.substr(eq + 1);
                            size_t ns = name.find_first_not_of(' ');
                            size_t ne = name.find_last_not_of(' ');
                            if (ns != std::string::npos)
                                name = name.substr(ns, ne - ns + 1);
                            size_t vs = val.find_first_not_of(' ');
                            size_t ve = val.find_last_not_of(' ');
                            if (vs != std::string::npos)
                                val = val.substr(vs, ve - vs + 1);
                            tc.expected.push_back({name, val});
                        }
                    }
                }

                test_cases.push_back(tc);
            }
        }
    }

    // テスト内容（#[test] 関数 / //! test: ディレクティブ）が無ければ
    // テストベンチ自体を生成しない
    bool has_test_fn = false;
    for (const auto* fn : testbench_fns_) {
        if (fn && !fn->hir_stmts.empty()) {
            has_test_fn = true;
            break;
        }
    }
    if (test_cases.empty() && !has_test_fn) {
        return "";
    }

    // #[test] からの参照解決用にポート名を記録し、テストベンチ生成モードへ
    // （DUT内部信号の読み取りは dut. 階層参照として出力する）
    tb_port_names_.clear();
    tb_input_names_.clear();
    for (const auto& port : mod.ports) {
        tb_port_names_.insert(port.name);
        if (port.direction == SVPort::Input) {
            tb_input_names_.insert(port.name);
        }
    }
    emitting_testbench_ = true;

    ss << "// 自動生成テストベンチ - Cm compiler\n";
    ss << "`timescale 1ns / 1ps\n\n";

    ss << "module " << mod.name << "_tb;\n\n";

    // モジュールパラメータをTB側にlocalparamとして写す
    // （信号宣言の [WIDTH-1:0] などがTB内でも解決できるように）
    for (const auto& hp : mod.header_parameters) {
        std::string lp = hp;
        const std::string pfx = "parameter ";
        if (lp.rfind(pfx, 0) == 0) {
            lp = "localparam " + lp.substr(pfx.size());
        }
        ss << "    " << lp << ";\n";
    }
    if (!mod.header_parameters.empty()) {
        ss << "\n";
    }

    // ポートに対応する信号宣言
    for (const auto& port : mod.ports) {
        ss << "    " << port.sv_type << " " << port.name << port.array_suffix << ";\n";
    }
    ss << "\n";

    // DUTインスタンス化
    ss << "    // DUTインスタンス\n";
    ss << "    " << mod.name << " dut (\n";
    for (size_t i = 0; i < mod.ports.size(); ++i) {
        ss << "        ." << mod.ports[i].name << "(" << mod.ports[i].name << ")";
        if (i + 1 < mod.ports.size())
            ss << ",";
        ss << "\n";
    }
    ss << "    );\n\n";

    // クロック生成: "clk" ポート、なければプロセスのクロックとして
    // 使われている入力ポート（pixel_clk 等）を採用する
    bool has_clk = false;
    std::string tb_clk = "clk";
    for (const auto& port : mod.ports) {
        if (port.name == "clk" && port.direction == SVPort::Input) {
            has_clk = true;
        }
    }
    if (!has_clk) {
        for (const auto& port : mod.ports) {
            if (port.direction == SVPort::Input && process_clock_names_.count(port.name)) {
                has_clk = true;
                tb_clk = port.name;
                break;
            }
        }
    }
    tb_clk_name_ = tb_clk;
    std::string rst_name;         // リセット信号の実際のポート名
    bool rst_active_low = false;  // アクティブLowリセットかどうか
    for (const auto& port : mod.ports) {
        if (port.direction == SVPort::Input) {
            if (port.name == "rst") {
                rst_name = "rst";
                rst_active_low = false;
            } else if (port.name == "rst_n") {
                rst_name = "rst_n";
                rst_active_low = true;
            }
        }
    }
    bool has_rst = !rst_name.empty();

    if (has_clk) {
        ss << "    // クロック生成 (10ns周期 = 100MHz)\n";
        ss << "    initial " << tb_clk << " = 0;\n";
        ss << "    always #5 " << tb_clk << " = ~" << tb_clk << ";\n\n";
    }

    // テストシーケンス
    // VCDはシミュレーション実行時のカレントディレクトリに出力する。
    // コンパイル時の-oの相対パスを埋め込むと、シミュレータを別の
    // ディレクトリから実行したときに開けず異常終了するため
    ss << "    // テストシーケンス\n";
    ss << "    initial begin\n";
    ss << "        $dumpfile(\"" << mod.name << "_tb.vcd\");\n";
    ss << "        $dumpvars(0, " << mod.name << "_tb);\n\n";

    // 入力ポート初期化（クロックはクロック生成側で駆動する）
    for (const auto& port : mod.ports) {
        if (port.direction == SVPort::Input && port.name != "clk" && port.name != tb_clk) {
            ss << "        " << port.name << " = 0;\n";
        }
    }
    ss << "\n";

    // リセットシーケンス（実際のポート名を使用）
    if (has_rst) {
        ss << "        // リセット\n";
        if (rst_active_low) {
            // アクティブLow: 0→1（リセット解除）
            ss << "        " << rst_name << " = 0;\n";
            ss << "        #20;\n";
            ss << "        " << rst_name << " = 1;\n";
        } else {
            // アクティブHigh: 1→0（リセット解除）
            ss << "        " << rst_name << " = 1;\n";
            ss << "        #20;\n";
            ss << "        " << rst_name << " = 0;\n";
        }
        ss << "        #10;\n\n";
    }

    bool has_tb_stmts = false;
    for (const auto* fn : testbench_fns_) {
        if (fn && !fn->hir_stmts.empty()) {
            has_tb_stmts = true;
            break;
        }
    }
    if (has_tb_stmts) {
        // #[test] 関数によるサイクル精度テストシーケンス（宣言順に逐次実行）
        for (const auto* fn : testbench_fns_) {
            if (!fn || fn->hir_stmts.empty()) {
                continue;
            }
            ss << "        // ---- test: " << fn->name << " ----\n";
            for (const auto* stmt : fn->hir_stmts) {
                if (stmt) {
                    ss << emitTestbenchStmt(*stmt);
                }
            }
            ss << "\n";
        }
    } else if (!test_cases.empty()) {
        // テストシナリオベースの検証
        int test_num = 1;
        for (const auto& tc : test_cases) {
            ss << "        // テスト " << test_num << "\n";

            // 入力値設定
            for (const auto& [name, val] : tc.inputs) {
                ss << "        " << name << " = " << val << ";\n";
            }

            // 評価待ち
            if (tc.cycles > 0) {
                // async: 指定サイクル分クロックを待つ
                ss << "        repeat(" << tc.cycles << ") @(posedge clk);\n";
                ss << "        #1; // 安定化\n";
            } else if (has_clk) {
                // クロック付きだがcycles未指定: 1サイクル待ち
                ss << "        @(posedge clk);\n";
                ss << "        #1;\n";
            } else {
                // 組み合わせ回路: 伝搬遅延待ち
                ss << "        #10;\n";
            }

            // 出力値の表示と検証
            for (const auto& [name, val] : tc.expected) {
                ss << "        $display(\"TEST " << test_num << ": " << name << "=%0d\", " << name
                   << ");\n";
            }
            ss << "\n";
            test_num++;
        }
    } else {
        // テストシナリオなし: 基本的な動作確認
        ss << "        // テスト実行\n";
        ss << "        #100;\n\n";
    }

    ss << "        $display(\"=== Test Complete ===\");\n";
    ss << "        $finish;\n";
    ss << "    end\n\n";

    ss << "endmodule\n";

    emitting_testbench_ = false;
    return ss.str();
}

// #[test] からの代入先を検証する。
// 駆動できるのはDUTの入力ポートのみ（出力ポート・内部信号への代入は
// 意図しない多重駆動や不正な階層代入になるため明確なエラーで停止する。
// 内部信号の「読み取り」は dut. 階層参照として対応済み）
void SVCodeGen::validateTestbenchAssignTarget(const hir::HirExpr& lhs) {
    // 配列アクセス等はルートの識別子まで辿る
    const hir::HirExpr* root = &lhs;
    while (true) {
        if (auto* idx = std::get_if<std::unique_ptr<hir::HirIndex>>(&root->kind)) {
            if (*idx && (*idx)->object) {
                root = (*idx)->object.get();
                continue;
            }
        }
        break;
    }
    auto* var = std::get_if<std::unique_ptr<hir::HirVarRef>>(&root->kind);
    if (!var || !*var) {
        return;
    }
    const std::string& nm = (*var)->name;
    if (tb_input_names_.count(nm) > 0) {
        return;  // 入力ポート: OK
    }
    if (tb_port_names_.count(nm) > 0) {
        throw std::runtime_error("#[test] からDUTの出力ポート '" + nm +
                                 "' へは代入できません（読み取りのみ可能です）");
    }
    if (module_signal_names_.count(nm) > 0) {
        throw std::runtime_error("#[test] からDUT内部信号 '" + nm +
                                 "' へは代入できません（assertでの読み取りは可能です）。"
                                 "#[input] ポート経由で駆動してください");
    }
}

// #[test] 関数のHIR文をテストベンチのinitial文へ変換する。
// 対応: 代入（DUT入力の駆動）/ step(n)（nクロック待機）/
// assert(cond, msg)（PASS/FAIL表示・失敗時$fatal）/ println（$display）
std::string SVCodeGen::emitTestbenchStmt(const hir::HirStmt& stmt) {
    const std::string ind = "        ";

    // 式文（代入・step・assert・println）
    if (auto* expr_stmt = std::get_if<std::unique_ptr<hir::HirExprStmt>>(&stmt.kind)) {
        if (!*expr_stmt || !(*expr_stmt)->expr) {
            return "";
        }
        const hir::HirExpr& e = *(*expr_stmt)->expr;

        // 呼び出し
        if (auto* call = std::get_if<std::unique_ptr<hir::HirCall>>(&e.kind)) {
            const auto& c = **call;
            if (c.func_name == "step") {
                std::string n = c.args.empty() ? "1" : emitHirExpr(*c.args[0]);
                const std::string& ck = tb_clk_name_.empty() ? "clk" : tb_clk_name_;
                return ind + "repeat (" + n + ") @(posedge " + ck + ");\n" + ind +
                       "#1; // NBA確定待ち\n";
            }
            if (c.func_name == "assert") {
                std::string cond = c.args.empty() ? "1" : emitHirExpr(*c.args[0]);
                std::string msg = "assertion";
                if (c.args.size() > 1) {
                    if (auto* lit =
                            std::get_if<std::unique_ptr<hir::HirLiteral>>(&c.args[1]->kind)) {
                        if (auto* sv = std::get_if<std::string>(&(*lit)->value)) {
                            msg = *sv;
                        }
                    }
                }
                std::string out;
                out += ind + "if (!(" + cond + ")) begin\n";
                out += ind + "    $display(\"FAIL: " + msg + "\");\n";
                out += ind + "    $fatal(1);\n";
                out += ind + "end else begin\n";
                out += ind + "    $display(\"PASS: " + msg + "\");\n";
                out += ind + "end\n";
                return out;
            }
            if (c.func_name == "println" || c.func_name == "__println__" ||
                c.func_name == "print") {
                std::string msg;
                if (!c.args.empty()) {
                    if (auto* lit =
                            std::get_if<std::unique_ptr<hir::HirLiteral>>(&c.args[0]->kind)) {
                        if (auto* sv = std::get_if<std::string>(&(*lit)->value)) {
                            msg = *sv;
                        }
                    }
                }
                return ind + "$display(\"" + msg + "\");\n";
            }
            // その他の呼び出しは非対応（静かに握り潰さず明示エラーにする）
            throw std::runtime_error(
                "エラー[SV007]: #[test] 関数内で非対応の呼び出しです: " + c.func_name +
                "（使用できるのは step / assert / println と入力ポートへの代入です）");
        }

        // 代入式（HirBinary Assign）: ブロッキング代入でDUT入力を駆動
        if (auto* bin = std::get_if<std::unique_ptr<hir::HirBinary>>(&e.kind)) {
            if (*bin && (*bin)->op == hir::HirBinaryOp::Assign && (*bin)->lhs && (*bin)->rhs) {
                validateTestbenchAssignTarget(*(*bin)->lhs);
                return ind + emitHirExpr(*(*bin)->lhs) + " = " + emitHirExpr(*(*bin)->rhs) + ";\n";
            }
        }

        return ind + emitHirExpr(e) + ";\n";
    }

    // 単純代入文
    if (auto* assign = std::get_if<std::unique_ptr<hir::HirAssign>>(&stmt.kind)) {
        if (*assign && (*assign)->target && (*assign)->value) {
            validateTestbenchAssignTarget(*(*assign)->target);
            return ind + emitHirExpr(*(*assign)->target) + " = " + emitHirExpr(*(*assign)->value) +
                   ";\n";
        }
    }

    // その他はベストエフォートで既存エミッタへ
    std::string sv_stmt = emitHirStmt(stmt);
    if (!sv_stmt.empty()) {
        return ind + sv_stmt + "\n";
    }
    return "";
}

// HIR式をSVに変換（assign文の非定数式用）
std::string SVCodeGen::emitHirExpr(const hir::HirExpr& expr) {
    // リテラル
    if (auto* lit = std::get_if<std::unique_ptr<hir::HirLiteral>>(&expr.kind)) {
        if (*lit) {
            const auto& value = (*lit)->value;
            if (std::holds_alternative<int64_t>(value)) {
                return std::to_string(std::get<int64_t>(value));
            } else if (std::holds_alternative<double>(value)) {
                return std::to_string(std::get<double>(value));
            } else if (std::holds_alternative<bool>(value)) {
                return std::get<bool>(value) ? "1'b1" : "1'b0";
            } else if (std::holds_alternative<char>(value)) {
                return std::to_string(static_cast<int64_t>(std::get<char>(value)));
            } else if (std::holds_alternative<std::string>(value)) {
                return "\"" + std::get<std::string>(value) + "\"";
            }
        }
    }
    // 配列リテラル
    if (auto* arr = std::get_if<std::unique_ptr<hir::HirArrayLiteral>>(&expr.kind)) {
        if (*arr) {
            std::string res = "'{";
            for (size_t i = 0; i < (*arr)->elements.size(); ++i) {
                if (i > 0)
                    res += ", ";
                res += emitHirExpr(*(*arr)->elements[i]);
            }
            res += "}";
            return res;
        }
    }

    // 識別子（変数参照）
    if (auto* var = std::get_if<std::unique_ptr<hir::HirVarRef>>(&expr.kind)) {
        if (*var) {
            const std::string& nm = (*var)->name;
            // テストベンチ生成中: ポート以外のDUT内部信号（内部レジスタ等）は
            // 階層参照（dut.名前）として読み取る
            if (emitting_testbench_ && tb_port_names_.count(nm) == 0 &&
                module_signal_names_.count(nm) > 0) {
                return "dut." + nm;
            }
            return nm;
        }
    }

    // 二項演算
    if (auto* binary = std::get_if<std::unique_ptr<hir::HirBinary>>(&expr.kind)) {
        if (*binary && (*binary)->lhs && (*binary)->rhs) {
            std::string lhs = emitHirExpr(*(*binary)->lhs);
            std::string rhs = emitHirExpr(*(*binary)->rhs);
            std::string op;
            switch ((*binary)->op) {
                case hir::HirBinaryOp::Add:
                    op = "+";
                    break;
                case hir::HirBinaryOp::Sub:
                    op = "-";
                    break;
                case hir::HirBinaryOp::Mul:
                    op = "*";
                    break;
                case hir::HirBinaryOp::Div:
                    op = "/";
                    break;
                case hir::HirBinaryOp::Mod:
                    op = "%";
                    break;
                case hir::HirBinaryOp::BitAnd:
                    op = "&";
                    break;
                case hir::HirBinaryOp::BitOr:
                    op = "|";
                    break;
                case hir::HirBinaryOp::BitXor:
                    op = "^";
                    break;
                case hir::HirBinaryOp::Shl:
                    op = "<<";
                    break;
                case hir::HirBinaryOp::Shr:
                    op = ">>";
                    break;
                case hir::HirBinaryOp::And:
                    op = "&&";
                    break;
                case hir::HirBinaryOp::Or:
                    op = "||";
                    break;
                case hir::HirBinaryOp::Eq:
                    op = "==";
                    break;
                case hir::HirBinaryOp::Ne:
                    op = "!=";
                    break;
                case hir::HirBinaryOp::Lt:
                    op = "<";
                    break;
                case hir::HirBinaryOp::Le:
                    op = "<=";
                    break;
                case hir::HirBinaryOp::Gt:
                    op = ">";
                    break;
                case hir::HirBinaryOp::Ge:
                    op = ">=";
                    break;
                case hir::HirBinaryOp::Assign:
                    // 代入式: SVでは式として括弧に包めないため
                    // 素の代入形式で返す（式文経由で "lhs = rhs;" になる）
                    return lhs + " = " + rhs;
                default:
                    op = "?";
                    break;
            }
            return "(" + lhs + " " + op + " " + rhs + ")";
        }
    }

    // 単項演算
    if (auto* unary = std::get_if<std::unique_ptr<hir::HirUnary>>(&expr.kind)) {
        if (*unary && (*unary)->operand) {
            std::string operand = emitHirExpr(*(*unary)->operand);
            std::string op;
            switch ((*unary)->op) {
                case hir::HirUnaryOp::Neg:
                    op = "-";
                    break;
                case hir::HirUnaryOp::Not:
                    op = "!";
                    break;
                case hir::HirUnaryOp::BitNot:
                    op = "~";
                    break;
                default:
                    op = "?";
                    break;
            }
            return op + operand;
        }
    }

    // メンバアクセス
    if (auto* member = std::get_if<std::unique_ptr<hir::HirMember>>(&expr.kind)) {
        if (*member && (*member)->object) {
            std::string obj = emitHirExpr(*(*member)->object);
            return obj + "." + (*member)->member;
        }
    }

    // 三項演算子
    if (auto* ternary = std::get_if<std::unique_ptr<hir::HirTernary>>(&expr.kind)) {
        if (*ternary && (*ternary)->condition && (*ternary)->then_expr && (*ternary)->else_expr) {
            std::string cond = emitHirExpr(*(*ternary)->condition);
            std::string then_e = emitHirExpr(*(*ternary)->then_expr);
            std::string else_e = emitHirExpr(*(*ternary)->else_expr);
            return "(" + cond + " ? " + then_e + " : " + else_e + ")";
        }
    }

    // キャスト
    if (auto* cast = std::get_if<std::unique_ptr<hir::HirCast>>(&expr.kind)) {
        if (*cast && (*cast)->operand) {
            return emitHirExpr(*(*cast)->operand);
        }
    }

    // 未対応の式: 0への静かな縮退は回路の意味を変えるため明示エラーにする（SV007）
    {
        std::string ctx = emitting_testbench_ ? "#[test] 関数内で非対応の式です"
                                              : "initialブロックで非対応のHIR式です";
        throw std::runtime_error("エラー[SV007]: " + ctx +
                                 "（variant index=" + std::to_string(expr.kind.index()) + "）");
    }
}

// HIR文をSVに変換（initial block用）
std::string SVCodeGen::emitHirStmt(const hir::HirStmt& stmt) {
    // 代入文
    if (auto* assign = std::get_if<std::unique_ptr<hir::HirAssign>>(&stmt.kind)) {
        if (*assign && (*assign)->target && (*assign)->value) {
            std::string lhs = emitHirExpr(*(*assign)->target);
            std::string rhs = emitHirExpr(*(*assign)->value);
            return lhs + " = " + rhs + ";";
        }
    }

    // 変数宣言（let文）
    if (auto* let = std::get_if<std::unique_ptr<hir::HirLet>>(&stmt.kind)) {
        if (*let) {
            std::string sv_type = mapType((*let)->type);
            std::string init_val = (*let)->init ? emitHirExpr(*(*let)->init) : "0";
            return sv_type + " " + (*let)->name + " = " + init_val + ";";
        }
    }

    // 式文
    if (auto* expr_stmt = std::get_if<std::unique_ptr<hir::HirExprStmt>>(&stmt.kind)) {
        if (*expr_stmt && (*expr_stmt)->expr) {
            return emitHirExpr(*(*expr_stmt)->expr) + ";";
        }
    }

    // ブロック文
    if (auto* block = std::get_if<std::unique_ptr<hir::HirBlock>>(&stmt.kind)) {
        if (*block) {
            std::ostringstream ss;
            ss << "begin\n";
            for (const auto& s : (*block)->stmts) {
                if (s) {
                    std::string sv_stmt = emitHirStmt(*s);
                    if (!sv_stmt.empty()) {
                        ss << "    " << sv_stmt << "\n";
                    }
                }
            }
            ss << "end";
            return ss.str();
        }
    }

    // if文
    if (auto* if_stmt = std::get_if<std::unique_ptr<hir::HirIf>>(&stmt.kind)) {
        if (*if_stmt && (*if_stmt)->cond) {
            std::ostringstream ss;
            std::string cond = emitHirExpr(*(*if_stmt)->cond);
            ss << "if (" << cond << ") begin\n";
            for (const auto& s : (*if_stmt)->then_block) {
                if (s) {
                    std::string sv_stmt = emitHirStmt(*s);
                    if (!sv_stmt.empty()) {
                        ss << "    " << sv_stmt << "\n";
                    }
                }
            }
            ss << "end";
            if (!(*if_stmt)->else_block.empty()) {
                ss << " else begin\n";
                for (const auto& s : (*if_stmt)->else_block) {
                    if (s) {
                        std::string sv_stmt = emitHirStmt(*s);
                        if (!sv_stmt.empty()) {
                            ss << "    " << sv_stmt << "\n";
                        }
                    }
                }
                ss << "end";
            }
            return ss.str();
        }
    }

    // 未対応の文: 静かなコメント化は検証漏れの温床になるため明示エラーにする（SV007）
    {
        std::string ctx = emitting_testbench_ ? "#[test] 関数内で非対応の文です"
                                                "（step/assert/代入/println/if等が使用できます）"
                                              : "initialブロックで非対応のHIR文です";
        throw std::runtime_error("エラー[SV007]: " + ctx +
                                 "（variant index=" + std::to_string(stmt.kind.index()) + "）");
    }
}
}  // namespace cm::codegen::sv
