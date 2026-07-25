#pragma once

// MIRプログラムをモジュール別に分割するユーティリティ
// 分割はゼロコピーで行い、元のMirProgramの参照を保持する

#include "nodes.hpp"

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace cm::mir {

// モジュール別サブプログラム（元のMirProgramへのビュー）
struct ModuleProgram {
    std::string module_name;  // モジュール名（空文字列 → "main"）

    // このモジュールに属する要素（元のMirProgramの要素への参照）
    std::vector<const MirFunction*> functions;
    std::vector<const MirStruct*> structs;
    std::vector<const MirEnum*> enums;

    // 他モジュールから参照されるが、このモジュールには定義されない要素
    // extern宣言の生成に使用
    std::vector<const MirFunction*> extern_functions;
    std::vector<const MirStruct*> extern_structs;
    std::vector<const MirEnum*> extern_enums;

    // 共有データ（全モジュール共通、元のMirProgramへの参照）
    std::vector<const MirInterface*> interfaces;
    std::vector<const VTable*> vtables;

    // このモジュールが定義を持つグローバル変数（所有モジュールのみ。定義の重複による状態分裂を防ぐ）
    std::vector<const MirGlobalVar*> global_vars;

    // 他モジュールが定義するグローバル変数（extern宣言のみ生成する）
    std::vector<const MirGlobalVar*> extern_global_vars;

    // typedef定義マップ（LLVM backendでTypeAlias/Struct名の解決に使用）
    const std::unordered_map<std::string, hir::TypePtr>* typedef_defs = nullptr;

    // 分割元のMirProgram（関数存在判定・インターフェイス検索等、プログラム全体のメタデータ参照用）
    const MirProgram* origin = nullptr;
};

// MIR分割ユーティリティ
class MirSplitter {
   public:
    // MirProgramをsource_file（優先）またはmodule_pathに基づきモジュール別に分割
    // 戻り値: モジュール名 → ModuleProgram のマップ
    static std::map<std::string, ModuleProgram> split_by_module(const MirProgram& program);

    // モジュール名を正規化（空文字列 → "main"）
    static std::string normalize_module_name(const std::string& module_path);

    // ソースファイルパスからモジュール名を導出
    // 例: "libs/efi_core.cm" → "libs_efi_core"
    //     "main.cm" → "main"
    //     "" → "main"
    static std::string source_file_to_module_name(const std::string& source_file);

    // 関数が呼び出す関数名を収集（リンク時のランタイムライブラリ使用判定にも使用）
    static std::vector<std::string> collect_called_functions(const MirFunction& func);

   private:
    // 関数が参照する型名を収集
    static std::vector<std::string> collect_referenced_types(const MirFunction& func);
};

}  // namespace cm::mir
