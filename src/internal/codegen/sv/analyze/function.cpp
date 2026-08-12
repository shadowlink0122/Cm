// ============================================================
// MIR解析 - 関数からのalways_ff/always_comb/functionブロック生成
// ============================================================
#include "internal/base/i18n.hpp"
#include "internal/codegen/sv/codegen.hpp"
#include "internal/codegen/sv/internal.hpp"

#include <cctype>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cm::codegen::sv {

void SVCodeGen::analyzeFunction(const mir::MirFunction& func, SVModule& mod) {
    // main関数はスキップ（ハードウェアにmainはない）
    if (func.name == "main")
        return;

    // std::debug::assert はイントリンシック（呼び出し箇所で即時アサーションに展開）。
    // 定義本体は出力しない（assertはSVの予約語でもある）
    if (func.name == "assert" || func.name == "panic")
        return;

    // #[test] 関数はテストベンチ生成専用（モジュール本体へは出力しない）
    for (const auto& attr : func.attributes) {
        if (attr == "test") {
            testbench_fns_.push_back(&func);
            return;
        }
    }

    // 非always/非async関数で、非void（戻り値あり）の場合 → SV function automatic void関数は always_comb / always_ff にフォールスルー
    if (!func.is_always && !func.is_async &&
        func.always_kind == mir::MirFunction::AlwaysKind::None) {
        // edgeパラメータの有無を確認
        bool has_edge_param = false;
        for (auto arg_id : func.arg_locals) {
            if (arg_id < func.locals.size()) {
                auto& local = func.locals[arg_id];
                if (local.type && (local.type->kind == hir::TypeKind::Posedge ||
                                   local.type->kind == hir::TypeKind::Negedge)) {
                    has_edge_param = true;
                }
            }
        }

        // 非void関数（戻り値あり）→ SV function automatic
        bool is_void = true;
        std::string ret_type_str = "void";
        if (func.return_local < func.locals.size()) {
            auto& ret_local = func.locals[func.return_local];
            if (ret_local.type && ret_local.type->kind != hir::TypeKind::Void) {
                is_void = false;
                ret_type_str = mapType(ret_local.type);
            }
        }

        if (!is_void && !has_edge_param) {
            std::ostringstream fn_ss;
            indent_level_ = 1;

            // 関数名のnamespace::フラット化（import時の alu_lib::add → add）
            std::string flat_func_name = func.name;
            flat_func_name = strip_namespace(flat_func_name);

            if (flat_func_name == "stringToUint") {
                std::ostringstream fn_ss;
                fn_ss
                    << "    function automatic logic [31:0] stringToUint(input logic [23:0] s);\n";
                fn_ss << "        return {8'd0, s};\n";
                fn_ss << "    endfunction\n";
                mod.function_blocks.push_back(fn_ss.str());
                return;
            }

            // 引数リスト構築（posedge/negedge型を除外）
            std::vector<std::string> args;
            std::set<std::string> arg_names;  // 引数名の重複チェック用
            for (auto arg_id : func.arg_locals) {
                if (arg_id < func.locals.size()) {
                    auto& local = func.locals[arg_id];
                    if (local.type && (local.type->kind == hir::TypeKind::Posedge ||
                                       local.type->kind == hir::TypeKind::Negedge))
                        continue;
                    args.push_back("input " + mapType(local.type) + " " + local.name);
                    arg_names.insert(local.name);
                }
            }

            fn_ss << indent() << "function automatic " << ret_type_str << " " << flat_func_name
                  << "(";
            for (size_t i = 0; i < args.size(); ++i) {
                if (i > 0)
                    fn_ss << ", ";
                fn_ss << args[i];
            }
            fn_ss << ");\n";

            // ローカル変数宣言（引数と戻り値を除く、テンポラリ変数は後で除去）
            increaseIndent();
            std::set<mir::LocalId> arg_set(func.arg_locals.begin(), func.arg_locals.end());
            // 一旦全ローカル変数を記録（テンポラリは後でスキップ判定）
            std::vector<std::pair<size_t, std::string>> local_decls;
            for (size_t i = 0; i < func.locals.size(); ++i) {
                if (i == func.return_local)
                    continue;
                if (arg_set.count(static_cast<mir::LocalId>(i)))
                    continue;
                auto& local = func.locals[i];
                if (local.name.empty() || local.name.find('@') != std::string::npos)
                    continue;
                // import/export時にグローバル定数がローカルとして混入するのを防止
                if (local.is_global)
                    continue;
                // 引数と同名のローカル変数はスキップ（関数引数の重複宣言防止）
                if (arg_names.count(local.name))
                    continue;
                // ポインタ型テンポラリはスキップ
                if (local.name.find("_t") == 0 && local.type &&
                    local.type->kind == hir::TypeKind::Pointer)
                    continue;
                local_decls.push_back({i, mapType(local.type) + " " + local.name + ";"});
            }

            // 関数本体 — テンポラリ変数のインライン展開
            std::string body_content;
            if (!func.basic_blocks.empty() && func.basic_blocks[0]) {
                // ループヘッダ情報とテンポラリ情報を関数ごとに1回だけ計算する
                collectSingleDefTemps(func);
                current_loop_latches_ = compute_loop_latches(func);
                computeForLoops(func);
                std::set<size_t> visited;
                std::ostringstream body_ss;
                emitBlockRecursive(func, 0, visited, body_ss);
                std::string raw_body = body_ss.str();

                // @return → 関数名 に置換（フラット化済み名前を使用）
                size_t pos = 0;
                while ((pos = raw_body.find("@return", pos)) != std::string::npos) {
                    raw_body.replace(pos, 7, flat_func_name);
                    pos += flat_func_name.size();
                }

                // 式ツリー化（Phase 2）により単一定義テンポラリは出力時に構造的へインライン展開済み。
                // テキストベースの再展開パスは不要になった
                body_content = raw_body;

                // 本体で使用されなくなったテンポラリ宣言を除去
                auto is_word_used = [&](const std::string& name) {
                    size_t p = 0;
                    while ((p = body_content.find(name, p)) != std::string::npos) {
                        bool at_start = (p == 0 || (!std::isalnum(body_content[p - 1]) &&
                                                    body_content[p - 1] != '_'));
                        size_t after = p + name.size();
                        bool at_end =
                            (after >= body_content.size() ||
                             (!std::isalnum(body_content[after]) && body_content[after] != '_'));
                        if (at_start && at_end) {
                            return true;
                        }
                        p += name.size();
                    }
                    return false;
                };
                auto decl_it = local_decls.begin();
                while (decl_it != local_decls.end()) {
                    auto& local = func.locals[decl_it->first];
                    if (local.name.size() > 2 && local.name[0] == '_' && local.name[1] == 't' &&
                        std::isdigit(local.name[2]) && !is_word_used(local.name)) {
                        decl_it = local_decls.erase(decl_it);
                    } else {
                        ++decl_it;
                    }
                }
            }

            // ローカル変数宣言を出力
            for (const auto& decl : local_decls) {
                fn_ss << indent() << decl.second << "\n";
            }

            // 展開済みの関数本体を出力
            fn_ss << body_content;

            decreaseIndent();
            fn_ss << indent() << "endfunction\n";

            mod.function_blocks.push_back(fn_ss.str());
            return;
        }  // if (!is_void && !has_edge_param)
    }

    // ローカル変数・一時変数をalwaysブロック内ローカルとして宣言する候補を収集（モジュールスコープへのホイストをやめ、スコープ汚染とfunction内ローカルとの名前衝突（VARHIDDEN）を防ぐ。
    //   ポートやモジュール信号と名前が衝突する変数は従来どおり宣言しない＝
    //   モジュールスコープの実体を参照する）
    std::vector<std::pair<std::string, std::string>> block_local_decls;  // {名前, 宣言文}
    std::set<std::string> port_names;
    for (const auto& port : mod.ports) {
        port_names.insert(port.name);
    }
    for (const auto& local : func.locals) {
        std::string name = local.name;
        if (name.empty() || name == "_0")
            continue;  // 戻り値用
        // モジュール信号への参照はローカルではない
        if (local.is_global)
            continue;
        // posedge/negedge型パラメータはセンシティビティ指定であり変数ではない
        if (local.type && (local.type->kind == hir::TypeKind::Posedge ||
                           local.type->kind == hir::TypeKind::Negedge))
            continue;
        // 不正なSV識別子をスキップ（@return等）
        if (name.find('@') != std::string::npos)
            continue;
        // self.プレフィックスを除去
        if (name.find("self.") == 0)
            name = name.substr(5);
        // ポートと名前が衝突する場合はスキップ
        if (port_names.count(name))
            continue;
        // extern struct インスタンスと同名の変数はスキップ
        bool is_instance_var = false;
        for (const auto& inst : mod.instance_blocks) {
            if (inst.find(" " + name + " ") != std::string::npos ||
                inst.find(" " + name + ";") != std::string::npos) {
                is_instance_var = true;
                break;
            }
        }
        if (is_instance_var)
            continue;
        // parameter宣言と名前が衝突する場合はスキップ
        bool is_param_var = false;
        for (const auto& param : mod.parameters) {
            if (param.find(" " + name + " ") != std::string::npos ||
                param.find(" " + name + ";") != std::string::npos) {
                is_param_var = true;
                break;
            }
        }
        // モジュールパラメータ（#[sv::parameter]）とも衝突チェック
        if (!is_param_var && sv_param_names_.count(name) > 0) {
            is_param_var = true;
        }
        if (is_param_var)
            continue;
        // 既に登録済みの宣言もスキップ（変数名の部分一致で検出）
        std::string decl = mapType(local.type) + " " + name + getArraySuffix(local.type) + ";";
        bool already_declared = false;
        for (const auto& existing : mod.reg_declarations) {
            // 完全一致またはBRAM/LutRAM属性付き宣言で同名変数がある場合もスキップ
            if (existing == decl || existing.find(" " + name + " ") != std::string::npos ||
                existing.find(" " + name + ";") != std::string::npos) {
                already_declared = true;
                break;
            }
        }
        if (!already_declared) {
            for (const auto& existing : mod.wire_declarations) {
                if (existing.find(" " + name + " ") != std::string::npos ||
                    existing.find(" " + name + ";") != std::string::npos) {
                    already_declared = true;
                    break;
                }
            }
        }
        if (!already_declared) {
            bool dup_candidate = false;
            for (const auto& c : block_local_decls) {
                if (c.first == name) {
                    dup_candidate = true;
                    break;
                }
            }
            if (!dup_candidate) {
                block_local_decls.push_back({name, decl});
            }
        }
    }

    std::ostringstream block_ss;

    // モジュール内のインデントレベルを設定
    indent_level_ = 1;

    // 関数名コメントを追加（namespace::プレフィックスをフラット化）
    std::string display_name = func.name;
    display_name = strip_namespace(display_name);
    block_ss << indent() << "// " << display_name << "\n";

    // SV固有型: posedge/negedge型パラメータの検出
    std::string edge_type;   // "posedge" or "negedge"
    std::string edge_clock;  // クロック信号名
    bool has_explicit_edge = false;

    // 複数エッジ: 非同期リセット用 (always void f(posedge clk, negedge rst_n))
    std::vector<std::pair<std::string, std::string>> all_edges;  // {edge_type, signal_name}

    for (const auto& local : func.locals) {
        if (local.is_global)
            continue;
        if (local.type && local.type->kind == hir::TypeKind::Posedge) {
            // 重複排除: 同名信号が既にある場合はスキップ
            bool dup = false;
            for (const auto& e : all_edges) {
                if (e.second == local.name) {
                    dup = true;
                    break;
                }
            }
            if (!dup) {
                if (!has_explicit_edge) {
                    edge_type = "posedge";
                    edge_clock = local.name;
                    has_explicit_edge = true;
                    // テストベンチのクロック検出用にプロセスクロック名を記録
                    process_clock_names_.insert(local.name);
                }
                all_edges.push_back({"posedge", local.name});
            }
        }
        if (local.type && local.type->kind == hir::TypeKind::Negedge) {
            // 重複排除: 同名信号が既にある場合はスキップ
            bool dup = false;
            for (const auto& e : all_edges) {
                if (e.second == local.name) {
                    dup = true;
                    break;
                }
            }
            if (!dup) {
                if (!has_explicit_edge) {
                    edge_type = "negedge";
                    edge_clock = local.name;
                    has_explicit_edge = true;
                }
                all_edges.push_back({"negedge", local.name});
            }
        }
    }

    if (has_explicit_edge) {
        // 明示的なposedge/negedge型パラメータ → always_ff
        if (all_edges.size() > 1) {
            // 複数エッジ: always_ff @(posedge clk or negedge rst_n)
            block_ss << indent() << "always_ff @(";
            for (size_t i = 0; i < all_edges.size(); ++i) {
                if (i > 0)
                    block_ss << " or ";
                block_ss << all_edges[i].first << " " << all_edges[i].second;
            }
            block_ss << ") begin\n";
        } else {
            block_ss << indent() << "always_ff @(" << edge_type << " " << edge_clock << ") begin\n";
        }
    } else if (func.is_always && !has_explicit_edge) {
        // always修飾子 + エッジパラメータなし
        using AK = mir::MirFunction::AlwaysKind;
        if (func.always_kind == AK::Comb) {
            // always_comb 明示指定
            block_ss << indent() << "always_comb begin\n";
        } else if (func.always_kind == AK::Latch) {
            // always_latch 明示指定
            block_ss << indent() << "always_latch begin\n";
        } else if (func.always_kind == AK::FF) {
            // always_ff明示指定なのにエッジパラメータがない（R16: 従来はこの分岐が下のFF専用分岐より
            // 先に評価されて黙ってalways_combへ変換され、順序回路の意図が失われていた）
            throw std::runtime_error(
                i18n::msgf(i18n::MsgId::SvSv008AlwaysFfRequiresEdge, func.name));
        } else {
            // AutoまたはNone: 後でCFG解析で判別（一旦always_combとして出力し後で置換）
            block_ss << indent() << "always_comb begin\n";
        }
    } else if (func.always_kind == mir::MirFunction::AlwaysKind::FF) {
        // always_ff 明示指定（エッジパラメータなし）→ デフォルト posedge clk
        std::string clock_name = "clk";
        for (const auto& attr : func.attributes) {
            std::string prefix1 = "sv::clock_domain(";
            std::string prefix2 = "verilog::clock_domain(";
            if (attr.find(prefix1) == 0 && attr.back() == ')') {
                clock_name = attr.substr(prefix1.size(), attr.size() - prefix1.size() - 1);
            } else if (attr.find(prefix2) == 0 && attr.back() == ')') {
                clock_name = attr.substr(prefix2.size(), attr.size() - prefix2.size() - 1);
            }
        }
        block_ss << indent() << "always_ff @(posedge " << clock_name << ") begin\n";
    } else if (func.is_always || func.is_async) {
        // always修飾子+エッジあり、またはasync修飾子（後方互換）→ always_ff @(posedge clk)
        // Phase 4: マルチクロックドメイン対応
        std::string clock_name = "clk";
        for (const auto& attr : func.attributes) {
            std::string prefix1 = "sv::clock_domain(";
            std::string prefix2 = "verilog::clock_domain(";
            if (attr.find(prefix1) == 0 && attr.back() == ')') {
                clock_name = attr.substr(prefix1.size(), attr.size() - prefix1.size() - 1);
            } else if (attr.find(prefix2) == 0 && attr.back() == ')') {
                clock_name = attr.substr(prefix2.size(), attr.size() - prefix2.size() - 1);
            }
        }

        for (const auto& attr : func.attributes) {
            if (attr.find("sv::pipeline") != std::string::npos ||
                attr.find("verilog::pipeline") != std::string::npos) {
                block_ss << indent() << "// synthesis attribute: " << attr << "\n";
            }
            if (attr.find("sv::share") != std::string::npos ||
                attr.find("verilog::share") != std::string::npos) {
                block_ss << indent() << "// synthesis attribute: resource sharing enabled\n";
            }
        }

        block_ss << indent() << "always_ff @(posedge " << clock_name << ") begin\n";
    } else {
        block_ss << indent() << "always_comb begin\n";
    }

    // ブロック内ローカル宣言のスコープとして名前付きブロックにする（名前付きブロック内の変数宣言はVerilog-2001から全ツールで有効）
    {
        std::string header = block_ss.str();
        const std::string begin_nl = " begin\n";
        if (header.size() >= begin_nl.size() &&
            header.compare(header.size() - begin_nl.size(), begin_nl.size(), begin_nl) == 0) {
            header.replace(header.size() - begin_nl.size(), begin_nl.size(),
                           " begin : " + display_name + "_blk\n");
            block_ss.str("");
            block_ss << header;
        }
    }

    increaseIndent();

    // CFG再帰走査でブロックを構造化出力
    std::ostringstream raw_ss;
    if (!func.basic_blocks.empty() && func.basic_blocks[0]) {
        // ループヘッダ情報とテンポラリ情報を関数ごとに1回だけ計算する
        collectSingleDefTemps(func);
        current_loop_latches_ = compute_loop_latches(func);
        computeForLoops(func);
        std::set<size_t> visited;
        emitBlockRecursive(func, 0, visited, raw_ss);
    }

    // 式ツリー化（Phase 2）により単一定義テンポラリは出力時に構造的へインライン展開済み。テキストベースのインライン展開パス（Pass1/Pass2）は不要になった
    const std::string body_text = raw_ss.str();

    // 本文で実際に使用されるローカル変数のみブロック内に宣言する（単一定義テンポラリは式ツリーへインライン済みのため宣言不要）
    {
        auto used_in_body = [&body_text](const std::string& name) {
            return contains_identifier(body_text, name);
        };
        for (const auto& c : block_local_decls) {
            if (used_in_body(c.first)) {
                block_ss << indent() << c.second << "\n";
            }
        }
    }

    block_ss << body_text;

    decreaseIndent();
    block_ss << indent() << "end\n";

    // 未使用テンポラリ宣言の除去はモジュール出力時に全ブロックを対象に行う（テンポラリ名は関数間で衝突するため、関数単位の除去は誤削除の危険がある）
    std::string block_content = block_ss.str();

    // 三項演算子化は emitTerminator の構造的判定（Phase 2b）で実施済み

    // else if 正規化: "end else begin\n    if (...) begin" → "end else if (...) begin"
    // 結合時にブロック内容のインデントを1レベル浅く調整し、余分なendも除去
    {
        std::istringstream elif_stream(block_content);
        std::vector<std::string> elif_lines;
        std::string elif_line;
        while (std::getline(elif_stream, elif_line)) {
            elif_lines.push_back(elif_line);
        }

        std::ostringstream elif_ss;
        bool first = true;
        // インデント調整量のスタック: 結合されたelse ifの中で4スペース浅くする
        int indent_adjust = 0;
        std::vector<int> adjust_stack;  // begin/endの対応でadjustを追跡

        for (size_t i = 0; i < elif_lines.size(); ++i) {
            auto trim_start = elif_lines[i].find_first_not_of(' ');
            if (trim_start == std::string::npos) {
                if (!first)
                    elif_ss << "\n";
                elif_ss << elif_lines[i];
                first = false;
                continue;
            }
            std::string trimmed = elif_lines[i].substr(trim_start);
            std::string indent_str = elif_lines[i].substr(0, trim_start);

            // "end else begin" + 次行 "if (...)" パターン検出
            if (trimmed == "end else begin" && i + 1 < elif_lines.size()) {
                auto next_trim = elif_lines[i + 1].find_first_not_of(' ');
                if (next_trim != std::string::npos &&
                    elif_lines[i + 1].substr(next_trim, 4) == "if (") {
                    // 結合: "end else if (...) begin"
                    // ネストされた結合（2段目以降）では自身も調整対象のため、現在のindent_adjustを適用して1段浅くする
                    std::string merged_indent = indent_str;
                    if (indent_adjust > 0 && static_cast<int>(trim_start) > indent_adjust) {
                        merged_indent = indent_str.substr(static_cast<size_t>(indent_adjust));
                    }
                    if (!first)
                        elif_ss << "\n";
                    elif_ss << merged_indent << "end else " << elif_lines[i + 1].substr(next_trim);
                    first = false;
                    ++i;  // if行をスキップ
                    // 次行以降のインデントを4スペース浅く調整
                    indent_adjust += 4;
                    // 対応するendを見つけるためにdepthカウンタを初期化
                    adjust_stack.push_back(0);
                    continue;
                }
            }

            // インデント調整中: begin/endの深さを追跡
            if (indent_adjust > 0 && !adjust_stack.empty()) {
                // beginを含む行でdepth++
                if (trimmed.size() >= 5 && trimmed.substr(trimmed.size() - 5) == "begin") {
                    adjust_stack.back()++;
                }
                // "end"で始まる行でdepth--
                if (trimmed == "end" || (trimmed.size() >= 4 && trimmed.substr(0, 4) == "end ")) {
                    if (adjust_stack.back() > 0) {
                        adjust_stack.back()--;
                    } else {
                        // この"end"は余分（結合されたelse ifの対応end）→ スキップ
                        indent_adjust -= 4;
                        adjust_stack.pop_back();
                        continue;
                    }
                }
            }

            // インデント調整を適用
            if (!first)
                elif_ss << "\n";
            if (indent_adjust > 0 && static_cast<int>(trim_start) > indent_adjust) {
                elif_ss << indent_str.substr(indent_adjust) << trimmed;
            } else {
                elif_ss << elif_lines[i];
            }
            first = false;
        }
        block_content = elif_ss.str();
    }

    // 冗長三項（cond ? X : X）は構造的検出が両辺同一時に直接単純代入を出力するため、テキストベースの除去パスは不要になった

    if (has_explicit_edge || func.is_async ||
        func.always_kind == mir::MirFunction::AlwaysKind::FF) {
        mod.always_ff_blocks.push_back(block_content);
    } else {
        using AK = mir::MirFunction::AlwaysKind;
        if (func.always_kind == AK::Latch) {
            // always_latch 明示指定
            mod.always_latch_blocks.push_back(block_content);
        } else if (func.always_kind == AK::Comb) {
            // always_comb 明示指定
            mod.always_comb_blocks.push_back(block_content);
        } else {
            // Auto: MIRの代入完全性解析で判別（式ツリー化 Phase 3）。
            // entryから各returnまでの全制御パスで代入されない
            // モジュールレベル信号があればラッチ推論となる。
            // （従来は「if行数 vs else行数」のテキストヒューリスティックで、if前のデフォルト代入を見落とし、片側代入のif/elseを見逃していた）
            auto incomplete_signals = findIncompletelyAssignedSignals(func);
            if (!incomplete_signals.empty()) {
                // ブロックヘッダを always_latch に置換し、要因の信号を注記する
                size_t pos = block_content.find("always_comb begin");
                if (pos != std::string::npos) {
                    block_content.replace(pos, 17, "always_latch begin");
                }
                std::string note = "    // ラッチ推論: ";
                for (size_t i = 0; i < incomplete_signals.size(); ++i) {
                    if (i > 0) {
                        note += ", ";
                    }
                    note += incomplete_signals[i];
                }
                note += " が全パスで代入されません\n";
                block_content = note + block_content;
                mod.always_latch_blocks.push_back(block_content);
            } else {
                mod.always_comb_blocks.push_back(block_content);
            }
        }
    }

    // インデントレベルをリセット
    indent_level_ = 0;
}

}  // namespace cm::codegen::sv
