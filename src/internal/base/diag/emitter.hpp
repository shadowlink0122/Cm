#pragma once

// ============================================================
// 診断エミッタ（表示の一元化。diagnostics-engine-unification 第1段）
// Parser/TypeCheckerが収集した診断の表示（source_map写像・参照ファイル読込・重大度ラベル）をここへ集約する。
// 従来はbuild.cpp/check.cppが同一の写像+ファイル読込コードを複製しており（X5）、check側の型検査診断はsource_map未適用で展開後の行番号を表示していた。
// ドライバは本クラスを構築してemit/emit_allを呼ぶだけにし、診断の表示ロジックを新規に手書きすることは禁止する
// ============================================================

#include "internal/base/diag/diagnostics.hpp"
#include "internal/base/source/location.hpp"
#include "internal/base/source/map.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace cm {

class DiagnosticEmitter {
   public:
    // code: 診断のSpanが指す（プリプロセス後の）ソース本文
    // input_file: 入力ファイルパス（source_map不在時の座標表示に使う）
    // source_map: プリプロセスのソースマップ（nullptrまたは空なら写像なしで表示）
    DiagnosticEmitter(const std::string& code, const std::string& input_file,
                      const SourceMap* source_map = nullptr);

    // 診断1件を表示する。labelを指定するとlintのレベル設定（error/warning/hint昇格）で重大度表記を上書きできる
    void emit(const Diagnostic& diag, const std::string& label = "");

    // 診断列を一括表示する
    void emit_all(const std::vector<Diagnostic>& diags);

    // 行無効化コメント判定など、プリプロセス後座標での問い合わせに使う
    const SourceLocationManager& location_manager() const { return loc_mgr_; }

   private:
    // source_mapが参照する元ファイル（import_chain含む）の内容を初回表示時に一括読込する
    void ensure_file_contents();

    static std::string severity_label(Severity severity);

    SourceLocationManager loc_mgr_;
    const SourceMap* source_map_;
    bool contents_loaded_ = false;
    std::unordered_map<std::string, std::string> file_contents_;
};

}  // namespace cm
