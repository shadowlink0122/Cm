# アーキテクチャドキュメント

Cmコンパイラの実装方法を、実装に使われている基本アルゴリズムとバグを防ぐための技術ごとに記述した文書群。対象はnative/jitバックエンドに関わる実装で、js/sv/wasmは共有コードの境界説明でのみ言及する。各文書は実ソースの`パス:行`を引用しており、対応する設計経緯は[アーカイブ](../archive/)の各設計文書を参照。
全Cm構文のO3でのLLVM IR変換対訳は[構文→LLVM IR対訳リファレンス](codegen/index.md)（10カテゴリ35文書）を参照。

## pipeline — コンパイルパイプライン

| ドキュメント | 内容 |
|---|---|
| [コンパイルパイプライン全体像](pipeline/overview.md) | lexer→parser→AST→型検査→HIR→MIR→LLVM IR→native/jitの段構成と、ドライバ（main.cpp/build.cpp/backend）からの呼び出し流れ |
| [MIRの設計](pipeline/mir-design.md) | MirFunction・locals・基本ブロック・terminatorの表現、最適化パスの種類と実行順、O0/O1/O2の差、パス追加時の不変条件 |
| [MIR最適化パスの全カタログ](pipeline/mir-optimization-passes.md) | 標準パイプライン12+単独駆動4+休眠1の全17パスを、目的・アルゴリズム・実行条件・維持すべき不変条件付きで1パス1節で列挙 |
| [属性の処理](pipeline/attributes.md) | `#[target]`・`#[test]`・`#[derive]`等のパースと消費フェーズの分散設計（パース直後フィルタ・型検査・JITランナー・derive生成） |
| [条件付きコンパイル](pipeline/conditional-compilation.md) | プリレックス行ベースの`#ifdef`フィルタ（空行置換による行番号保存）、組み込みシンボル3系統とホスト由来性、`#[target]`属性との棲み分け |

## lowering — 言語機能の脱糖

| ドキュメント | 内容 |
|---|---|
| [クロージャのlowering](lowering/closures.md) | ラムダの独立関数化とキャプチャ前置、高階ランタイム向けのi64環境配列+サンク合成、`__lambda_`命名規約の役割 |
| [enumとmatchのlowering](lowering/enums-and-match.md) | `{i32 tag, [N x i8] payload}`のタグ付きunionレイアウト、`__tag`比較+`__payload`抽出への脱糖、網羅性検査 |
| [メソッドチェーンの処理](lowering/method-chains.md) | 後置式の左結合パース、チェーン各段の型伝播とレシーバ参照渡し・一時実体化・書き戻し、`return self`のデリファレンス、連結チェーン平坦化 |
| [FFI（use libc / extern "C"）](lowering/ffi-extern.md) | 2つの宣言形式のパースとHirExternBlock、マングリング除外と宣言のみのllvm::Function発行、native=リンカ/jit=ホストプロセスの2経路シンボル解決 |
| [インラインアセンブリ](lowering/inline-asm.md) | `${制約:変数}`記法の`$N`番号化とAsmData解決、GCC互換制約の並べ替え・clobber自動付与・レジスタ非リマップ方針によるllvm::InlineAsm変換 |

## macro — マクロ

| ドキュメント | 内容 |
|---|---|
| [マクロの展開](macro/expansion.md) | 型付きマクロ（定数のHIRインライン置換・関数マクロのパース時関数化）の2段階処理と、ビルド未接続のトークンツリー展開器（matcher/expander/hygiene）の位置づけ |

## types — 型システム基礎

| ドキュメント | 内容 |
|---|---|
| [asキャスト](types/casts.md) | 型検査段の許可規則（数値→string拒否・縮小警告）、HIR/MIRのCast表現とtypedef解決・配列decay、native/jit共有のキャスト命令選択、チェーンキャスト |
| [ユニオン型](types/union-types.md) | `UnionType`の変種リスト表現と`{i32 tag, payload}`ランタイムレイアウト、`is`・型ガード・match型パターンの脱糖、タグ検査付き取り出し、enumタグ付きunionとの違い |
| [型推論](types/inference.md) | 式ボトムアップ局所推論、リテラル型付けと暗黙拡幅、三項・matchの分岐合流昇格、ジェネリクスの実引数単一化、`expr.type`一回決定境界 |

## generics — ジェネリクス

| ドキュメント | 内容 |
|---|---|
| [単相化（モノモーフィゼーション）](generics/monomorphization.md) | 不動点反復による特殊化生成と、`$`区切り可逆型キーによるネスト型引数（`Box<Pair<int,string>>`等）の一意識別 |
| [シンボルマングリング](generics/mangling.md) | `Struct__method`・モジュール修飾名・特殊化サフィックスのキー空間設計と、単一シンボルテーブルによる衝突のハードエラー化 |
| [インスタンス化の診断](generics/instantiation-diagnostics.md) | 個数不一致・未定義型引数・引数なし使用・明示型引数不一致の検出と、宣言時の演算子境界検査 |

## interface — インターフェイス

| ドキュメント | 内容 |
|---|---|
| [静的ディスパッチ](interface/static-dispatch.md) | ジェネリック境界（`<T: Trait>`）経由の呼び出しを単相化でモノモーフィックに解決する仕組み、演算子正規化、deriveの自動実装生成 |
| [動的ディスパッチ](interface/dynamic-dispatch.md) | fat pointer（データ+vtable）表現、vtable生成、構造体フィールド・配列・スライス要素へ格納する際のfat pointer構築 |

## memory — メモリ管理

| ドキュメント | 内容 |
|---|---|
| [dropパスと所有権](memory/drop-and-ownership.md) | デストラクタ挿入の5系統（スコープ終端・ループ毎周期・break/continue・return・関数終端）、コンパイラ一時の解放、move後使用の診断 |
| [アロケータ](memory/allocator.md) | `cm_mem_*`経路への一本化と`set_allocator_fns`ファサード、確保・解放ペア不一致というバグクラスの防止 |
| [集約コピーのlowering](memory/aggregate-copy.md) | 閾値超の集約コピーのmemcpy化、大構造体戻り値のsret化、大配列引数のポインタ渡しによるコンパイル時間・スタック爆発の防止 |

## strings — 文字列

| ドキュメント | 内容 |
|---|---|
| [ランタイム表現](strings/representation.md) | char*互換SDSヘッダ方式によるO(1)バイト長・埋め込みNUL保持、連結・比較のlowering、C FFI互換の維持 |
| [UTF-8コードポイント処理](strings/utf8.md) | len()のコードポイント数化とbyte_lenの分離、substring/indexOf等のコードポイント添字統一、バイトAPIとの二層構造 |
| [StringBuilder](strings/stringbuilder.md) | 容量倍増バッファによる償却O(1) append、ハンドルをint64_t固定とするABI設計、素朴な`+`連結との使い分け |

## slices — スライス・動的配列

| ドキュメント | 内容 |
|---|---|
| [ランタイム表現と要素型ディスパッチ](slices/runtime-representation.md) | CmSliceヘッダ構造と操作関数群、slice_dispatch.hppへの要素型ディスパッチ一元化によるelem_size不一致ヒープ破壊の防止 |
| [境界検査](slices/bounds-checking.md) | 既定無検査+`--sanitize=bounds`の二段構え、MIR計装パスとLLVM BoundsCheckingPassの補完関係 |
| [チェーンレシーバの解決](slices/chain-receiver.md) | `m[0].push(x)`・`make().len()`のレシーバ場所化と書き戻し、黙った欠落を許さない診断 |

## codegen-native — nativeコード生成

| ドキュメント | 内容 |
|---|---|
| [MIR→LLVM IR変換](codegen-native/mir-to-llvm.md) | MIRToLLVMの構成、関数IDマングリングとmain/cm_*/ラムダ/externの特別扱い、型マッピングとsret変換の不変条件 |
| [オブジェクトファイル出力](codegen-native/object-emission.md) | TargetMachine構成（arm64/x86_64・クロス書き換え）、直接オブジェクト出力、fork隔離+タイムアウトでLLVMクラッシュから本体を守る設計 |
| [リンクとランタイム解決](codegen-native/linking-and-runtime.md) | プラットフォーム別リンカコマンド、cm_*プレフィックス走査による必要ランタイムの自動検出、cm_runtime.oの探索順 |
| [数値出力とキャストの一貫性](codegen-native/numeric-and-casts.md) | 最短round-trip数値書式化、`as`キャストの意味論（飽和・切り捨て・ptr↔int）、共通実装への集約 |
| [LLVM最適化の構成](codegen-native/llvm-optimization.md) | native/jit双方のPassBuilderパイプライン構築とO0〜O3写像、MergeFunctions ICF、サニタイザ計装、最適化暴走への3層防御、verifyModuleの配置 |
| [スライスと配列のコード生成](codegen-native/slice-and-array-codegen.md) | スライス=opaque ptr+cm_slice_*呼び出し・固定長配列=[N x elem]+インラインGEPの二分方針、リテラル生成・添字GEP・サブスライス参照・for-in展開のIR |
| [非同期ランタイムとイベントループ](codegen-native/async-event-loop.md) | async/awaitフラグがnative/jitではMIR検証で拒否される設計、未リンクの準備実装（Future・Executor・kqueue/epoll）の位置づけ、実際の並行処理を担うnative::thread/sync |
| [printと文字列補間](codegen-native/print-and-interpolation.md) | 補間の分解→プレースホルダ変換→型ディスパッチ（cm_format_replace_*呼び分け・unionタグ分岐）の三段構成、Display/Debug自動実装の再帰的文字列化 |
| [最適化レベルO0/O3の生成過程と実差分](codegen-native/optimization-levels.md) | O0の完全素通しとO3の二段最適化（MIR式整理→LLVM mem2reg・インライン化・ループ閉形式評価）を、実機IRダンプの対比で記述 |

## codegen-jit — JIT実行

| ドキュメント | 内容 |
|---|---|
| [LLJITエンジン](codegen-jit/lljit-engine.md) | ORC/LLJITによるインメモリ実行、ホストプロセスからのシンボル解決、テストごとの独立エンジンによる状態隔離 |

## modules — モジュールシステム

| ドキュメント | 内容 |
|---|---|
| [モジュール解決](modules/import-resolution.md) | モジュール名→パスのマッピングと検索パス構成、実行ファイルパス取得、SourceMap付きテキスト展開方式 |
| [可視性と多重import対策](modules/visibility-and-dedup.md) | export可視性の強制、canonicalパス鍵による二重展開防止と`__cm_priv_`改名、同名衝突の診断 |

## diagnostics — 診断

| ドキュメント | 内容 |
|---|---|
| [メッセージのi18n構成](diagnostics/i18n-messages.md) | `kMessages[MsgId][Lang]`2次元テーブルへの全メッセージ集約、`{0}`プレースホルダ書式、断片連結を禁止する理由 |
| [正しさ検査のlint群](diagnostics/correctness-lints.md) | 確定代入解析・return網羅検査・use-after-move診断、エラーコード番号帯の体系、--strictでのエラー昇格方針 |

## build — ビルド基盤

| ドキュメント | 内容 |
|---|---|
| [インクリメンタルビルドと並列コード生成](build/incremental-and-parallel-codegen.md) | モジュール分割コード生成、内容アドレスキャッシュキー設計、ワーカ並列と原子的書き込み、コンパイル時間の超線形化対策 |
