# モジュール解決とimport展開

Cmのimportは、コンパイルの最前段でモジュール名をファイルパスへ解決し、モジュール本体をソーステキストへインライン展開するプリプロセッサ方式で実現されている。
本文書は、モジュール名からパスへのマッピング規則、検索パスの構成と探索順、実行ファイルパス取得のプラットフォーム別実装、そしてテキスト展開方式の設計上の利点と注意点を記述する。

## 概要

importの解決には2つの層がある。

- `preprocessor::ImportPreprocessor`（src/internal/preprocessor/import.hpp:17）— コンパイルパイプラインの本流。import文を検出してモジュールファイルを探し、その内容をソーステキストへ再帰的に展開する。native/jit/その他すべてのバックエンドは、この展開後の単一ソースを字句解析から先の共通パイプラインで処理する。
- `module::ModuleResolver`（src/internal/module/resolver.hpp:26）— モジュール単位でパース済みHIR/MIRを保持するリゾルバ。モジュール名→`ModuleInfo`のキャッシュ（resolver.hpp:29）を持ち、`find_exported_function`（resolver.cpp:197）等でexport済みシンボルのMIRを直接引ける。プロセス全体でマジックスタティックの単一インスタンスを共有する（resolver.cpp:64-67）。

## データ構造とアルゴリズム

### モジュール名→パスのマッピング

基本規則は「`::`を`/`へ置換して`.cm`を付ける」である。`foo::bar`は`foo/bar.cm`になり、見つからなければ`foo/bar/mod.cm`へフォールバックする（src/internal/module/resolver.cpp:113-139、src/internal/preprocessor/module_resolve.cpp:20-50）。

```cpp
// src/internal/module/resolver.cpp:117-135
    // ネストされたモジュールの場合（例: "std::io" -> "std/io.cm"）
    std::string module_path = module_name;
    std::replace(module_path.begin(), module_path.end(), ':', '/');
    ...
        // mod.cmファイルも試す（例: "std/io/mod.cm"）
        auto mod_path = search_path / module_path / "mod.cm";
```

プリプロセッサ側の`resolve_module_path`（src/internal/preprocessor/module_resolve.cpp:115-411）はこれに加えて次の判定を行う。

- 相対パスimport（`./`・`../`）は現在ファイルの親ディレクトリ基準で解決し、`::サブモジュール`接尾辞を分離する（module_resolve.cpp:118-153）。
- 3セグメント以上（`std::mem::malloc`等）で末尾セグメントが小文字始まりの場合、まずフルパス`std/mem/malloc.cm`の実在を確認し、存在しなければ末尾を関数/変数名とみなして`std/mem.cm`を選択import扱いにする（module_resolve.cpp:185-229）。
- 2セグメント以上でルートディレクトリのエントリポイントへフォールバックする際は、サブモジュール（`std/io.cm`または`std/io/`）の実在を検証し、`import std::nonexistent::foo`が`std/mod.cm`へ誤解決されるのを防ぐ（module_resolve.cpp:288-314）。
- ディレクトリのエントリポイントは`find_module_entry_point`（module_resolve.cpp:413-450）が「`module`宣言を先頭10行以内に持つ.cmファイル → ディレクトリ同名の.cmファイル → mod.cm」の順で決定する。

### 検索パスの探索順

`ImportPreprocessor`のコンストラクタ（src/internal/preprocessor/import/setup.cpp:68-160）が検索パスを次の順で積む。相対パス解決を除き、常に「現在ファイルの親ディレクトリ → 検索パスリスト」の順で探索される（module_resolve.cpp:234-408）。

1. プロジェクトルート（`cm.toml`または`.git`を上方探索、無ければ環境変数`CM_PROJECT_ROOT`、最後にカレントディレクトリ。module_resolve.cpp:80-113）
2. カレントディレクトリ（プロジェクトルートと異なる場合。setup.cpp:77-80）
3. 環境変数`CM_STD_PATH`（setup.cpp:83-88）
4. 実行ファイル隣接の`libs`と、インストールレイアウト（`~/.cm/bin/cm` → `~/.cm/libs`）の`libs`（setup.cpp:92-105）
5. プロジェクトルート直下の`libs`（setup.cpp:108-111）
6. プラットフォーム別システムパス（macOS: `/usr/local/lib/cm/std`・`/opt/homebrew/lib/cm/std`・`~/.cm/std`、Linux: `/usr/lib/cm/std`等、Windows: `%LOCALAPPDATA%\Cm\std`等。setup.cpp:113-136）
7. 環境変数`CM_MODULE_PATH`（Unixは`:`区切り、Windowsは`;`区切り。setup.cpp:138-152）

`module::ModuleResolver`側はより簡素で、「カレントディレクトリ → 実行ファイル隣接の`std` → カレントディレクトリの`std` → `CM_MODULE_PATH`」の順である（src/internal/module/resolver.cpp:74-107）。

### 実行ファイルパス取得のプラットフォーム別実装

検索パス4と`std`解決のため、実行ファイルのディレクトリを`get_executable_directory`が取得する。実装はsetup.cpp:40-66とresolver.cpp:33-59の2箇所に同型で存在する。

```cpp
// src/internal/preprocessor/import/setup.cpp:40-66（要約）
#ifdef __APPLE__
    _NSGetExecutablePath(path, &size)      // mach-o/dyld.h
#endif
#ifdef __linux__
    readlink("/proc/self/exe", path, ...)  // unistd.h
#endif
#ifdef _WIN32
    GetModuleFileNameA(NULL, path, MAX_PATH)  // windows.h
#endif
    return {};  // フォールバック: 空のパス
```

いずれも失敗時は空パスを返し、呼び出し側は該当検索パスの追加をスキップするだけで探索自体は続行する（setup.cpp:92-105）。
これにより、リポジトリ外の任意のディレクトリからインストール済みバイナリを実行しても`std::*`が解決できる。

### importのテキスト展開

`process`（setup.cpp:166-242）が入口で、実処理は`process_imports`（src/internal/preprocessor/import/expand.cpp:24）が行単位でソースを走査する。

- import文の検出は正規表現ではなく高速な文字列判定で行い、複数行にまたがるimport文（`{`と`;`の追跡）にも対応する（expand.cpp:60-120）。`export import X;`の再export行もimportとして展開する（expand.cpp:72-98）。
- 解決したパスは`std::filesystem::canonical`で正規化し（expand.cpp:406）、`import_stack`との照合で循環依存を検出してimportチェーン付きのエラーを出す（expand.cpp:408-430）。
- モジュール本体は再帰的に`process_imports`へかけ、展開結果を`module_cache`、展開前の生ソースを`raw_module_cache`、exportキーワード除去済みを`processed_module_cache`に保持して再読込を避ける（import.hpp:58-62、expand.cpp:528-553）。
- 出力の各行は`emit_line`/`emit_source`（expand.cpp:42-57）を通り、`SourceMap`（展開後の行番号→元ファイル・元行番号・importチェーン。import.hpp:20-27）を同時に構築する。ファイル切り替わり位置から`ModuleRange`を再構成し（setup.cpp:199-235）、後段の診断が元ファイルの位置で報告できるようにする。
- 展開形態はimportの種類で変わる。エイリアスimportは`namespace 別名 { ... }`でラップし（expand.cpp:602-609）、選択import・from構文はラップせず直接展開し（expand.cpp:610-691）、ワイルドカードはマーカーコメント付きで全体を展開し（expand.cpp:692-723）、通常importはモジュール宣言由来のnamespaceでラップする（expand.cpp:724-819）。
- 参照した全ファイルの正規化パスは`resolved_files`としてビルドキャッシュのフィンガープリントに供給される（setup.cpp:189-197）。

## 実装箇所

| ファイル | 役割 |
|---|---|
| src/internal/preprocessor/import.hpp | `ImportPreprocessor`の宣言。キャッシュ・ソースマップ・`ImportInfo`の定義 |
| src/internal/preprocessor/import/setup.cpp | 検索パス初期化・実行ファイルパス取得・`process`エントリポイント・`ModuleRange`再構成 |
| src/internal/preprocessor/import/parse.cpp | import文のパース（エイリアス・選択項目・from構文・ワイルドカード） |
| src/internal/preprocessor/import/expand.cpp | import文の再帰展開・namespaceラップ・循環依存検出・ソースマップ生成 |
| src/internal/preprocessor/module_resolve.cpp | モジュール名→ファイルパス解決・プロジェクトルート検出・エントリポイント探索 |
| src/internal/module/resolver.cpp | HIR/MIR層の`ModuleResolver`（モジュールキャッシュ・export関数/構造体の検索） |
| src/cmd/cm/build.cpp | コンパイル本流からの呼び出し（build.cpp:178-183） |

## 落とし穴とケア

- **テキスト展開方式の利点**: 展開後は単一ソースになるため、字句解析以降のパイプラインとnative/jitを含む全バックエンドがモジュールの存在を意識せずに済む。モジュール境界をまたぐジェネリクスのモノモーフ化やインライン化も、通常の単一ファイルコンパイルと同一経路で機能する。
- **テキスト展開方式の注意点**: 処理は行単位の字句的なテキスト操作であり、ASTを持たない。複数行宣言・コメント・文字列リテラル内の誤検出を避けるため、判定は`code_portion`等の正規化ヘルパ（src/internal/preprocessor/import_internal.hpp）を経由させる必要がある。新しい構文をimport/export検出に足す際は、この層がパーサより脆いことを前提に負テストを用意すること。
- **診断位置の維持**: 展開でソースの行番号が変わるため、`SourceMap`の構築（expand.cpp:42-57）を経由しない出力を追加してはならない。素の`result <<`で行を足すとソースマップと行がずれ、以降すべての診断位置が壊れる。
- **パス同一性はcanonicalパスで判定する**: 同一ファイルが相対パスと検索パスの両経路で見つかるため、重複排除・循環検出の鍵は必ず`std::filesystem::canonical`の結果を使う（expand.cpp:406）。生の指定文字列を鍵にすると二重展開が再発する（詳細は[visibility-and-dedup.md](visibility-and-dedup.md)）。
- **末尾セグメントの解釈はファイル実在で確定する**: `std::sync::mutex`のように小文字始まりでもモジュールでありうるため、「小文字=関数名」と即断せずフルパスの実在確認を先に行う（module_resolve.cpp:189-229）。この順序を崩すとサブモジュールの直接importが壊れる。
- **回帰テスト**: tests/common/modules/ 配下（`hier_import`・`relative_import`・`selective_import`・`recursive_wildcard`・`std_io_import`等）が解決規則ごとの実行テストを持ち、`make test-interpreter`/`make test-llvm`等のバックエンドスイートで全数実行される。

## 関連資料

- [visibility-and-dedup.md](visibility-and-dedup.md) — export可視性の強制と多重importの重複排除
- [../../archive/v0.17.0/module-visibility-and-import-dedup.md](../../archive/v0.17.0/module-visibility-and-import-dedup.md) — 可視性・重複排除の設計文書（実装済みアーカイブ）
- src/internal/preprocessor/conditional.cpp — import展開後に走る条件付きコンパイル（`#ifdef`等）
