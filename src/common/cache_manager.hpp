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

// 高速キャッシュ判定用のファイル情報
struct QuickCheckFileInfo {
    std::string path;      // ファイルパス
    int64_t mtime_ns = 0;  // 最終更新時刻（ナノ秒）
    uintmax_t size = 0;    // ファイルサイズ
};

// 高速キャッシュ判定結果
struct QuickCheckResult {
    bool valid = false;       // 判定結果
    std::string fingerprint;  // 前回のフィンガープリント
    std::string object_file;  // キャッシュ済みオブジェクトファイル名
    std::string target;       // ビルドターゲット
};

// キャッシュエントリのメタデータ
struct CacheEntry {
    std::string fingerprint;                                 // 合成フィンガープリント
    std::map<std::string, std::string> source_hashes;        // ファイルパス → SHA-256
    std::map<std::string, std::string> module_fingerprints;  // モジュール名 → SHA-256
    std::string target;                                      // ビルドターゲット
    int optimization_level = 0;                              // 最適化レベル
    std::string compiler_version;                            // コンパイラバージョン
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

    // コンパイラバイナリ自体のSHA-256ハッシュを計算
    static std::string compute_compiler_hash();

    // 複数ファイル＋メタ情報から合成フィンガープリントを生成
    std::string compute_fingerprint(const std::vector<std::string>& source_files,
                                    const std::string& target, int optimization_level);

    // キャッシュからエントリを検索
    // フィンガープリントが一致し、オブジェクトファイルが存在すればキャッシュヒット
    std::optional<CacheEntry> lookup(const std::string& fingerprint);

    // 前回キャッシュとの差分を検出（変更されたファイル一覧を返す）
    std::vector<std::string> detect_changed_files(const std::vector<std::string>& current_files,
                                                  const std::string& target,
                                                  int optimization_level);

    // モジュール別フィンガープリントを計算
    // module_files: モジュール名 → そのモジュールに属するファイルパスのリスト
    std::map<std::string, std::string> compute_module_fingerprints(
        const std::map<std::string, std::vector<std::string>>& module_files);

    // 変更されたモジュールのみを返す
    // 前回のキャッシュエントリと比較し、フィンガープリントが変わったモジュールを検出
    std::vector<std::string> detect_changed_modules(
        const std::map<std::string, std::vector<std::string>>& module_files,
        const std::string& target, int optimization_level);

    // 2つのフィンガープリントマップを直接比較して変更モジュールを検出
    static std::vector<std::string> detect_changed_modules(
        const std::map<std::string, std::string>& prev_fingerprints,
        const std::map<std::string, std::string>& current_fingerprints);

    // コンパイル成果物をキャッシュに保存
    bool store(const std::string& fingerprint, const std::filesystem::path& object_file,
               const CacheEntry& entry);

    // ========== モジュール別キャッシュ API ==========

    // モジュール単位の .o ファイルをキャッシュに保存
    // fingerprint: 全体のフィンガープリント
    // module_name: モジュール名
    // module_fingerprint: モジュールのSHA-256フィンガープリント
    // object_file: コンパイル済み .o ファイルのパス
    bool store_module_object(const std::string& fingerprint, const std::string& module_name,
                             const std::string& module_fingerprint,
                             const std::filesystem::path& object_file);

    // キャッシュ済みモジュール .o ファイルを検索
    // module_fingerprint が一致するキャッシュ済み .o のパスを返す
    std::optional<std::filesystem::path> lookup_module_object(
        const std::string& module_name, const std::string& module_fingerprint);

    // 全キャッシュ済みモジュール .o を取得
    // Returns: モジュール名 → キャッシュ済み .o ファイルパスのマップ
    std::map<std::string, std::filesystem::path> get_cached_module_objects(
        const std::string& fingerprint);

    // ========== 統計・管理 ==========

    // キャッシュ統計を取得
    CacheStats get_stats() const;

    // 全エントリを取得（統計表示用）
    std::map<std::string, CacheEntry> get_all_entries() const;

    // キャッシュを全削除
    bool clear();

    // 古いエントリを削除（LRU的に、max_entriesを超える分を削除）
    void evict_old_entries();

    // コンパイラバージョンを取得
    static std::string get_compiler_version();

    // キャッシュディレクトリのパスを取得
    std::filesystem::path cache_dir() const { return config_.cache_dir; }

    // コンパイラバイナリのパスを設定（argv[0]から）
    static void set_compiler_path(const std::string& path);

    // ISO 8601形式の現在日時を取得
    static std::string current_timestamp();

    // ========== 高速キャッシュ判定 ==========

    // 入力ファイルのタイムスタンプ+サイズで高速にキャッシュヒットを判定
    // （ImportPreprocessor + SHA-256 計算をスキップする）
    QuickCheckResult quick_check(const std::string& input_file, const std::string& target,
                                 int optimization_level);

    // 高速判定用の情報を保存
    void save_quick_check(const std::string& input_file, const std::string& target,
                          int optimization_level, const std::string& fingerprint,
                          const std::string& object_file,
                          const std::vector<std::string>& source_files);

   private:
    static std::string compiler_path_;
    CacheConfig config_;

    // キャッシュディレクトリの初期化（なければ作成）
    bool ensure_cache_dir();

    // マニフェストファイルのパス
    std::filesystem::path manifest_path() const;

    // オブジェクトキャッシュディレクトリのパス
    std::filesystem::path objects_dir() const;

    // モジュール別キャッシュディレクトリのパス
    std::filesystem::path modules_dir() const;

    // マニフェストを読み込み（JSON簡易パーサー）
    std::map<std::string, CacheEntry> load_manifest() const;

    // マニフェストを書き込み
    bool save_manifest(const std::map<std::string, CacheEntry>& entries) const;

    // 高速判定ファイルのパス
    std::filesystem::path quick_check_path() const;
};

}  // namespace cm::cache
