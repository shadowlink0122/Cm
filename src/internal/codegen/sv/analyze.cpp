// ============================================================
// MIR解析 - モジュール情報の抽出とalwaysブロック組み立て（各フェーズの実体はanalyze/配下に分割）
// ============================================================
#include "codegen.hpp"

#include <set>
#include <string>

namespace cm::codegen::sv {

// MIRからSVモジュール情報を抽出する（analyze/配下の各フェーズ関数を逐次呼び出すオーケストレータ）
void SVCodeGen::analyzeMIR(const mir::MirProgram& program) {
    // 事前収集: #[sv::parameter]名・文字列定数長・モジュールスコープ信号名
    analyzeCollectGlobals(program);

    SVModule default_mod;
    default_mod.name = "top";

    // モジュール名の決定: `module NAME;` 宣言 > ソースファイル名 > 出力ファイル名
    analyzeModuleName(default_mod);

    // 事前パス: IOインスタンス写像・インスタンス駆動信号・配列信号名の収集
    std::set<std::string> instance_driven_signals;
    std::set<std::string> array_signal_names;
    analyzePrepassInstances(program, instance_driven_signals, array_signal_names);

    // グローバル変数からポートと内部シグナルを生成
    bool has_clk = false;
    bool has_rst = false;
    analyzeGlobalPorts(program, default_mod, instance_driven_signals, array_signal_names, has_clk,
                       has_rst);

    // クロック信号の解決（不足クロック入力ポートの補完・暗黙clk/rstの自動追加）
    analyzeClockPorts(program, default_mod, has_clk, has_rst);

    // 関数解析ループ・enum/struct typedef・initialブロックの出力
    analyzeDeclarations(program, default_mod);

    modules_.push_back(default_mod);
}

}  // namespace cm::codegen::sv
