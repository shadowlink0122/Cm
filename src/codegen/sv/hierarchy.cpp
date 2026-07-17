#include "hierarchy.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

namespace cm::codegen::sv {

namespace {

// 循環import検出用の環境変数（コロン区切りの絶対パスチェーン）
constexpr const char* kChainEnv = "CM_SV_HIERARCHY_CHAIN";

std::string trim(const std::string& s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

// ソースに `//! sv: hierarchy` ディレクティブが含まれるか
bool has_hierarchy_directive(const std::string& source) {
    std::istringstream ss(source);
    std::string line;
    while (std::getline(ss, line)) {
        std::string t = trim(line);
        if (t.rfind("//!", 0) != 0) {
            continue;
        }
        t = trim(t.substr(3));
        if (t.rfind("sv:", 0) == 0 && trim(t.substr(3)) == "hierarchy") {
            return true;
        }
    }
    return false;
}

// サブモジュールのポート情報
struct PortDecl {
    std::string direction;  // input / output / inout
    std::string type;       // Cm型（posedge/negedgeはboolに正規化済み）
    std::string name;
};

// 1行のポート宣言/IO契約フィールド（`#[input] uint a;` 等）をパースする。
// ポートでなければfalseを返す
bool parse_port_line(const std::string& raw, PortDecl& out) {
    std::string t = trim(raw);
    if (t.rfind("#[", 0) != 0) {
        return false;
    }
    // 先頭の属性群を消費し、方向属性を探す
    std::string direction;
    while (t.rfind("#[", 0) == 0) {
        size_t close = t.find(']');
        if (close == std::string::npos) {
            break;
        }
        std::string attr = trim(t.substr(2, close - 2));
        if (attr == "input" || attr == "output" || attr == "inout") {
            direction = attr;
        }
        t = trim(t.substr(close + 1));
    }
    if (direction.empty() || t.empty()) {
        return false;
    }

    // `assign` 形式（連続代入）はポートとして扱う（型が続く）
    if (t.rfind("assign ", 0) == 0) {
        t = trim(t.substr(7));
    }

    // 型トークン
    std::istringstream toks(t);
    std::string type_tok;
    std::string name_tok;
    if (!(toks >> type_tok)) {
        return false;
    }
    // クロックエッジ型は1bit信号として接続する
    if (type_tok == "posedge" || type_tok == "negedge") {
        type_tok = "bool";
    }
    if (!(toks >> name_tok)) {
        return false;
    }
    // 名前から `= 初期値` / `;` を除去
    for (const char* sep : {"=", ";"}) {
        size_t pos = name_tok.find(sep);
        if (pos != std::string::npos) {
            name_tok = name_tok.substr(0, pos);
        }
    }
    name_tok = trim(name_tok);
    if (name_tok.empty()) {
        return false;
    }
    out = {direction, type_tok, name_tok};
    return true;
}

// サブモジュールのソースからポート宣言を抽出する（行単位の軽量スキャン）。
// 対象: `#[input] uint a;` / `#[output] utiny out = 0;` / `#[input] posedge clk;` 等。
// struct本体内のフィールド（IO契約構造体等）はポートではないためスキップする
std::vector<PortDecl> extract_ports(const std::string& source) {
    std::vector<PortDecl> ports;
    std::istringstream ss(source);
    std::string line;
    int struct_depth = 0;
    while (std::getline(ss, line)) {
        std::string t = trim(line);
        if (struct_depth > 0) {
            for (char c : t) {
                if (c == '{')
                    struct_depth++;
                else if (c == '}')
                    struct_depth--;
            }
            continue;
        }
        std::string decl_check = t;
        if (decl_check.rfind("export ", 0) == 0) {
            decl_check = trim(decl_check.substr(7));
        }
        if (decl_check.rfind("struct ", 0) == 0 || decl_check.rfind("extern struct ", 0) == 0) {
            for (char c : t) {
                if (c == '{')
                    struct_depth++;
                else if (c == '}')
                    struct_depth--;
            }
            continue;
        }
        PortDecl p;
        if (parse_port_line(t, p)) {
            ports.push_back(p);
        }
    }
    return ports;
}

// IOインスタンス（#[input]/#[output]フィールドを持つ構造体のグローバル変数）の
// フィールドをポートとして抽出する。
// 例: `struct AluIo { #[input] uint a; ... }; AluIo io;` → a等がモジュールポート
std::vector<PortDecl> extract_io_instance_ports(const std::string& source) {
    // 1パス目: 構造体定義（名前 → IOフィールド列）を収集する
    std::map<std::string, std::vector<PortDecl>> io_structs;
    {
        std::istringstream ss(source);
        std::string line;
        std::string current_struct;
        while (std::getline(ss, line)) {
            std::string t = trim(line);
            if (current_struct.empty()) {
                if (t.rfind("export ", 0) == 0) {
                    t = trim(t.substr(7));
                }
                if (t.rfind("extern ", 0) == 0) {
                    continue;  // extern structは対象外
                }
                if (t.rfind("struct ", 0) != 0) {
                    continue;
                }
                std::string rest = trim(t.substr(7));
                auto brace = rest.find('{');
                std::string name = trim(brace == std::string::npos ? rest : rest.substr(0, brace));
                if (name.empty()) {
                    continue;
                }
                current_struct = name;
                continue;
            }
            if (!t.empty() && t[0] == '}') {
                current_struct.clear();
                continue;
            }
            PortDecl p;
            if (parse_port_line(t, p)) {
                io_structs[current_struct].push_back(p);
            }
        }
    }
    if (io_structs.empty()) {
        return {};
    }

    // 2パス目: トップレベルのインスタンス宣言（`<構造体名> <変数名>;`）を探し、
    // 宣言順にフィールドをポートとして採用する
    std::vector<PortDecl> ports;
    std::istringstream ss(source);
    std::string line;
    int depth = 0;
    while (std::getline(ss, line)) {
        std::string t = trim(line);
        int line_depth = depth;
        for (char c : t) {
            if (c == '{')
                depth++;
            else if (c == '}')
                depth--;
        }
        if (line_depth != 0) {
            continue;  // 構造体/関数本体内はインスタンス宣言ではない
        }
        std::istringstream toks(t);
        std::string type_tok;
        std::string name_tok;
        if (!(toks >> type_tok)) {
            continue;
        }
        auto it = io_structs.find(type_tok);
        if (it == io_structs.end()) {
            continue;
        }
        if (!(toks >> name_tok)) {
            continue;
        }
        if (name_tok.empty() || name_tok.back() != ';') {
            continue;
        }
        for (const auto& p : it->second) {
            ports.push_back(p);
        }
    }
    return ports;
}

// サブモジュールの #[sv::parameter] const 宣言を抽出する。
// 対象: `#[sv::parameter] const uint WIDTH = 8;` 形式。
// 生成するextern structに #[sv::param] フィールドとして写し、
// インスタンス側から #(.WIDTH(値)) で上書き可能にする（v0.16.0 設計01 P3）
struct ParamDecl {
    std::string type;
    std::string name;
    std::string default_value;
};

std::vector<ParamDecl> extract_parameters(const std::string& source) {
    std::vector<ParamDecl> params;
    std::istringstream ss(source);
    std::string line;
    while (std::getline(ss, line)) {
        std::string t = trim(line);
        if (t.rfind("#[", 0) != 0) {
            continue;
        }
        bool is_param = false;
        while (t.rfind("#[", 0) == 0) {
            size_t close = t.find(']');
            if (close == std::string::npos) {
                break;
            }
            std::string attr = trim(t.substr(2, close - 2));
            if (attr == "sv::parameter" || attr == "verilog::parameter") {
                is_param = true;
            }
            t = trim(t.substr(close + 1));
        }
        if (!is_param) {
            continue;
        }
        if (t.rfind("const ", 0) != 0) {
            continue;
        }
        t = trim(t.substr(6));
        std::istringstream toks(t);
        ParamDecl p;
        if (!(toks >> p.type) || !(toks >> p.name)) {
            continue;
        }
        auto eq = t.find('=');
        if (eq != std::string::npos) {
            std::string val = trim(t.substr(eq + 1));
            auto semi = val.find(';');
            if (semi != std::string::npos) {
                val = trim(val.substr(0, semi));
            }
            p.default_value = val;
        }
        // 名前末尾の記号除去
        auto cut = p.name.find_first_of("=;");
        if (cut != std::string::npos) {
            p.name = trim(p.name.substr(0, cut));
        }
        if (!p.name.empty()) {
            params.push_back(p);
        }
    }
    return params;
}

// import文から相対モジュール指定子を取り出す（対象外なら空文字列）。
// 対象: `import ./name;` / `import ../dir/name;`（選択import等は対象外）
std::string parse_relative_import(const std::string& line) {
    std::string t = trim(line);
    if (t.rfind("import ", 0) != 0) {
        return "";
    }
    t = trim(t.substr(7));
    if (t.empty() || t.back() != ';') {
        return "";
    }
    t = trim(t.substr(0, t.size() - 1));
    if (t.rfind("./", 0) != 0 && t.rfind("../", 0) != 0) {
        return "";
    }
    // 選択import（::{...}）やエイリアスは階層化対象外
    if (t.find("::") != std::string::npos || t.find(' ') != std::string::npos) {
        return "";
    }
    return t;
}

}  // namespace

HierarchyResult process_sv_hierarchy(const std::string& source, const std::string& input_file) {
    HierarchyResult result;
    if (!has_hierarchy_directive(source)) {
        return result;
    }
    result.enabled = true;

    // 循環import検出
    std::error_code ec;
    auto abs_input = std::filesystem::absolute(input_file, ec).lexically_normal();
    if (const char* chain = std::getenv(kChainEnv)) {
        std::string chain_str = chain;
        if (chain_str.find(abs_input.string()) != std::string::npos) {
            result.error = "循環したsv階層importを検出しました: " + abs_input.string();
            return result;
        }
    }

    auto base_dir = abs_input.parent_path();

    std::ostringstream out;
    std::istringstream ss(source);
    std::string line;
    bool first = true;
    while (std::getline(ss, line)) {
        if (!first) {
            out << "\n";
        }
        first = false;

        std::string spec = parse_relative_import(line);
        if (spec.empty()) {
            out << line;
            continue;
        }

        // サブモジュールファイルの解決
        auto sub_path = (base_dir / (spec + ".cm")).lexically_normal();
        if (!std::filesystem::exists(sub_path)) {
            result.error = "sv階層import先が見つかりません: " + sub_path.string();
            return result;
        }

        // ポート抽出
        std::ifstream sub_file(sub_path);
        if (!sub_file.is_open()) {
            result.error = "sv階層import先を読み込めません: " + sub_path.string();
            return result;
        }
        std::stringstream sub_src;
        sub_src << sub_file.rdbuf();
        // ポート抽出: IOインスタンス（構造体宣言+インスタンス使用）のフィールドと
        // 直接のポート宣言（#[input]/#[output]）の両方を対象にする
        auto ports = extract_io_instance_ports(sub_src.str());
        for (const auto& p : extract_ports(sub_src.str())) {
            ports.push_back(p);
        }
        if (ports.empty()) {
            result.error =
                "sv階層import先にポート宣言（IOインスタンスまたは #[input]/#[output]）が"
                "ありません: " +
                sub_path.string();
            return result;
        }

        // モジュール名 = ファイル名のstem
        std::string module_name = sub_path.stem().string();

        // 1行のextern struct宣言に置換する（行番号を保存するため）。
        // #[sv::parameter] は #[sv::param] フィールドとして写し、
        // インスタンス側からstructリテラルで上書き可能にする
        auto sub_params = extract_parameters(sub_src.str());
        std::string decl = "extern struct " + module_name + " { ";
        for (const auto& pr : sub_params) {
            decl += "#[sv::param] " + pr.type + " " + pr.name;
            if (!pr.default_value.empty()) {
                decl += " = " + pr.default_value;
            }
            decl += "; ";
        }
        for (const auto& p : ports) {
            decl += "#[" + p.direction + "] " + p.type + " " + p.name + "; ";
        }
        decl += "}";
        out << decl;

        // サブモジュールを記録（重複排除）
        std::string sub_str = sub_path.string();
        bool seen = false;
        for (const auto& s : result.submodule_files) {
            if (s == sub_str) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            result.submodule_files.push_back(sub_str);
        }
    }

    result.transformed_source = out.str();
    return result;
}

bool append_submodules(const std::string& exe_path, const std::string& top_input_file,
                       const std::vector<std::string>& submodule_files,
                       const std::string& top_output, int opt_level, bool emit_memfile,
                       std::string& error) {
    if (submodule_files.empty()) {
        return true;
    }

    std::error_code ec;
    auto abs_input = std::filesystem::absolute(top_input_file, ec).lexically_normal();

    // 循環検出チェーンを拡張
    std::string chain;
    if (const char* prev = std::getenv(kChainEnv)) {
        chain = prev;
    }
    if (!chain.empty()) {
        chain += ":";
    }
    chain += abs_input.string();

    // 相対パスの実行ファイルはカレントディレクトリ基準で絶対化する
    // （サブプロセスをどのcwdでも起動できるように）
    std::string exe = exe_path;
    if (exe.find('/') != std::string::npos) {
        auto abs_exe = std::filesystem::absolute(exe, ec);
        if (!ec) {
            exe = abs_exe.lexically_normal().string();
        }
    }

    for (size_t i = 0; i < submodule_files.size(); ++i) {
        const auto& sub = submodule_files[i];
        std::string tmp_out = top_output + ".sub" + std::to_string(i) + ".sv";

        std::string cmd = std::string(kChainEnv) + "='" + chain + "' \"" + exe +
                          "\" compile --target=sv \"" + sub + "\" -o \"" + tmp_out + "\" -O" +
                          std::to_string(opt_level) + " -q";
        if (emit_memfile) {
            cmd += " --emit-memfile";
        }

        int rc = std::system(cmd.c_str());
        if (rc != 0) {
            error = "サブモジュールのSVコンパイルに失敗しました: " + sub;
            return false;
        }

        std::ifstream sub_sv(tmp_out);
        if (!sub_sv.is_open()) {
            error = "サブモジュールの生成SVを読み込めません: " + tmp_out;
            return false;
        }
        std::stringstream buf;
        buf << sub_sv.rdbuf();
        sub_sv.close();
        std::filesystem::remove(tmp_out, ec);

        // ヘッダコメント・timescaleを除去し、module宣言から連結する
        std::string sub_text = buf.str();
        size_t module_pos = sub_text.find("\nmodule ");
        if (module_pos == std::string::npos) {
            module_pos = (sub_text.rfind("module ", 0) == 0) ? 0 : std::string::npos;
        } else {
            module_pos += 1;
        }
        if (module_pos == std::string::npos) {
            error = "サブモジュールの生成SVにmodule宣言がありません: " + sub;
            return false;
        }

        std::ofstream top_sv(top_output, std::ios::app);
        if (!top_sv.is_open()) {
            error = "トップ出力ファイルへ追記できません: " + top_output;
            return false;
        }
        top_sv << "\n// ============================================================\n";
        top_sv << "// サブモジュール: " << std::filesystem::path(sub).filename().string() << "\n";
        top_sv << "// ============================================================\n";
        top_sv << sub_text.substr(module_pos);
    }

    return true;
}

}  // namespace cm::codegen::sv
