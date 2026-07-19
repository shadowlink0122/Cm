#include "hierarchy.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

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

// サブモジュールの #[sv::parameter] const 宣言を抽出する。
// 対象: `#[sv::parameter] const uint WIDTH = 8;` 形式。
// 生成するextern structに #[sv::param] フィールドとして写し、インスタンス側から #(.WIDTH(値)) で上書き可能にする（v0.16.0 設計01 P3）
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

// exportされたIO構造体（方向属性フィールドを持つ export struct）の情報。
// これが宣言されたモジュールだけが階層化（別モジュールとしてのインスタンス化）の対象になる
struct ExportedIoStruct {
    bool found = false;
    std::string name;               // 構造体名
    std::vector<ParamDecl> params;  // #[sv::param] フィールド（宣言順）
    std::vector<PortDecl> ports;    // 方向属性フィールド（宣言順）
};

// `#[sv::param] uint WIDTH = 8;` 形式のパラメータフィールドをパースする。
// パラメータフィールドでなければfalseを返す
bool parse_param_field_line(const std::string& raw, ParamDecl& out) {
    std::string t = trim(raw);
    if (t.rfind("#[", 0) != 0) {
        return false;
    }
    bool is_param = false;
    while (t.rfind("#[", 0) == 0) {
        size_t close = t.find(']');
        if (close == std::string::npos) {
            break;
        }
        std::string attr = trim(t.substr(2, close - 2));
        if (attr == "sv::param" || attr == "verilog::param") {
            is_param = true;
        }
        t = trim(t.substr(close + 1));
    }
    if (!is_param || t.empty()) {
        return false;
    }
    std::istringstream toks(t);
    ParamDecl p;
    if (!(toks >> p.type) || !(toks >> p.name)) {
        return false;
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
    auto cut = p.name.find_first_of("=;");
    if (cut != std::string::npos) {
        p.name = trim(p.name.substr(0, cut));
    }
    if (p.name.empty()) {
        return false;
    }
    out = p;
    return true;
}

// ソースから最初のexportされたIO構造体を抽出する。
// 対象: `export struct Name { #[sv::param]/#[input]/#[output]/#[inout] フィールド }`。
// 方向属性フィールドを1つ以上持つものだけをIO構造体とみなす
ExportedIoStruct extract_exported_io_struct(const std::string& source) {
    std::istringstream ss(source);
    std::string line;
    std::string current_struct;
    ExportedIoStruct current;
    while (std::getline(ss, line)) {
        std::string t = trim(line);
        if (current_struct.empty()) {
            if (t.rfind("export ", 0) != 0) {
                continue;
            }
            t = trim(t.substr(7));
            if (t.rfind("extern ", 0) == 0) {
                continue;  // extern structは対象外（既に外部モジュール宣言）
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
            current = ExportedIoStruct{};
            current.name = name;
            continue;
        }
        if (!t.empty() && t[0] == '}') {
            if (!current.ports.empty()) {
                current.found = true;
                return current;
            }
            current_struct.clear();
            continue;
        }
        ParamDecl pd;
        if (parse_param_field_line(t, pd)) {
            current.params.push_back(pd);
            continue;
        }
        PortDecl p;
        if (parse_port_line(t, p)) {
            current.ports.push_back(p);
        }
    }
    return ExportedIoStruct{};
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

    // 階層化したモジュールの修飾名置換対（モジュール名, IO構造体名）
    std::vector<std::pair<std::string, std::string>> qualified_names;

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

        // サブモジュールファイルの解決。
        // 見つからない場合は通常のimport解決に委ねる（エラー報告もそちらで行う）
        auto sub_path = (base_dir / (spec + ".cm")).lexically_normal();
        if (!std::filesystem::exists(sub_path)) {
            out << line;
            continue;
        }

        std::ifstream sub_file(sub_path);
        if (!sub_file.is_open()) {
            result.error = "sv階層import先を読み込めません: " + sub_path.string();
            return result;
        }
        std::stringstream sub_src;
        sub_src << sub_file.rdbuf();

        // 発動条件: import先がexportされたIO構造体（方向属性フィールドを持つ
        // export struct）を宣言していること。無ければ従来どおりフラット化する
        auto io_struct = extract_exported_io_struct(sub_src.str());
        if (!io_struct.found) {
            out << line;
            continue;
        }

        // モジュール名 = ファイル名のstem
        std::string module_name = sub_path.stem().string();

        // パラメータ: IO構造体の #[sv::param] フィールドに加え、
        // #[sv::parameter] const 宣言からの抽出も併用し名前で重複排除する
        auto params = io_struct.params;
        for (const auto& pr : extract_parameters(sub_src.str())) {
            bool dup = false;
            for (const auto& e : params) {
                if (e.name == pr.name) {
                    dup = true;
                    break;
                }
            }
            if (!dup) {
                params.push_back(pr);
            }
        }

        // 1行のextern struct宣言に置換する（行番号を保存するため）。
        // 方向属性フィールドは初期値を除去して写す（extern structでは
        // 出力フィールドの `= 値` が接続指定と解釈されるため）
        std::string decl = "extern struct " + module_name + " { ";
        for (const auto& pr : params) {
            decl += "#[sv::param] " + pr.type + " " + pr.name;
            if (!pr.default_value.empty()) {
                decl += " = " + pr.default_value;
            }
            decl += "; ";
        }
        for (const auto& p : io_struct.ports) {
            decl += "#[" + p.direction + "] " + p.type + " " + p.name + "; ";
        }
        decl += "}";
        out << decl;
        result.enabled = true;
        qualified_names.emplace_back(module_name, io_struct.name);

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

    if (!result.enabled) {
        return result;
    }

    // 親ソース中の修飾名 `<モジュール名>::<IO構造体名>` を `<モジュール名>` へ置換し、既存のextern structインスタンス生成（モジュールインスタンス化）に接続する
    std::string transformed = out.str();
    for (const auto& [module_name, struct_name] : qualified_names) {
        const std::string from = module_name + "::" + struct_name;
        size_t pos = 0;
        while ((pos = transformed.find(from, pos)) != std::string::npos) {
            transformed.replace(pos, from.size(), module_name);
            pos += module_name.size();
        }
    }
    result.transformed_source = transformed;
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

    // 相対パスの実行ファイルはカレントディレクトリ基準で絶対化する（サブプロセスをどのcwdでも起動できるように）
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
