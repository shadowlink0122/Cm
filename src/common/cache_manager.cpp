// インクリメンタルビルド用キャッシュマネージャー実装
// SHA-256による変更検出と成果物キャッシュ

#include "cache_manager.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

// ヘッダーオンリーSHA-256ライブラリ
#include "../../third-party/picosha2/picosha2.h"

namespace cm::cache {

// コンパイラバイナリのパス（argv[0]から設定）
std::string CacheManager::compiler_path_;

void CacheManager::set_compiler_path(const std::string& path) {
    compiler_path_ = path;
}

// ========== コンストラクタ ==========

CacheManager::CacheManager(const CacheConfig& config) : config_(config) {}

// ========== ファイルハッシュ計算 ==========

std::string CacheManager::compute_file_hash(const std::filesystem::path& file_path) {
    std::ifstream ifs(file_path, std::ios::binary);
    if (!ifs.is_open()) {
        return "";
    }

    // picosha2でSHA-256ハッシュを計算
    std::vector<unsigned char> hash(picosha2::k_digest_size);
    picosha2::hash256(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>(),
                      hash.begin(), hash.end());

    return picosha2::bytes_to_hex_string(hash.begin(), hash.end());
}

// ========== コンパイラバイナリハッシュ ==========

std::string CacheManager::compute_compiler_hash() {
    if (compiler_path_.empty()) {
        return "unknown";
    }
    // コンパイラバイナリのSHA-256でコンパイラ変更を検出
    auto hash = compute_file_hash(compiler_path_);
    return hash.empty() ? "unknown" : hash;
}

// ========== 合成フィンガープリント生成 ==========

std::string CacheManager::compute_fingerprint(const std::vector<std::string>& source_files,
                                              const std::string& target, int optimization_level) {
    // 全ファイルのハッシュを結合してフィンガープリントを生成
    std::string combined;

    // ファイルパスをソートして順序を固定
    std::vector<std::string> sorted_files = source_files;
    std::sort(sorted_files.begin(), sorted_files.end());

    for (const auto& file : sorted_files) {
        std::string hash = compute_file_hash(file);
        if (hash.empty()) {
            // ファイルが読めない場合はキャッシュ不可
            return "";
        }
        combined += file + ":" + hash + "\n";
    }

    // ターゲットと最適化レベルも含める
    combined += "target:" + target + "\n";
    combined += "opt:" + std::to_string(optimization_level) + "\n";
    combined += "version:" + get_compiler_version() + "\n";
    combined += "compiler:" + compute_compiler_hash() + "\n";

    // 結合文字列のSHA-256を最終フィンガープリントとする
    return picosha2::hash256_hex_string(combined);
}

// ========== キャッシュ検索 ==========

std::optional<CacheEntry> CacheManager::lookup(const std::string& fingerprint) {
    if (fingerprint.empty() || !config_.enabled) {
        return std::nullopt;
    }

    auto entries = load_manifest();
    auto it = entries.find(fingerprint);
    if (it == entries.end()) {
        return std::nullopt;
    }

    // オブジェクトファイルが存在するか確認
    auto obj_path = objects_dir() / it->second.object_file;
    if (!std::filesystem::exists(obj_path)) {
        return std::nullopt;
    }

    return it->second;
}

// ========== ファイル単位変更検出 ==========

std::vector<std::string> CacheManager::detect_changed_files(
    const std::vector<std::string>& current_files, const std::string& target,
    int optimization_level) {
    std::vector<std::string> changed;

    // 直近のキャッシュエントリから前回のハッシュを探す
    auto entries = load_manifest();
    if (entries.empty()) {
        return changed;  // 初回コンパイル
    }

    // 同じターゲット・最適化レベルの最新エントリを探す
    const CacheEntry* best = nullptr;
    for (const auto& [_, entry] : entries) {
        if (entry.target == target && entry.optimization_level == optimization_level) {
            if (!best || entry.created_at > best->created_at) {
                best = &entry;
            }
        }
    }

    if (!best || best->source_hashes.empty()) {
        return changed;  // 比較対象なし
    }

    // 各ファイルの現在のハッシュと前回を比較
    for (const auto& file : current_files) {
        auto current_hash = compute_file_hash(file);
        auto prev_it = best->source_hashes.find(file);
        if (prev_it == best->source_hashes.end()) {
            // 新規ファイル
            changed.push_back(file);
        } else if (prev_it->second != current_hash) {
            // 変更されたファイル
            changed.push_back(file);
        }
    }

    // 前回あったが今回ないファイル（削除されたファイル）
    for (const auto& [prev_file, _] : best->source_hashes) {
        bool found = false;
        for (const auto& f : current_files) {
            if (f == prev_file) {
                found = true;
                break;
            }
        }
        if (!found) {
            changed.push_back(prev_file + " (削除)");
        }
    }

    return changed;
}

// ========== モジュール別フィンガープリント計算 ==========

std::map<std::string, std::string> CacheManager::compute_module_fingerprints(
    const std::map<std::string, std::vector<std::string>>& module_files) {
    std::map<std::string, std::string> fingerprints;

    for (const auto& [module_name, files] : module_files) {
        // モジュール内のファイルをソートして順序を固定
        std::vector<std::string> sorted_files = files;
        std::sort(sorted_files.begin(), sorted_files.end());

        std::string combined;
        for (const auto& file : sorted_files) {
            std::string hash = compute_file_hash(file);
            if (hash.empty()) {
                continue;  // 読めないファイルはスキップ
            }
            combined += file + ":" + hash + "\n";
        }

        if (!combined.empty()) {
            fingerprints[module_name] = picosha2::hash256_hex_string(combined);
        }
    }

    return fingerprints;
}

// ========== 変更モジュール検出 ==========

std::vector<std::string> CacheManager::detect_changed_modules(
    const std::map<std::string, std::vector<std::string>>& module_files, const std::string& target,
    int optimization_level) {
    std::vector<std::string> changed;

    // 前回のキャッシュエントリを探す
    auto entries = load_manifest();
    const CacheEntry* best = nullptr;
    for (const auto& [_, entry] : entries) {
        if (entry.target == target && entry.optimization_level == optimization_level) {
            if (!best || entry.created_at > best->created_at) {
                best = &entry;
            }
        }
    }

    // 前回のモジュールフィンガープリントがない場合、全モジュールを変更とみなす
    if (!best || best->module_fingerprints.empty()) {
        for (const auto& [module_name, _] : module_files) {
            changed.push_back(module_name);
        }
        return changed;
    }

    // 現在のモジュールフィンガープリントを計算
    auto current_fps = compute_module_fingerprints(module_files);

    // 各モジュールのフィンガープリントを比較
    for (const auto& [module_name, current_fp] : current_fps) {
        auto prev_it = best->module_fingerprints.find(module_name);
        if (prev_it == best->module_fingerprints.end()) {
            // 新規モジュール
            changed.push_back(module_name);
        } else if (prev_it->second != current_fp) {
            // 変更されたモジュール
            changed.push_back(module_name);
        }
    }

    // 前回あったが今回ないモジュール（削除）
    for (const auto& [prev_module, _] : best->module_fingerprints) {
        if (current_fps.find(prev_module) == current_fps.end()) {
            changed.push_back(prev_module + " (削除)");
        }
    }

    return changed;
}

// 2つのフィンガープリントマップを直接比較して変更モジュールを検出（static）
std::vector<std::string> CacheManager::detect_changed_modules(
    const std::map<std::string, std::string>& prev_fingerprints,
    const std::map<std::string, std::string>& current_fingerprints) {
    std::vector<std::string> changed;

    // 現在のモジュールと前回を比較
    for (const auto& [module_name, current_fp] : current_fingerprints) {
        auto prev_it = prev_fingerprints.find(module_name);
        if (prev_it == prev_fingerprints.end()) {
            // 新規モジュール
            changed.push_back(module_name);
        } else if (prev_it->second != current_fp) {
            // 変更されたモジュール
            changed.push_back(module_name);
        }
    }

    // 前回あったが今回ないモジュール（削除）
    for (const auto& [prev_module, _] : prev_fingerprints) {
        if (current_fingerprints.find(prev_module) == current_fingerprints.end()) {
            changed.push_back(prev_module);
        }
    }

    return changed;
}

bool CacheManager::store(const std::string& fingerprint, const std::filesystem::path& object_file,
                         const CacheEntry& entry) {
    if (fingerprint.empty() || !config_.enabled) {
        return false;
    }

    if (!ensure_cache_dir()) {
        return false;
    }

    // オブジェクトファイルをキャッシュにコピー
    auto obj_dest = objects_dir() / entry.object_file;
    try {
        std::filesystem::copy_file(object_file, obj_dest,
                                   std::filesystem::copy_options::overwrite_existing);
    } catch (const std::exception& e) {
        std::cerr << "キャッシュ保存エラー: " << e.what() << "\n";
        return false;
    }

    // マニフェストを更新
    auto entries = load_manifest();
    entries[fingerprint] = entry;

    // 古いエントリを削除
    if (entries.size() > config_.max_entries) {
        // 作成日時が古い順に削除
        std::vector<std::pair<std::string, CacheEntry>> sorted_entries(entries.begin(),
                                                                       entries.end());
        std::sort(sorted_entries.begin(), sorted_entries.end(), [](const auto& a, const auto& b) {
            return a.second.created_at < b.second.created_at;
        });

        // 超過分を削除
        size_t to_remove = entries.size() - config_.max_entries;
        for (size_t i = 0; i < to_remove; ++i) {
            auto obj_path = objects_dir() / sorted_entries[i].second.object_file;
            std::filesystem::remove(obj_path);
            entries.erase(sorted_entries[i].first);
        }
    }

    return save_manifest(entries);
}

// ========== モジュール別キャッシュ ==========

bool CacheManager::store_module_object(const std::string& fingerprint,
                                       const std::string& module_name,
                                       const std::string& module_fingerprint,
                                       const std::filesystem::path& object_file) {
    if (fingerprint.empty() || module_name.empty() || !config_.enabled) {
        return false;
    }

    try {
        // モジュール別サブディレクトリを作成
        auto mod_dir = modules_dir() / fingerprint.substr(0, 16);
        std::filesystem::create_directories(mod_dir);

        // モジュール名をファイル名安全な文字列に変換
        std::string safe_name = module_name;
        for (auto& c : safe_name) {
            if (c == '/' || c == '\\' || c == ':')
                c = '_';
        }

        // .o ファイルをキャッシュにコピー
        auto dest = mod_dir / (safe_name + "_" + module_fingerprint.substr(0, 8) + ".o");
        std::filesystem::copy_file(object_file, dest,
                                   std::filesystem::copy_options::overwrite_existing);

        // メタデータファイルを書き込み（モジュール名 → フィンガープリント対応）
        auto meta_path = mod_dir / (safe_name + ".meta");
        std::ofstream ofs(meta_path);
        if (ofs.is_open()) {
            ofs << "module=" << module_name << "\n";
            ofs << "fingerprint=" << module_fingerprint << "\n";
            ofs << "object=" << dest.filename().string() << "\n";
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "[CACHE] モジュールキャッシュ保存エラー: " << e.what() << "\n";
        return false;
    }
}

std::optional<std::filesystem::path> CacheManager::lookup_module_object(
    const std::string& module_name, const std::string& module_fingerprint) {
    if (module_name.empty() || module_fingerprint.empty() || !config_.enabled) {
        return std::nullopt;
    }

    try {
        // 全フィンガープリントディレクトリを検索
        if (!std::filesystem::exists(modules_dir())) {
            return std::nullopt;
        }

        std::string safe_name = module_name;
        for (auto& c : safe_name) {
            if (c == '/' || c == '\\' || c == ':')
                c = '_';
        }

        for (const auto& dir_entry : std::filesystem::directory_iterator(modules_dir())) {
            if (!dir_entry.is_directory())
                continue;

            auto meta_path = dir_entry.path() / (safe_name + ".meta");
            if (!std::filesystem::exists(meta_path))
                continue;

            // メタデータを読み込んでフィンガープリントを比較
            std::ifstream ifs(meta_path);
            std::string line;
            std::string cached_fp;
            std::string cached_obj;
            while (std::getline(ifs, line)) {
                if (line.substr(0, 12) == "fingerprint=") {
                    cached_fp = line.substr(12);
                } else if (line.substr(0, 7) == "object=") {
                    cached_obj = line.substr(7);
                }
            }

            if (cached_fp == module_fingerprint && !cached_obj.empty()) {
                auto obj_path = dir_entry.path() / cached_obj;
                if (std::filesystem::exists(obj_path)) {
                    return obj_path;
                }
            }
        }
    } catch (const std::exception&) {
        // 検索エラーは無視
    }

    return std::nullopt;
}

std::map<std::string, std::filesystem::path> CacheManager::get_cached_module_objects(
    const std::string& fingerprint) {
    std::map<std::string, std::filesystem::path> result;

    if (fingerprint.empty() || !config_.enabled) {
        return result;
    }

    try {
        auto mod_dir = modules_dir() / fingerprint.substr(0, 16);
        if (!std::filesystem::exists(mod_dir)) {
            return result;
        }

        for (const auto& entry : std::filesystem::directory_iterator(mod_dir)) {
            if (entry.path().extension() == ".meta") {
                std::ifstream ifs(entry.path());
                std::string line;
                std::string mod_name;
                std::string obj_name;
                while (std::getline(ifs, line)) {
                    if (line.substr(0, 7) == "module=") {
                        mod_name = line.substr(7);
                    } else if (line.substr(0, 7) == "object=") {
                        obj_name = line.substr(7);
                    }
                }

                if (!mod_name.empty() && !obj_name.empty()) {
                    auto obj_path = mod_dir / obj_name;
                    if (std::filesystem::exists(obj_path)) {
                        result[mod_name] = obj_path;
                    }
                }
            }
        }
    } catch (const std::exception&) {
        // 検索エラーは無視
    }

    return result;
}

// ========== キャッシュ統計 ==========

CacheStats CacheManager::get_stats() const {
    CacheStats stats;

    auto entries = load_manifest();
    stats.total_entries = entries.size();

    // オブジェクトファイルの合計サイズを計算
    for (const auto& [_, entry] : entries) {
        auto obj_path = objects_dir() / entry.object_file;
        if (std::filesystem::exists(obj_path)) {
            stats.total_size_bytes += std::filesystem::file_size(obj_path);
        }
    }

    return stats;
}

// ========== 全エントリ取得 ==========

std::map<std::string, CacheEntry> CacheManager::get_all_entries() const {
    return load_manifest();
}

// ========== キャッシュクリア ==========

bool CacheManager::clear() {
    try {
        if (std::filesystem::exists(config_.cache_dir)) {
            std::filesystem::remove_all(config_.cache_dir);
            return true;
        }
    } catch (const std::exception& e) {
        std::cerr << "キャッシュクリアエラー: " << e.what() << "\n";
    }
    return false;
}

// ========== 古いエントリの削除 ==========

void CacheManager::evict_old_entries() {
    auto entries = load_manifest();
    if (entries.size() <= config_.max_entries) {
        return;
    }

    // 作成日時が古い順にソート
    std::vector<std::pair<std::string, CacheEntry>> sorted_entries(entries.begin(), entries.end());
    std::sort(sorted_entries.begin(), sorted_entries.end(), [](const auto& a, const auto& b) {
        return a.second.created_at < b.second.created_at;
    });

    size_t to_remove = entries.size() - config_.max_entries;
    for (size_t i = 0; i < to_remove; ++i) {
        auto obj_path = objects_dir() / sorted_entries[i].second.object_file;
        std::filesystem::remove(obj_path);
        entries.erase(sorted_entries[i].first);
    }

    save_manifest(entries);
}

// ========== コンパイラバージョン取得 ==========

std::string CacheManager::get_compiler_version() {
    // VERSIONファイルから読み込み
    std::ifstream ifs("VERSION");
    if (ifs.is_open()) {
        std::string version;
        std::getline(ifs, version);
        // 前後の空白を除去
        auto start = version.find_first_not_of(" \t\r\n");
        auto end = version.find_last_not_of(" \t\r\n");
        if (start != std::string::npos) {
            return version.substr(start, end - start + 1);
        }
    }
    return "unknown";
}

// ========== プライベートメソッド ==========

bool CacheManager::ensure_cache_dir() {
    try {
        std::filesystem::create_directories(objects_dir());
        return true;
    } catch (const std::exception& e) {
        std::cerr << "キャッシュディレクトリ作成エラー: " << e.what() << "\n";
        return false;
    }
}

std::filesystem::path CacheManager::manifest_path() const {
    return config_.cache_dir / "manifest.json";
}

std::filesystem::path CacheManager::objects_dir() const {
    return config_.cache_dir / "objects";
}

std::filesystem::path CacheManager::modules_dir() const {
    return config_.cache_dir / "modules";
}

// ========== マニフェスト読み込み（簡易JSONパーサー） ==========

std::map<std::string, CacheEntry> CacheManager::load_manifest() const {
    std::map<std::string, CacheEntry> entries;

    std::ifstream ifs(manifest_path());
    if (!ifs.is_open()) {
        return entries;
    }

    // 簡易JSONパーサー: 行ベースでキー=値を読み取る
    // V1形式: fingerprint|target|opt_level|version|object_file|timestamp|hashes
    // V2形式: fingerprint|target|opt_level|version|object_file|timestamp|hashes|module_fps
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::istringstream iss(line);
        std::string fingerprint, target, opt_str, version, obj_file, timestamp, hashes_str;

        if (!std::getline(iss, fingerprint, '|') || !std::getline(iss, target, '|') ||
            !std::getline(iss, opt_str, '|') || !std::getline(iss, version, '|') ||
            !std::getline(iss, obj_file, '|') || !std::getline(iss, timestamp, '|') ||
            !std::getline(iss, hashes_str, '|')) {
            continue;
        }

        CacheEntry entry;
        entry.fingerprint = fingerprint;
        entry.target = target;
        try {
            entry.optimization_level = std::stoi(opt_str);
        } catch (...) {
            entry.optimization_level = 0;
        }
        entry.compiler_version = version;
        entry.object_file = obj_file;
        entry.created_at = timestamp;

        // ソースハッシュを解析: file1=hash1,file2=hash2,...
        std::istringstream hash_iss(hashes_str);
        std::string hash_pair;
        while (std::getline(hash_iss, hash_pair, ',')) {
            auto eq_pos = hash_pair.find('=');
            if (eq_pos != std::string::npos) {
                entry.source_hashes[hash_pair.substr(0, eq_pos)] = hash_pair.substr(eq_pos + 1);
            }
        }

        // V2: モジュールフィンガープリントを解析ï¼存在する場合ï¼
        std::string module_fps_str;
        if (std::getline(iss, module_fps_str) && !module_fps_str.empty()) {
            std::istringstream mfp_iss(module_fps_str);
            std::string mfp_pair;
            while (std::getline(mfp_iss, mfp_pair, ',')) {
                auto eq_pos = mfp_pair.find('=');
                if (eq_pos != std::string::npos) {
                    entry.module_fingerprints[mfp_pair.substr(0, eq_pos)] =
                        mfp_pair.substr(eq_pos + 1);
                }
            }
        }

        entries[fingerprint] = entry;
    }

    return entries;
}

// ========== マニフェスト書き込み ==========

bool CacheManager::save_manifest(const std::map<std::string, CacheEntry>& entries) const {
    try {
        std::filesystem::create_directories(config_.cache_dir);
    } catch (...) {
        return false;
    }

    std::ofstream ofs(manifest_path());
    if (!ofs.is_open()) {
        return false;
    }

    // ヘッダーコメント
    ofs << "# Cm Compiler Cache Manifest V2\n";
    ofs << "# 形式: fingerprint|target|opt_level|version|object_file|timestamp|hashes|module_fps\n";

    for (const auto& [fp, entry] : entries) {
        ofs << fp << "|" << entry.target << "|" << entry.optimization_level << "|"
            << entry.compiler_version << "|" << entry.object_file << "|" << entry.created_at << "|";

        // ソースハッシュを書き出し
        bool first = true;
        for (const auto& [file, hash] : entry.source_hashes) {
            if (!first) {
                ofs << ",";
            }
            ofs << file << "=" << hash;
            first = false;
        }

        // モジュールフィンガープリントを書き出し（V2拡張）
        ofs << "|";
        first = true;
        for (const auto& [module_name, mfp] : entry.module_fingerprints) {
            if (!first) {
                ofs << ",";
            }
            ofs << module_name << "=" << mfp;
            first = false;
        }

        ofs << "\n";
    }

    return true;
}

// ========== 現在日時を取得 ==========

std::string CacheManager::current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    localtime_r(&time, &tm_buf);

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

}  // namespace cm::cache
