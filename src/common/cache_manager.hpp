#pragma once

// インクリメンタルビルド用キャッシュマネージャー
// 全入力ファイルの合成フィンガープリントを計算し、
// コンパイル成果物（.o等）をキャッシュする

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace cm::cache {

// キャッシュ設定
struct CacheConfig {
    std::filesystem::path cache_dir = ".cm-cache";  // キャッシュディレクトリ
    size_t max_entries = 100;                       // 最大エントリ数
    bool enabled = true;                            // キャッシュ有効フラグ
};

// キャッシュエントリのメタデータ
struct CacheEntry {
    std::string fingerprint;                           // 合成フィンガープリント
    std::map<std::string, std::string> source_hashes;  // ファイルパス → SHA-256
    std::string target;                                // ビルドターゲット
    int optimization_level = 0;                        // 最適化レベル
    std::string compiler_version;                      // コンパイラバージョン
    std::string object_file;  // キャッシュされたオブジェクトファイル名
    std::string created_at;   // 作成日時（ISO 8601）
};

// キャッシュ統計
struct CacheStats {
    size_t total_entries = 0;
    size_t total_size_bytes = 0;
    size_t hit_count = 0;
    size_t miss_count = 0;
};

// キャッシュマネージャー
class CacheManager {
   public:
    explicit CacheManager(const CacheConfig& config = CacheConfig{});

    // ファイルのSHA-256ハッシュを計算
    static std::string compute_file_hash(const std::filesystem::path& file_path);

    // 複数ファイル＋メタ情報から合成フィンガープリントを生成
    std::string compute_fingerprint(const std::vector<std::string>& source_files,
                                    const std::string& target, int optimization_level);

    // キャッシュからエントリを検索
    // フィンガープリントが一致し、オブジェクトファイルが存在すればキャッシュヒット
    std::optional<CacheEntry> lookup(const std::string& fingerprint);

    // コンパイル成果物をキャッシュに保存
    bool store(const std::string& fingerprint, const std::filesystem::path& object_file,
               const CacheEntry& entry);

    // キャッシュ統計を取得
    CacheStats get_stats() const;

    // キャッシュを全削除
    bool clear();

    // 古いエントリを削除（LRU的に、max_entriesを超える分を削除）
    void evict_old_entries();

    // コンパイラバージョンを取得
    static std::string get_compiler_version();

    // キャッシュディレクトリのパスを取得
    std::filesystem::path cache_dir() const { return config_.cache_dir; }

   private:
    CacheConfig config_;

    // キャッシュディレクトリの初期化（なければ作成）
    bool ensure_cache_dir();

    // マニフェストファイルのパス
    std::filesystem::path manifest_path() const;

    // オブジェクトキャッシュディレクトリのパス
    std::filesystem::path objects_dir() const;

    // マニフェストを読み込み（JSON簡易パーサー）
    std::map<std::string, CacheEntry> load_manifest() const;

    // マニフェストを書き込み
    bool save_manifest(const std::map<std::string, CacheEntry>& entries) const;

    // ISO 8601形式の現在日時を取得
    static std::string current_timestamp();
};

}  // namespace cm::cache
