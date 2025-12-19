#include "module_resolver.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

#include "../frontend/lexer/lexer.hpp"
#include "../frontend/parser/parser.hpp"
#include "../hir/hir_lowering.hpp"
#include "../mir/mir_lowering.hpp"

namespace cm::module {

// グローバルインスタンス
std::unique_ptr<ModuleResolver> g_module_resolver;

void initialize_module_resolver() {
    if (!g_module_resolver) {
        g_module_resolver = std::make_unique<ModuleResolver>();
    }
}

ModuleResolver::ModuleResolver() {
    // デフォルトの検索パスを設定
    current_dir = std::filesystem::current_path();

    // 1. カレントディレクトリ
    search_paths.push_back(current_dir);

    // 2. 標準ライブラリパス（プロジェクトルート/std）
    auto std_path = current_dir / "std";
    if (std::filesystem::exists(std_path)) {
        search_paths.push_back(std_path);
    }

    // 3. 環境変数 CM_MODULE_PATH
    if (const char* env_path = std::getenv("CM_MODULE_PATH")) {
        std::stringstream ss(env_path);
        std::string path;
        while (std::getline(ss, path, ':')) {  // Unix系は:で分割
            if (!path.empty()) {
                search_paths.push_back(std::filesystem::path(path));
            }
        }
    }
}

void ModuleResolver::add_search_path(const std::filesystem::path& path) {
    search_paths.push_back(path);
}

std::filesystem::path ModuleResolver::resolve_module_path(const std::string& module_name) {
    // モジュール名をファイル名に変換（例: "math_lib" -> "math_lib.cm"）
    std::string filename = module_name + ".cm";

    // ネストされたモジュールの場合（例: "std::io" -> "std/io.cm"）
    std::string module_path = module_name;
    std::replace(module_path.begin(), module_path.end(), ':', '/');
    if (module_path != module_name) {
        filename = module_path + ".cm";
    }

    // 検索パスから探す
    for (const auto& search_path : search_paths) {
        auto full_path = search_path / filename;
        if (std::filesystem::exists(full_path)) {
            return full_path;
        }

        // mod.cmファイルも試す（例: "std/io/mod.cm"）
        auto mod_path = search_path / module_path / "mod.cm";
        if (std::filesystem::exists(mod_path)) {
            return mod_path;
        }
    }

    return {};  // 見つからない
}

ModuleInfo* ModuleResolver::load_module(const std::string& module_name) {
    // すでにロード済みの場合
    auto it = modules.find(module_name);
    if (it != modules.end()) {
        return it->second.get();
    }

    // モジュールファイルを探す
    auto module_path = resolve_module_path(module_name);
    if (module_path.empty()) {
        std::cerr << "エラー: モジュール '" << module_name << "' が見つかりません\n";
        return nullptr;
    }

    // モジュールをパース
    auto hir_program = parse_module_file(module_path);
    if (!hir_program) {
        std::cerr << "エラー: モジュール '" << module_name << "' のパースに失敗しました\n";
        return nullptr;
    }

    // ModuleInfoを作成
    auto module_info = std::make_unique<ModuleInfo>();
    module_info->name = module_name;
    module_info->path = module_path.string();
    module_info->exports = extract_exports(*hir_program);
    module_info->hir = std::move(hir_program);
    module_info->is_loaded = true;

    // キャッシュに追加
    auto* ptr = module_info.get();
    modules[module_name] = std::move(module_info);

    return ptr;
}

bool ModuleResolver::compile_module(const std::string& module_name) {
    auto* module_info = load_module(module_name);
    if (!module_info) {
        return false;
    }

    if (module_info->is_compiled) {
        return true;  // すでにコンパイル済み
    }

    // HIRからMIRに変換
    mir::MirLowering lowering;
    auto mir_program = std::make_unique<mir::MirProgram>(lowering.lower(*module_info->hir));

    module_info->mir = std::move(mir_program);
    module_info->is_compiled = true;

    return true;
}

const mir::MirFunction* ModuleResolver::find_exported_function(
    const std::string& module_name,
    const std::string& function_name
) {
    auto* module_info = load_module(module_name);
    if (!module_info) {
        return nullptr;
    }

    // コンパイルされていない場合はコンパイル
    if (!module_info->is_compiled) {
        if (!compile_module(module_name)) {
            return nullptr;
        }
    }

    // エクスポートされた関数を検索
    for (const auto& func : module_info->mir->functions) {
        if (func && func->name == function_name && func->is_export) {
            return func.get();
        }
    }

    return nullptr;
}

const mir::MirStruct* ModuleResolver::find_exported_struct(
    const std::string& module_name,
    const std::string& struct_name
) {
    auto* module_info = load_module(module_name);
    if (!module_info) {
        return nullptr;
    }

    // コンパイルされていない場合はコンパイル
    if (!module_info->is_compiled) {
        if (!compile_module(module_name)) {
            return nullptr;
        }
    }

    // エクスポートされた構造体を検索
    for (const auto& st : module_info->mir->structs) {
        if (st && st->name == struct_name && st->is_export) {
            return st.get();
        }
    }

    return nullptr;
}

std::unique_ptr<hir::HirProgram> ModuleResolver::parse_module_file(const std::filesystem::path& path) {
    // ファイルを読み込む
    std::ifstream file(path);
    if (!file) {
        return nullptr;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    // レキサーとパーサーで処理
    Lexer lex(source);
    auto tokens = lex.tokenize();

    Parser parser(tokens);
    auto ast = parser.parse();

    // HIRに変換
    hir::HirLowering hir_lowering;
    return std::make_unique<hir::HirProgram>(hir_lowering.lower(ast));
}

std::vector<std::string> ModuleResolver::extract_exports(const hir::HirProgram& program) {
    std::vector<std::string> exports;

    for (const auto& decl : program.declarations) {
        // 関数のエクスポート
        if (auto* func = std::get_if<std::unique_ptr<hir::HirFunction>>(&decl->kind)) {
            if ((*func)->is_export) {
                exports.push_back((*func)->name);
            }
        }
        // 構造体のエクスポート
        else if (auto* st = std::get_if<std::unique_ptr<hir::HirStruct>>(&decl->kind)) {
            if ((*st)->is_export) {
                exports.push_back((*st)->name);
            }
        }
        // インターフェースのエクスポート
        else if (auto* iface = std::get_if<std::unique_ptr<hir::HirInterface>>(&decl->kind)) {
            if ((*iface)->is_export) {
                exports.push_back((*iface)->name);
            }
        }
        // Enumのエクスポート
        else if (auto* en = std::get_if<std::unique_ptr<hir::HirEnum>>(&decl->kind)) {
            if ((*en)->is_export) {
                exports.push_back((*en)->name);
            }
        }
    }

    return exports;
}

}  // namespace cm::module