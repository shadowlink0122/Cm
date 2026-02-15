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

// ========== キャッシュ保存 ==========

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

// ========== マニフェスト読み込み（簡易JSONパーサー） ==========

std::map<std::string, CacheEntry> CacheManager::load_manifest() const {
    std::map<std::string, CacheEntry> entries;

    std::ifstream ifs(manifest_path());
    if (!ifs.is_open()) {
        return entries;
    }

    // 簡易JSONパーサー: 行ベースでキー=値を読み取る
    // 形式: fingerprint|target|opt_level|compiler_version|object_file|created_at|hash1,hash2,...
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
            !std::getline(iss, hashes_str)) {
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
    ofs << "# Cm Compiler Cache Manifest\n";
    ofs << "# 形式: fingerprint|target|opt_level|version|object_file|timestamp|hashes\n";

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
