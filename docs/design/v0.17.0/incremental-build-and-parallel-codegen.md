---
title: インクリメンタルビルド・並列コード生成・ICF・タイムアウトのプロセス分離
parent: v0.17.0 Design
---

# インクリメンタルビルド・並列コード生成・ICF・タイムアウトのプロセス分離

## 対象所見

| # | 領域 | 所見 | 状態 |
|---|------|------|------|
| H14 | コンパイル時間 | コード生成が単一スレッド・キャッシュ無し・関数数に対し超線形（6400関数でO2 6秒）、`compileModules`のキャッシュ経路は存在するが未接続 | 未着手 |
| M6 | コンパイル時間 | 30秒タイムアウト時にコード生成スレッドを`detach`するためGB級メモリを保持したまま残留し後続ビルドを劣化させる（safe_codegen.hpp:107,120） | 未着手 |
| M10 | ジェネリクス | レイアウト同一の特殊化（`pick__int`と`pick__uint`等）が統合されず全コード重複（ICF無し、コンパイル時間と生成物サイズが乗算的に膨張） | 未着手 |

なお監査が参照する`backend/llvm.cpp`の実パスは`src/cmd/cm/backend/llvm.cpp`である（トップレベル`Cm/backend/`は存在しない）。

## 背景と根本原因

### インクリメンタルビルド未接続（H14）

ドライバは常に空キャッシュで全体コンパイルを行う。

- src/cmd/cm/backend/llvm.cpp:171 で`codegen.compileWithModuleInfo(mir, {})`（第2引数`changed_modules_hint`に空`{}`）。
- 呼び出し先 src/internal/codegen/llvm/native/codegen.cpp:87-97 の`compileWithModuleInfo`は`(void)changed_modules_hint;`でヒントを捨て、`compile(program)`で全体を単一コンパイルし、空の`ModuleCompileInfo`を返す。コメント（codegen.cpp:85-86）が「現在モジュール分割コンパイルは無効化されている / 将来フロントエンド差分化が実装された際に再有効化予定」と明記。

一方、キャッシュ対応の経路は実装されているが呼ばれていない。

- src/internal/codegen/llvm/native/codegen.cpp:100-241 `compileModules(program, changed_modules, cached_objects, output_dir)`。MIRを`mir::MirSplitter::split_by_module`（src/internal/mir/mir_splitter.cpp:127）で分割し、`cached_objects.find(mod_name)`（codegen.cpp:120-123）でキャッシュ判定、ヒット時は`.o`再利用、ミス時のみ独立`LLVMContext`で個別コンパイルする。
- `compileModules`の参照は宣言（codegen.hpp:105）と定義（codegen.cpp:100）のみで、呼び出し元がソース全体でゼロ。差分・キャッシュ経路は死蔵している。

### 単一スレッド・超線形コード生成（H14）

MIR→LLVM変換は単一スレッドで、関数配列を逐次走査する複数のO(N)ループの積み重ね。

- src/internal/codegen/llvm/core/translate/program.cpp:312（シグネチャ宣言ループ）、:335（DFE用インデックス構築）、:412（DFEのBFS、各関数で全basic_blocks×statements走査）、:460-479（本体生成ループ`convertFunction`）。
- `compile()`（codegen.cpp:26-82）→`generateIR`（:51）→`MIRToLLVM::convert`の単一パスで、並列化なし。関数数増加に対し呼び出しグラフ探索・重複判定（`std::set<std::string> declaredFunctions`の文字列比較）を含め超線形に増大する。

### タイムアウト時のスレッドdetach（M6）

コード生成はタイムアウト監視付きの単一別スレッドで走り、タイムアウト時に`detach`する。

- src/internal/codegen/llvm/native/safe_codegen.hpp:63 `std::thread generation_thread([&]() { ... pass.run(module); ... })`（LLVM codegenを1本の別スレッドで実行）。
- タイムアウト定数 safe_codegen.hpp:28 `static constexpr auto DEFAULT_TIMEOUT = std::chrono::seconds(30);`。監視ループ（:97-127）が100msポーリング。
- タイムアウト時 safe_codegen.hpp:106-108 で`generation_thread.detach();`（コメント「リソースリークのリスクがある」「LLVMの内部ループから抜け出す他の方法がない」）。出力サイズ超過時（`MAX_OUTPUT_SIZE`=100MB、:27）も:119-121でdetach。
- detachされたスレッドはキャプチャした`buffer`/`module`を参照したまま動作継続し、GB級のメモリを保持したまま残留する。正常終了時のみ`join()`（:130-132）。

### ICF不在（M10）

同一レイアウトの特殊化がマージされない。

- 特殊化名生成 src/internal/mir/lowering/mono/typeinfo.cpp:89-111 `make_specialized_name`は`base + "__" + normalize_type_arg(arg)`で名前を作る（`pick__int`・`pick__uint`）。
- 重複判定はすべて「特殊化名の文字列一致」でのみ行われる（specialize.cpp:47-53の`generated.count(specialized_name)`、driver.cpp:82-83/99-100、program.cpp:311/465の`declaredFunctions`）。生成された本体（IR/レイアウト）のハッシュや構造比較でマージする処理は存在しない（`identical.code|code.folding|ICF|dedup`のgrepでゼロ件）。
- `int`と`uint`（同一32bitレイアウト）は`pick__int`と`pick__uint`という別名の別関数として2本生成され、コンパイル時間と生成物サイズが乗算的に膨張する。

## 設計方針

### 1. インクリメンタルビルドの接続（H14）

死蔵している`compileModules`のキャッシュ経路をドライバへ接続する。

- ドライバ（llvm.cpp:171）を`compileWithModuleInfo(mir, {})`から、モジュール分割 + キャッシュ照合を行う`compileModules`経路へ切替える。
- 変更モジュール判定: 前回ビルドのモジュールごとの入力ハッシュ（ソース + 依存の推移閉包）を保存し、変化したモジュールのみを`changed_modules`に載せる。
- オブジェクトキャッシュ: `cached_objects`（`std::map<std::string, std::filesystem::path>`）へ前回の`.o`パスを渡し、未変更モジュールは再リンクのみ行う。
- キャッシュ保存先はビルドディレクトリ配下（`.tmp`相当のビルドキャッシュ）とし、`.gitignore`へ追加する。
- モジュール分割は`MirSplitter::split_by_module`を利用（既存）。分割境界がモノモーフ化特殊化とどう対応するか（特殊化がどのモジュールに属すか）を定義する。

### 2. モジュール並列コード生成（H14）

`compileModules`が各モジュールを独立`LLVMContext`でコンパイルする構造（codegen.cpp:132-234）を、スレッドプールで並列化する。

- モジュール間は独立`LLVMContext`で共有状態を持たないため、モジュール単位の並列生成が安全。
- ワーカ数はコア数に合わせる（現状CPU使用率0.79コアの単一スレッドを解消）。
- 生成物は`ModuleObjectFile`（codegen.hpp:96-100、`from_cache`フラグ付き）としてリンク段へ集約。

### 3. ICF（同一コード折り畳み、M10）

同一レイアウト・同一IRの特殊化を1本へ折り畳む。

- 二段階で行う。
  - フロント側（安価）: モノモーフ化で、型引数がレイアウト等価（`int`/`uint`, `long`/`ulong`等の同ビット幅・同シグネチャ）な特殊化を1つの正準特殊化へ束ね、別名は正準名へのエイリアスにする。`make_specialized_name`（typeinfo.cpp:89）の直後にレイアウト正準化キーを導入する。
  - バックエンド側（網羅的）: リンク時にIRレベルのICF（同一本体のマージ）を有効化。LLVMの`MergeFunctions`パス、またはリンカのICF（`--icf`）を利用する。
- レイアウト等価判定は監査ロードマップ第2段「型ディスパッチの一元化」（elem_size等の網羅的switch共有ヘルパ）と同じ正準化基盤を使い、`int`/`uint`のケース漏れを防ぐ。

### 4. タイムアウトのプロセス分離（M6）

タイムアウト時にスレッドをdetachしてメモリを保持する現状を、子プロセス分離へ置換する。

- コード生成（`pass.run(module)`）を子プロセスで実行し、タイムアウト時は子プロセスを`kill`する。親プロセスはメモリを保持せず、子の全リソースはOSが即時回収する。
- safe_codegen.hpp:63の`std::thread` + :106-108/:119-121のdetachを、fork/exec（またはposix_spawn）ベースのワーカへ置換する。生成結果は一時ファイル or パイプで受け取る。
- これにより後続ビルドの劣化（GB級メモリ残留）を根絶する。タイムアウト値（30秒、safe_codegen.hpp:28）とサイズ上限（100MB、:27）はプロセス境界で強制する。

## 構文例・出力例

該当なし（コンパイラ内部の性能・ビルド基盤の変更であり、ユーザー向け構文・出力の変化は無い）。ビルドキャッシュ用のCLIオプション（例: キャッシュ無効化フラグ）を追加する場合は別途options.cppで定義する。

## 実装の段階分割

1. 第1段（M6、独立・低リスク）: コード生成のタイムアウトをスレッドdetachから子プロセス分離へ置換。メモリ残留を止める。
2. 第2段（M10フロント側）: モノモーフ化にレイアウト正準化キーを導入し、`int`/`uint`等の等価特殊化をエイリアス化。生成関数数を削減。
3. 第3段（M10バックエンド側）: リンク時ICF（MergeFunctions or リンカ`--icf`）を有効化し、残る同一本体を折り畳む。
4. 第4段（H14キャッシュ接続）: ドライバをllvm.cpp:171で`compileModules`経路へ切替え、モジュール入力ハッシュとオブジェクトキャッシュを実装。
5. 第5段（H14並列化）: モジュール単位のコード生成をスレッドプールで並列化。

各段は独立に効果を持ち、第1段（M6）だけでもビルド劣化を止められる。

## テスト計画（tests/common/配下）

性能・ビルド基盤の変更のため、機能回帰（挙動不変）を主眼にする。

- `tests/common/generics/layout_equal_specialization_test.cm` + `.expect`: `pick__int`と`pick__uint`が同一レイアウトでも正しい結果を返し、ICF後も値が壊れないことを全バックエンドで確認（M10の正当性保証。生成物削減自体は別途ベンチで測る）。
- `tests/common/generics/many_specializations_test.cm` + `.expect`: 多数の特殊化を持つプログラムがコンパイルを完走し、結果が正しいこと（並列化・ICFの回帰確認）。
- インクリメンタルビルドとタイムアウトのプロセス分離は、tests/common（バックエンドスイート）ではなくビルドドライバのregression/integrationで検証する（`compileModules`のキャッシュヒット/ミス経路、タイムアウト時に親メモリが解放されること）。
- ICFやキャッシュを有効化しても既存の全バックエンドスイート（jit/native/wasm/js/ts）が挙動不変で通ることを回帰の合格条件とする。

## リスクと非互換性

- インクリメンタルビルドのキャッシュ無効化漏れ（依存変更を検知できないと古い`.o`を使い誤ビルド）が最大のリスク。入力ハッシュに依存の推移閉包を含め、疑わしい場合は全体再ビルドへフォールバックする保守的設計にする。
- モジュール並列化は独立`LLVMContext`前提。グローバル状態（シンボルテーブル・declaredFunctions）を共有しないことを保証する必要がある。
- ICFのレイアウト等価判定を誤ると、レイアウトが実は異なる型（監査C8の`Foo__int`ユーザー構造体衝突等）を誤って折り畳む危険がある。正準化キーはビット幅・シグネチャ・フィールドレイアウトを厳密に含める。
- タイムアウトのプロセス分離はプラットフォーム依存（fork/posix_spawn）。wasm/jsバックエンドや非対応環境では従来のスレッド方式へフォールバックする。
- いずれもユーザー向けの構文・出力は不変。挙動が変わってはならない（性能・ビルド時間のみ改善）。

## 関連

- 監査レポート: docs/design/v0.17.0/large-scale-bottleneck-audit.md（H14, M6, M10、およびロードマップ第3段「スケール基盤」）
- ドライバ/キャッシュ経路: src/cmd/cm/backend/llvm.cpp:171, src/internal/codegen/llvm/native/codegen.cpp:87-97（未接続）, :100-241（compileModules）, codegen.hpp:84-108
- タイムアウト: src/internal/codegen/llvm/native/safe_codegen.hpp:28, :63, :106-121, :130-132
- 特殊化名/ICF不在: src/internal/mir/lowering/mono/typeinfo.cpp:89-111, specialize.cpp:47-53
- 逐次codegen: src/internal/codegen/llvm/core/translate/program.cpp:312, :460-479
- モジュール分割: src/internal/mir/mir_splitter.cpp:127
