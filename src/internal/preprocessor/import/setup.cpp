// ============================================================
// importプリプロセッサ - 検索パス初期化と前処理エントリポイント
// ============================================================

#include "internal/preprocessor/import.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef __APPLE__
#include <limits.h>
#include <mach-o/dyld.h>
#endif

#ifdef __linux__
#include <limits.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

namespace cm::preprocessor {

// ========== 高速文字列ユーティリティ（std::regex 置換用） ==========

// 実行ファイルのディレクトリを取得するヘルパー関数
static std::filesystem::path get_executable_directory() {
#ifdef __APPLE__
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        return std::filesystem::path(path).parent_path();
    }
#endif

#ifdef __linux__
    char path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len != -1) {
        path[len] = '\0';
        return std::filesystem::path(path).parent_path();
    }
#endif

#ifdef _WIN32
    char path[MAX_PATH];
    if (GetModuleFileNameA(NULL, path, MAX_PATH) != 0) {
        return std::filesystem::path(path).parent_path();
    }
#endif

    return {};  // フォールバック: 空のパス
}

ImportPreprocessor::ImportPreprocessor(bool debug) : debug_mode(debug) {
    // プロジェクトルートを検出
    project_root = find_project_root(std::filesystem::current_path());

    // デフォルトの検索パスを設定
    // 1. プロジェクトルート
    search_paths.push_back(project_root);

    // 2. カレントディレクトリ（プロジェクトルートと異なる場合）
    auto current_dir = std::filesystem::current_path();
    if (current_dir != project_root) {
        search_paths.push_back(current_dir);
    }

    // 3. 標準ライブラリパス（環境変数 CM_STD_PATH）
    if (const char* std_env = std::getenv("CM_STD_PATH")) {
        auto std_env_path = std::filesystem::path(std_env);
        if (std::filesystem::exists(std_env_path)) {
            search_paths.push_back(std_env_path);
        }
    }

    // 4. 実行ファイルの場所を基準（異なるディレクトリから実行される場合に重要）
    // 注: std::io は std/io に変換される。libs/std/io を見つけるため exe_dir/libs を追加
    auto exe_dir = get_executable_directory();
    if (!exe_dir.empty()) {
        auto exe_libs = exe_dir / "libs";
        if (std::filesystem::exists(exe_libs)) {
            search_paths.push_back(exe_libs);
        }

        // インストールレイアウト（~/.cm/bin/cm → ~/.cm/libs）にも対応する。
        // リポジトリ外からインストール済みバイナリで実行しても std::* が解決できるようにする
        auto install_libs = exe_dir.parent_path() / "libs";
        if (std::filesystem::exists(install_libs)) {
            search_paths.push_back(install_libs);
        }
    }

    // 5. プロジェクトルート（project_root/libs/std/io 等を探すため）
    auto project_libs = project_root / "libs";
    if (std::filesystem::exists(project_libs)) {
        search_paths.push_back(project_libs);
    }

    // 5. システムインストールパス（プラットフォーム依存）
#ifdef __APPLE__
    // macOS: Homebrew や /usr/local
    std::vector<std::filesystem::path> system_paths = {
        "/usr/local/lib/cm/std", "/opt/homebrew/lib/cm/std",
        std::filesystem::path(std::getenv("HOME") ? std::getenv("HOME") : "") / ".cm/std"};
#elif defined(_WIN32)
    // Windows: AppData や Program Files
    std::vector<std::filesystem::path> system_paths = {
        std::filesystem::path(std::getenv("LOCALAPPDATA") ? std::getenv("LOCALAPPDATA") : "") /
            "Cm/std",
        std::filesystem::path(std::getenv("PROGRAMFILES") ? std::getenv("PROGRAMFILES") : "") /
            "Cm/std"};
#else
    // Linux/Unix
    std::vector<std::filesystem::path> system_paths = {
        "/usr/lib/cm/std", "/usr/local/lib/cm/std",
        std::filesystem::path(std::getenv("HOME") ? std::getenv("HOME") : "") / ".cm/std"};
#endif
    for (const auto& sys_path : system_paths) {
        if (!sys_path.empty() && std::filesystem::exists(sys_path)) {
            search_paths.push_back(sys_path);
        }
    }

    // 6. 環境変数 CM_MODULE_PATH（追加の検索パス）
    if (const char* env_path = std::getenv("CM_MODULE_PATH")) {
        std::stringstream ss(env_path);
        std::string path;
#ifdef _WIN32
        const char delimiter = ';';
#else
        const char delimiter = ':';
#endif
        while (std::getline(ss, path, delimiter)) {
            if (!path.empty()) {
                search_paths.push_back(std::filesystem::path(path));
            }
        }
    }

    if (debug_mode) {
        std::cout << "[PREPROCESSOR] Search paths:\n";
        for (const auto& p : search_paths) {
            std::cout << "  - " << p << "\n";
        }
    }
}

void ImportPreprocessor::add_search_path(const std::filesystem::path& path) {
    search_paths.push_back(path);
}

}  // namespace cm::preprocessor
