# export可視性の強制と多重importの重複排除

importのテキスト展開（[import-resolution.md](import-resolution.md)）は、そのままでは「非exportシンボルが素通しで公開される」「同一ファイルの二重展開」「同名シンボルの黙った先勝ち解決」という3種のバグを生む。
本文書は、これらを防ぐためのexport可視性の強制、canonicalパスを鍵とする重複排除、生ソース収集+パス鍵伝搬による非exportヘルパーの改名、および同名衝突診断の設計を記述する。

## 概要

方針は「preprocessor層でファイル同一性と可視性を確定させ、型検査層は最終防壁として衝突検出のみを担う」である。

- 選択import（`import mod::{items}`）は`filter_exports`（src/internal/preprocessor/export/extract.cpp:21）でexport宣言だけを抽出する。型定義（struct/enum/typedef/const）はメソッド解決とimpl残置のため透過的に保持する。
- 非export自由関数の選択importは診断対象で、check/lintでは常に、コンパイルでは`--force-check`/`--strict`指定時に警告する（src/cmd/cm/check.cpp:82、src/cmd/cm/build.cpp:179-182）。
- 同一ファイルの多重import（選択・ワイルドカード・全体・間接の任意の組み合わせ）はcanonicalパス単位で本体展開を1回に抑える。
- 異なるモジュールから同名の非修飾シンボルを取り込むimportは、先勝ちで黙殺せずエラーにする（`exposed_symbols_`表）。
- 非exportヘルパー関数は`__cm_priv_<stem>_<連番>_<名前>`へ改名され、export関数経由の内部呼び出しは追従しつつ、ユーザーコードからの非修飾参照だけが解決不能になる。

## データ構造とアルゴリズム

### 選択importのフィルタリングと型の透過

`filter_exports`（extract.cpp:21-328）は行単位でexportブロックを分類し、`import_items`に含まれるものだけを出力する。
事前スキャンで「フィルタ後も出力に残る型名」（`kept_types`）を集め、非export struct/enumでもimplが対象型を参照している限り一緒に残す（extract.cpp:35-64）。これを怠ると型は使えるのに`.method()`が解決できなくなる。
ネストしたimport展開領域（`===== Selective import from ... =====`等のマーカー内）は、そのモジュール自身のexportではないため取捨選択せず素通しする（extract.cpp:66-98）。
同一ファイルからの2回目以降の選択importは増分モード（`incremental=true`）で処理し、初回展開で出力済みのネストimport領域・非export型/implを再出力せず、新規要求されたexportシンボルとその型のimplだけを出力する（import.hpp:113-119、extract.cpp:39-41、79-97）。

### 非exportシンボルの検出（選択import時の診断）

`find_non_exported_function_items`（extract.cpp:334-460）は展開済みテキストではなく元のモジュールソースをトップレベル（波括弧深度0）だけ走査し、選択importで明示指定されたアイテムのうち非export関数として定義されているものを返す。
インラインexportに加えて名前列挙形式`export { a, b };`もexport済みとして記録し（extract.cpp:397-412）、同名がexport定義としても存在する場合は警告しない保守的判定を取る（extract.cpp:331-333）。
検出結果は展開時に`ImportNonExportedSymbol`メッセージで警告される（src/internal/preprocessor/import/expand.cpp:558-571、本文はsrc/internal/base/messages/messages.cpp:912-914）。
型定義（struct/enum/typedef/const）の透過は仕様として維持するため対象は自由関数に限る（import.hpp:93-96）。

### 非exportヘルパーの改名（生ソース収集+パス鍵伝搬）

export情報が完全に残るのはファイル読み込み直後の生ソースだけであり、加工済みテキストでの分類は不可能である。
そこで`load_module_file`（src/internal/preprocessor/module_resolve.cpp:52-78）が読み込み時点で非exportトップレベル関数名を収集し、weakly_canonicalなパスを鍵に`module_internal_fns_`（import.hpp:123）へ「(改名プレフィックス, 関数名リスト)」として保持する。

```cpp
// src/internal/preprocessor/module_resolve.cpp:64-76（要約）
    auto canonical = std::filesystem::weakly_canonical(module_path, ec);
    const std::string key = ec ? module_path.string() : canonical.string();
    if (module_internal_fns_.find(key) == module_internal_fns_.end()) {
        std::string prefix = module_path.stem().string();  // 非英数は'_'へ
        prefix += "_" + std::to_string(module_internal_fns_.size());
        module_internal_fns_[key] = {prefix, collect_non_export_function_names(content)};
    }
```

- `collect_non_export_function_names`（extract.cpp:511-627）はリストexport（コメント混在の複数行`export { a, b };`を含む）で公開された名前・`main`/`efi_main`・既に`__cm_priv_`で始まる名前を除外する。
- `rename_internal_functions`（extract.cpp:629-657）は単語境界一致で`__cm_priv_<prefix>_<名前>`へ一貫改名する。改名結果は元名を`_`付きで含むため再適用しても変化しない（冪等）。
- 適用は展開断片のemit地点すべて（選択import: expand.cpp:683、ワイルドカード: expand.cpp:709-720、通常import: expand.cpp:810）で`apply_internal_fn_renames`（extract.cpp:659-669）を通して行い、未登録パスは素通しする。

これにより、namespace外へ複製されたヘルパーが非修飾グローバル名で公開されてカプセル化を破るクラスのバグを防ぐ。

### 同一ファイルの重複排除（canonicalパス鍵）

`process_imports`は2つの重複排除表を持ち、いずれもcanonicalパスを鍵にする（expand.cpp:406、466-510）。

- 選択import: `imported_symbols[canonical_path]`にシンボル名単位で登録し、新規シンボルが無ければ展開せずスキップ、有れば増分モードで新規分のみ出力する（expand.cpp:467-496、573-585）。
- ワイルドカード・全体import: `imported_modules`（canonicalパス集合）で2回目以降を丸ごとスキップする（expand.cpp:497-510）。

`export import X;`（再export）もimportとして展開されるため、mod.cm経由の間接importと直接importが混在しても本体は1回しか出力されない（expand.cpp:72-98）。
この設計が防ぐのは「`import std::collections::Vector;`の後の`import std::json;`が内部で`std::collections`を全体importして同一implを二重展開し、型検査の`Duplicate method`例外で落ちる」クラスのバグである。

### 同名シンボルの衝突診断（黙った先勝ちの防止）

トップレベルの選択importで公開される非修飾名は、`exposed_symbols_`（公開名→由来モジュールのcanonicalパス。import.hpp:54-56）へ登録される。
同名が別パスから登録されようとしたら`ImportDuplicateSymbol`でエラーにする（expand.cpp:436-464、本文はmessages.cpp:909-911）。

```cpp
// src/internal/preprocessor/import/expand.cpp:448-462（要約）
    auto ex_it = exposed_symbols_.find(exposed_name);
    if (ex_it != exposed_symbols_.end() && ex_it->second != canonical_path) {
        error << i18n::msgf(i18n::MsgId::ImportDuplicateSymbol, exposed_name,
                            rel(ex_it->second), rel(canonical_path));
        throw std::runtime_error(error.str());
    }
    exposed_symbols_[exposed_name] = canonical_path;
```

エイリアス付きアイテム（`import mod::{x as y}`）は公開名（エイリアス）で判定し、同一モジュールの再importは許容する。
エイリアスの実体は展開断片内の識別子を単語境界で改名することで実現し、断片内の自己再帰・相互参照も一貫して追従する（expand.cpp:651-679）。
この診断が無いと、型検査の`Scope::define_function`が既存名で黙って`false`を返すため（src/internal/types/scope.cpp:30-33）、大規模プロジェクトで後からimportした同名関数が無診断で捨てられ、先に定義された方が黙って呼ばれる。

### 型検査層の最終防壁

preprocessor修正後に残る真の重複は型検査が捕捉する。

- implメソッドの重複は`register_impl`が`throw std::runtime_error("Duplicate method: ...")`で停止する（src/internal/types/checking/decl.cpp:729）。これを「黙ってスキップ」に緩めるとメソッドテーブルの整合が崩れ、下流で`not a function`に化けることが確認されているため緩めてはならない。
- マングル名の単一シンボルテーブル`mangled_symbols_`が、メソッド・自由関数・モジュール修飾名の別由来・別シグネチャ衝突を検出する（src/internal/types/checking/checker.hpp:253-263）。

## 実装箇所

| ファイル | 役割 |
|---|---|
| src/internal/preprocessor/export/extract.cpp | `filter_exports`・非export関数の検出/収集・`__cm_priv_`改名・exportブロック抽出 |
| src/internal/preprocessor/export/rewrite.cpp | export構文の書き換え（名前列挙・namespace export・暗黙impl export） |
| src/internal/preprocessor/import/expand.cpp | 重複排除表・衝突診断・エイリアス改名・改名適用のemit地点 |
| src/internal/preprocessor/module_resolve.cpp | `load_module_file`での非exportヘルパー名収集（生ソース時点） |
| src/internal/preprocessor/import.hpp | `imported_symbols`/`imported_modules`/`exposed_symbols_`/`module_internal_fns_`の宣言 |
| src/cmd/cm/check.cpp・src/cmd/cm/build.cpp | `set_warn_non_exported`の有効化条件（check/lintは常時、buildは`--force-check`/`--strict`） |
| src/internal/types/scope.cpp・src/internal/types/checking/decl.cpp | 最終防壁（`define_function`の重複拒否・`Duplicate method`検出） |

## 落とし穴とケア

- **不変条件: 本体展開はcanonicalパスにつき1回**。新しいimport形態を追加する場合も、判定鍵は必ず`std::filesystem::canonical`の結果を使い、`imported_symbols`/`imported_modules`のどちらかへ登録すること。生の指定文字列を鍵にすると、相対パスと検索パス経由の同一ファイルが二重展開され`Duplicate method`が再発する。
- **不変条件: 改名は冪等**。`__cm_priv_`改名は複数のemit地点で重複適用されうるため、改名結果が再度改名対象にならないこと（`__cm_priv_`始まりの除外。extract.cpp:610）を維持する。
- **可視性強制は生ソースで判定する**。展開・フィルタ後のテキストにはexport情報が残らないため、非export判定を加工済みテキストへ移してはならない（`raw_module_cache`が生ソースを保持する理由。expand.cpp:525-545）。
- **型定義の透過を壊さない**。struct/enum/typedef/constの透過は仕様であり、可視性強制の対象を自由関数から広げる場合は`kept_types`のimpl保持ロジック（extract.cpp:35-64）との整合を先に検証する。
- **選択的再export（`export import x::{items}`）は展開せず素通しを維持する**。展開するとMIR組み込み（println等）の意味論を壊すことが確認されている。
- **非export警告のエラー化は段階導入**。現在は警告であり（messages.cpp:913の文言どおり将来エラー化予定）、昇格時は標準ライブラリとtests/commonの全数調査を先に行う。
- **回帰テスト**: tests/common/modules/selective_import_dedup.cm（M7の二重展開防止）、tests/common/modules/selective_import_alias.cm（アイテムエイリアス）、tests/i18n/dup_import/（同名衝突診断）、tests/i18n/non_export_import/・tests/i18n/non_export_helper/（可視性警告と内部改名）、tests/common/errors/err_duplicate_impl.cm・err_duplicate_method.cm（最終防壁）。

## 関連資料

- [import-resolution.md](import-resolution.md) — モジュール解決とテキスト展開の全体像
- [../../archive/v0.17.0/modules/module-visibility-and-import-dedup.md](../../archive/v0.17.0/modules/module-visibility-and-import-dedup.md) — 本設計の背景・根本原因・段階分割を記録した設計文書
- [../../archive/v0.17.0/modules/mangling-collision-detection.md](../../archive/v0.17.0/modules/mangling-collision-detection.md) — マングル名の単一シンボルテーブルによる衝突検出
