// constraints.cpp - 物理制約ファイル（Gowin .cst / .tcl）の生成
// #[sv::pin("U12", io_type: "LVCMOS33", drive: 8)] 属性と
// //! sv: device: / //! sv: option: ディレクティブから、
// --emit-constraints 指定時にピン制約とプロジェクトスクリプトを生成する。
// 設計: docs/design/v0.16.0/02_constraints_emission.md

#include "codegen.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace cm::codegen::sv {

namespace {

// sv::pin属性の解析結果
struct PinInfo {
    std::string port_name;
    std::string pin_loc;  // 物理ピン（必須・第1引数）
    std::vector<std::pair<std::string, std::string>> params;  // IO_PORT用 key=value
};

// 属性文字列 sv::pin("U12", "io_type:LVCMOS33", "drive:8") から引数列を取り出す
std::vector<std::string> parse_attr_args(const std::string& attr, const std::string& name) {
    std::vector<std::string> args;
    const std::string prefix = name + "(";
    if (attr.rfind(prefix, 0) != 0 || attr.back() != ')') {
        return args;
    }
    std::string body = attr.substr(prefix.size(), attr.size() - prefix.size() - 1);
    // "..." をカンマで分割（引数値にカンマは想定しない）
    std::string cur;
    bool in_str = false;
    for (char c : body) {
        if (c == '"') {
            in_str = !in_str;
            continue;
        }
        if (c == ',' && !in_str) {
            if (!cur.empty()) {
                args.push_back(cur);
            }
            cur.clear();
            continue;
        }
        if (c == ' ' && !in_str && cur.empty()) {
            continue;  // 区切り後の空白
        }
        cur += c;
    }
    if (!cur.empty()) {
        args.push_back(cur);
    }
    return args;
}

// key:value 引数のキーをGowin .cst のIO_PORT属性名へ写像する。
// 既知キーは正式名、未知キーは大文字化してそのまま転記（ツール固有属性に開かれた設計）
std::string map_io_key(const std::string& key) {
    if (key == "io_type") {
        return "IO_TYPE";
    }
    if (key == "drive") {
        return "DRIVE";
    }
    if (key == "pull") {
        return "PULL_MODE";
    }
    if (key == "slew") {
        return "SLEW_RATE";
    }
    std::string upper = key;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return upper;
}

// グローバル変数がポート宣言（input/output/inout属性付き）かどうか
bool is_port(const mir::MirGlobalVar& gv) {
    for (const auto& attr : gv.attributes) {
        if (attr == "input" || attr == "output" || attr == "inout") {
            return true;
        }
    }
    return false;
}

}  // namespace

// #[sv::pin] 属性からピン情報を収集する
std::vector<SVCodeGen::CollectedPin> SVCodeGen::collectPins(const mir::MirProgram& program) {
    std::vector<CollectedPin> pins;
    for (const auto& gv : program.global_vars) {
        if (!gv) {
            continue;
        }
        PinInfo info;
        info.port_name = gv->name;
        for (const auto& attr : gv->attributes) {
            for (const char* attr_name : {"sv::pin", "verilog::pin"}) {
                auto args = parse_attr_args(attr, attr_name);
                if (args.empty()) {
                    continue;
                }
                info.pin_loc = args[0];
                for (size_t i = 1; i < args.size(); ++i) {
                    auto colon = args[i].find(':');
                    if (colon != std::string::npos) {
                        info.params.emplace_back(map_io_key(args[i].substr(0, colon)),
                                                 args[i].substr(colon + 1));
                    }
                }
            }
            // 旧形式 sv::iostandard("...") との互換
            for (const char* io_name : {"sv::iostandard", "verilog::iostandard"}) {
                auto args = parse_attr_args(attr, io_name);
                if (!args.empty()) {
                    info.params.emplace_back("IO_TYPE", args[0]);
                }
            }
        }
        if (!info.pin_loc.empty()) {
            pins.push_back({info.port_name, info.pin_loc, info.params});
        } else if (is_port(*gv) && options_.emitConstraints) {
            // 割り当て漏れの検出（clk等の暗黙ポートも対象）
            std::cerr << "警告: ポート '" << gv->name
                      << "' に #[sv::pin] が指定されていません（.cstに含まれません）\n";
        }
    }
    return pins;
}

// Gowin .cst（物理制約）の生成
std::string SVCodeGen::generateCST(const mir::MirProgram& program) {
    auto pins = collectPins(program);
    if (pins.empty()) {
        return "";
    }
    std::ostringstream ss;
    ss << "// "
       << "自動生成: cm --target=sv --emit-constraints（手編集する場合は"
       << "生成元の #[sv::pin] 属性と同期を保つこと）\n\n";
    for (const auto& pin : pins) {
        ss << "IO_LOC  \"" << pin.port_name << "\" " << pin.pin_loc << ";\n";
        if (!pin.params.empty()) {
            ss << "IO_PORT \"" << pin.port_name << "\"";
            for (const auto& [key, val] : pin.params) {
                ss << " " << key << "=" << val;
            }
            ss << ";\n";
        }
        ss << "\n";
    }
    return ss.str();
}

// Gowin プロジェクトスクリプト（.tcl）の生成
std::string SVCodeGen::generateProjectTCL(const std::string& module_name,
                                          const std::string& sv_path, const std::string& cst_path) {
    if (options_.devicePN.empty()) {
        return "";
    }
    namespace fs = std::filesystem;
    std::ostringstream ss;
    ss << "# 自動生成: cm --target=sv --emit-constraints\n";
    ss << "# 使い方: gw_sh " << fs::path(sv_path).stem().string() << "_build.tcl\n\n";
    ss << "set device_pn \"" << options_.devicePN << "\"\n";
    if (!options_.deviceVersion.empty()) {
        ss << "set device_version \"" << options_.deviceVersion << "\"\n";
    }
    ss << "\ncreate_project -name " << module_name << " -dir . -pn $device_pn";
    if (!options_.deviceVersion.empty()) {
        ss << " -device_version $device_version";
    }
    ss << " -force\n\n";
    // パスに空白等が含まれてもTclが正しく解釈できるようbracesで囲む
    ss << "add_file {" << fs::absolute(sv_path).string() << "}\n";
    if (!cst_path.empty()) {
        ss << "add_file {" << fs::absolute(cst_path).string() << "}\n";
    }
    ss << "\nset_option -verilog_std sysv2017\n";
    ss << "set_option -top_module " << module_name << "\n";
    ss << "set_option -output_base_name " << module_name << "\n";
    for (const auto& opt : options_.toolOptions) {
        ss << "set_option -" << opt << " 1\n";
    }
    ss << "\nrun all\n";
    return ss.str();
}

// //! sv: device: / //! sv: option: ディレクティブをソースから抽出する
SvProjectDirectives parse_sv_project_directives(const std::string& source) {
    SvProjectDirectives result;
    std::istringstream iss(source);
    std::string line;
    while (std::getline(iss, line)) {
        auto pos = line.find("//!");
        if (pos == std::string::npos) {
            continue;
        }
        std::string rest = line.substr(pos + 3);
        // 前後空白を除去して "sv:" プレフィックスを確認
        auto trim = [](std::string s) {
            size_t b = s.find_first_not_of(" \t");
            if (b == std::string::npos) {
                return std::string();
            }
            size_t e = s.find_last_not_of(" \t\r");
            return s.substr(b, e - b + 1);
        };
        rest = trim(rest);
        if (rest.rfind("sv:", 0) != 0) {
            continue;
        }
        rest = trim(rest.substr(3));
        if (rest.rfind("device:", 0) == 0) {
            std::string val = trim(rest.substr(7));
            // "型番 [版]" の形式（版は任意）
            auto sp = val.find(' ');
            if (sp != std::string::npos) {
                result.device_pn = trim(val.substr(0, sp));
                result.device_version = trim(val.substr(sp + 1));
            } else {
                result.device_pn = val;
            }
        } else if (rest.rfind("option:", 0) == 0) {
            std::string val = trim(rest.substr(7));
            if (!val.empty()) {
                result.tool_options.push_back(val);
            }
        }
    }
    return result;
}

}  // namespace cm::codegen::sv
