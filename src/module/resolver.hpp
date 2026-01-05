#pragma once

#include "../hir/nodes.hpp"
#include "../mir/nodes.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cm::module {

// モジュール情報を保持する構造体
struct ModuleInfo {
    std::string name;                      // モジュール名（例: "math_lib"）
    std::string path;                      // ファイルパス
    std::vector<std::string> exports;      // エクスポートされる名前
    std::unique_ptr<hir::HirProgram> hir;  // HIR表現
    std::unique_ptr<mir::MirProgram> mir;  // MIR表現（コンパイル後）
    bool is_loaded = false;                // ロード済みかどうか
    bool is_compiled = false;              // コンパイル済みかどうか
};

// モジュール解決器
class ModuleResolver {
   private:
    // モジュールキャッシュ（モジュール名 -> ModuleInfo）
    std::unordered_map<std::string, std::unique_ptr<ModuleInfo>> modules;

    // モジュール検索パス
    std::vector<std::filesystem::path> search_paths;

    // 現在のディレクトリ
    std::filesystem::path current_dir;

   public:
    ModuleResolver();

    // モジュール検索パスを追加
    void add_search_path(const std::filesystem::path& path);

    // モジュールを解決（ファイルを見つける）
    std::filesystem::path resolve_module_path(const std::string& module_name);

    // モジュールをロード（パースしてHIRを生成）
    ModuleInfo* load_module(const std::string& module_name);

    // モジュールをコンパイル（MIRを生成）
    bool compile_module(const std::string& module_name);

    // エクスポートされた関数を検索
    const mir::MirFunction* find_exported_function(const std::string& module_name,
                                                   const std::string& function_name);

    // エクスポートされた構造体を検索
    const mir::MirStruct* find_exported_struct(const std::string& module_name,
                                               const std::string& struct_name);

    // すべてのロード済みモジュールを取得
    const std::unordered_map<std::string, std::unique_ptr<ModuleInfo>>& get_modules() const {
        return modules;
    }

   private:
    // モジュールファイルをパース
    std::unique_ptr<hir::HirProgram> parse_module_file(const std::filesystem::path& path);

    // エクスポートリストを抽出
    std::vector<std::string> extract_exports(const hir::HirProgram& program);
};

// グローバルモジュールリゾルバーのインスタンス
extern std::unique_ptr<ModuleResolver> g_module_resolver;

// 初期化
void initialize_module_resolver();

}  // namespace cm::module