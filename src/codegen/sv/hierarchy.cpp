#include "hierarchy.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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

// サブモジュールのソースからポート宣言を抽出する（行単位の軽量スキャン）。
// 対象: `#[input] uint a;` / `#[output] utiny out = 0;` / `#[input] posedge clk;` 等
std::vector<PortDecl> extract_ports(const std::string& source) {
    std::vector<PortDecl> ports;
    std::istringstream ss(source);
    std::string line;
    while (std::getline(ss, line)) {
        std::string t = trim(line);
        if (t.rfind("#[", 0) != 0) {
            continue;
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
            continue;
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
            continue;
        }
        // クロックエッジ型は1bit信号として接続する
        if (type_tok == "posedge" || type_tok == "negedge") {
            type_tok = "bool";
        }
        if (!(toks >> name_tok)) {
            continue;
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
            continue;
        }
        ports.push_back({direction, type_tok, name_tok});
    }
    return ports;
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
        auto ports = extract_ports(sub_src.str());
        if (ports.empty()) {
            result.error = "sv階層import先にポート宣言（#[input]/#[output]）がありません: " +
                           sub_path.string();
            return result;
        }

        // モジュール名 = ファイル名のstem
        std::string module_name = sub_path.stem().string();

        // 1行のextern struct宣言に置換する（行番号を保存するため）
        std::string decl = "extern struct " + module_name + " { ";
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
