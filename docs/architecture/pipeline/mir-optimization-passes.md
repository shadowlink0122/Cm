# MIR最適化パスカタログ

本書はCmコンパイラのMIRレベルで動作する全パスを1パス1節で列挙し、それぞれの目的・アルゴリズム・実行条件・維持すべき不変条件を実装コードに即して記述する網羅カタログである。
パスは大きく3群に分かれる: `create_standard_passes`（`src/internal/mir/passes/core/manager.cpp:25-77`）が登録する標準パイプラインのパス群、ドライバ `src/cmd/cm/build.cpp` やLLVMバックエンドが単独駆動するパス群、そして実装済みだがどこにも登録されていない休眠パスである。
パイプラインの実行機構（`OptimizationPipeline::run_until_fixpoint` と収束判定）とO0/O1/O2の反復回数差の概観は [MIRの設計](mir-design.md) が単一情報源であり、本書では繰り返さない。

## パス一覧

| パス（クラス/関数） | 目的 | 実行条件 |
|---|---|---|
| `ConstantLoopUnroll` | 定数トリップカウントループの静的展開 | `--funroll-loops` 指定時（O0でも）、svターゲットは常時 |
| `StringReassignFree` | 文字列再代入時の旧バッファ解放 | O1以上のパイプライン先頭、O0はドライバが直接実行（健全性パス） |
| `SparseConditionalConstantPropagation` | 到達可能性を考慮した大域定数伝播とCFG簡約 | O1以上 |
| `ConstantFolding` | ブロック内定数畳み込み・恒等式簡約・SwitchInt定数化 | O1以上（O2以上で2回目）、svはO1以上で保存モードのみ |
| `GVN` | ブロック内共通部分式除去 | O1以上 |
| `CopyPropagation` | ブロック内コピー伝播 | O1以上（O2以上で2回目、js/ts/webは集約抑止） |
| `DeadStoreElimination` | 同一ブロック内の上書きされるストア除去 | O1以上 |
| `SimplifyControlFlow` | 到達不能ブロック除去・直列ブロック統合・空ブロックバイパス | O1以上 |
| `FunctionInlining` | 小関数のインライン展開（現在は休眠） | O1以上に登録（実質作動せず） |
| `TailCallElimination` | 自己末尾呼び出しへの `is_tail_call` マーク付け | O1以上 |
| `LoopInvariantCodeMotion` | ループ不変式のpre-headerへの巻き上げ | O1以上 |
| `DeadCodeElimination` | 到達不能ブロック・未使用ストア・Nopの除去 | O1以上（O2以上で2回目）、Compile時は関数単位でO0でも実行 |
| `ProgramDeadCodeElimination` | 未使用関数・未使用構造体のプログラム単位削除 | Compile時のみ（sv除く）、最適化レベル非依存 |
| `UndefinedCheckInstrumentation` | ゼロ除算・null参照ガードの計装 | `--sanitize=undefined` 時に最適化後1回 |
| `BoundsCheckInstrumentation` | スライスアクセスの境界検査計装 | `--sanitize=bounds` 時に最適化後1回（sv除く） |
| `NoStdChecker` | ベアメタルターゲットでのOS依存呼び出し検証 | baremetal系ターゲットのLLVM出力前に常時 |
| `ArrayBaseExtraction` | 多次元配列アクセスのベースオフセット抽出 | 未登録（休眠パス） |

## 標準パイプラインのパス

### ConstantLoopUnroll

- 実装: `src/internal/mir/passes/loop/const_unroll.cpp:332`（`run`）、`const_unroll.cpp:352`（`try_unroll_one`）、宣言は `const_unroll.hpp:17-38`。
- 目的: 初期値・境界・増分がすべて定数のループを、ループ構造を持たない直列ブロック列へ静的展開する。svバックエンドではgenerate/genvar相当の繰り返し構造生成を担う。
- アルゴリズム: 反復ごとに到達可能集合と支配木（`DominatorTree`）を再計算し、SwitchInt終端を持つヘッダとバックエッジ（ヘッダが支配する先行ブロック）から自然ループを同定する（`const_unroll.cpp:373-414`）。横入り辺のないreducibleなループのみ対象とし、ヘッダの遷移先が本体入口と出口の各1つ、ラッチがヘッダへの無条件Gotoであることを検査する（`const_unroll.cpp:416-474`）。ヘッダの条件式を抽象値 `BlockValue` の連鎖解決で「誘導変数 vs 定数」の比較に還元し、誘導変数の更新を最大 `max_trips_` 回シミュレートしてトリップカウントを確定する（`const_unroll.cpp:476-511`・`const_unroll.cpp:664-686`）。その後ヘッダと本体をトリップカウント回複製して直列接続し、旧ループブロックは到達不能のまま後段のクリーンアップに委ねる（`const_unroll.cpp:710-775`）。展開後の総文数が `max_total_statements_` を超える場合は展開しない。`run` は1ループずつ最大16周繰り返し、外側展開で複製された内側ループを次周で展開する。
- 実行条件: `--funroll-loops` 指定時のみ `create_standard_passes` の先頭（SCCP/ConstantFoldingより前）に登録され、O0でも有効（`manager.cpp:29-34`）。svターゲットではフラグと無関係に `unroll_constant_loops`（`const_unroll.cpp:780`）がドライバから常時呼ばれる（`src/cmd/cm/build.cpp:512-514`）。
- 不変条件: `no_opt` 文を含むループは展開しない（`const_unroll.cpp:556-558`）、誘導変数のアドレスが取られるループは対象外、展開後は `update_successors` と `build_cfg` でCFGを再構築する。ループ同定の前提（単一出口・無条件Gotoラッチ）を緩めると、複製時の飛び先リマップが漏れて誤った制御フロー（反復回数のずれ・無限ループ）を生む。

### StringReassignFree

- 実装: `src/internal/mir/passes/cleanup/string_reassign_free.cpp:80`（`run`）、設計コメントは `string_reassign_free.hpp:9-27`。
- 目的: `s = s + "x"` のような文字列ローカルの再代入で旧バッファがリークするのを防ぎ、再代入直前に旧値の `cm_string_free` 呼び出しを挿入する。最適化ではなくメモリ健全性パスである。
- アルゴリズム: 文字列型ローカル（引数除く）ごとに定義サイトと使用レコードを1パスで収集し、(1) 全定義がfresh所有バッファ（`cm_string_concat` 等の新規確保ランタイムの結果、または単独消費される一時経由のコピー）、(2) エイリアスされない（読み取り専用ランタイム・returnのみ許容、コピー先一時は1段だけプロファイル追跡して除外）、(3) 再代入地点への到達定義が全てfreshで未初期化を含まない、の3条件で対象を絞る。到達定義解析は対象ローカル単体・ブロック単位のデータフローで、値ドメインは `{UNINIT} ∪ 定義サイト` である。挿入は旧値の退避コピー（`_c12_old` ローカル）を定義文の直前に置き、ブロックを分割してfree呼び出しのCall終端を差し込む形で行う。
- 実行条件: O1以上ではパイプライン先頭（`manager.cpp:41-44`）、O0（svを除く）ではドライバが直接各関数へ実行する（`build.cpp:501-510`）。
- 不変条件: loweringが生成する素のMIR形状（`T = concat(...) → X = copy(T)`）を前提に分類するため、CopyPropagation等がこの形状を書き換える前に実行しなければならない。冪等性ガード（`_c12_old` の既存検査、`string_reassign_free.cpp:81-88`）を外すと収束反復での再実行が二重freeを起こす。fresh判定・エイリアス判定のホワイトリストを緩めると解放後使用や二重解放、厳しくしすぎるとリークというバグのクラスになる。

### SparseConditionalConstantPropagation

- 実装: `src/internal/mir/passes/scalar/sccp.cpp:34`（`run`）、`sccp.cpp:204`（`analyze`）、宣言は `sccp.hpp:15-82`。
- 目的: ブロック間をまたぐ大域的な定数伝播と、定数化されたSwitchIntにもとづく到達不能コードの刈り取りを行う。ブロックローカルなConstantFoldingの補完である。
- アルゴリズム: 古典的SCCPで、ローカルごとに `Undefined / Constant / Overdefined` の3値ラティスを持ち、ブロック単位のIN/OUT状態をワークリストで不動点まで反復する（反復上限10000、`sccp.cpp:218-221`）。先行ブロックのOUTを到達可能なものだけmeetでマージし、`compute_successors` がSwitchIntの判別値が定数のとき実行される辺だけを辿ることで到達可能性と定数性を同時に解く。関数引数とインラインasmの出力変数は事前に全ブロックでOverdefinedに初期化する（`sccp.cpp:56-88`）。解析後 `apply_constants` が文・終端のオペランドを定数へ書き換え、`simplify_cfg`・`remove_unreachable_blocks` でCFGを簡約する。定数は代入先ローカルの整数幅へ正規化してからバインドする（`sccp.cpp:15-27`）。
- 実行条件: O1以上（`manager.cpp:47`）。
- 不変条件: ラティスの単調性（Undefined→Constant→Overdefinedの一方向遷移）が収束の前提であり、meetや転送関数で値を「戻す」と不動点反復が終わらない。asm出力の事前Overdefined化を外すと、ループ前の初期代入から誤って定数と推論され、ループ本体が到達不能と判定されて消えるバグのクラスになる。デストラクタ関数（名前に `__dtor` を含む）ではブロック削除をスキップする（`sccp.cpp:95-101`）ため、この除外を外すと単相化で生成されたループブロックが誤削除される。

### ConstantFolding

- 実装: `src/internal/mir/passes/scalar/folding.cpp:12`（`run`）、`folding.cpp:74`（`process_block`）、宣言は `folding.hpp:16-54`。定数演算の共通実装は `scalar/const_eval.hpp`。
- 目的: 定数オペランドの二項・単項演算・キャストの畳み込み、代数的恒等式の簡約（`x*1→x`・`x+0→x`・`x*0→0` 等、`folding.hpp:48-51`）、判別値が定数のSwitchIntのGoto化を行う。
- アルゴリズム: ブロック内前方走査で `LocalId → MirConstant` の定数表を維持する（ブロック間伝播はせず、大域はSCCPに委ねる。`folding.cpp:24-32`）。事前に複数回代入されるローカル（ループ変数等）とasm出力オペランドを検出して追跡から除外し（`detect_multi_assigned`、`folding.cpp:37-72`）、関数引数も除外する。Deref書き込みは表全体をクリア、投影付き代入はベースの表エントリを無効化する保守的エイリアス処理を行う（`folding.cpp:108-128`）。畳み込み結果は代入先ローカルの整数幅へ正規化（狭い型への代入はラップ）してから記録する（`folding.cpp:151-160`）。終端の畳み込みは `fold_terminators_` が真のときのみ行い、SwitchIntをGotoへ置換する（`folding.cpp:176-204`）。
- 実行条件: O1以上で1回、O2以上でパス列末尾にもう1回（`manager.cpp:48`・`manager.cpp:71`）。svターゲットはパイプライン外で `ConstantFolding(fold_terminators=false)` の保存モードのみをO1以上で1回実行する（`build.cpp:517-520`）。
- 不変条件: `no_opt` 文はスキップしつつ代入先を定数表から消す（`folding.cpp:93-103`）。整数幅の正規化を外すと、狭い型のオーバーフロー時のラップ挙動がバックエンドの実行結果と食い違う。svの保存モードは文数とCFG形状を変えない書き換えのみが許され、これを破ると合成対象のハードウェアロジックが消失する。

### GVN

- 実装: `src/internal/mir/passes/redundancy/gvn.cpp:11`（`run`）、`gvn.cpp:23`（`process_block`）。
- 目的: 同一ブロック内で同じ式（BinaryOp・UnaryOp・Cast）を再計算する代入を、先行する計算結果ローカルのコピーへ置き換える共通部分式除去。
- アルゴリズム: 式を文字列キーへ直列化（`stringify_rvalue`、`gvn.cpp:155-185`）して `式キー → 結果ローカル` の利用可能式表を維持し、同一キーの再出現を `Use(Copy(結果ローカル))` に書き換える。代入・StorageLive/Dead・asm出力で変更されたローカルについては、逆引き表 `var_to_exprs` を使い依存式と「その変数が結果になっている式」の両方を無効化する（`gvn.cpp:108-128`）。Deref書き込みは表全体をクリアする。値番号のブロック間共有はなく、名前どおりのGVNではなくブロックローカルCSEである。
- 実行条件: O1以上（`manager.cpp:51`）。
- 不変条件: MIRは非SSAのため、再代入時の式無効化（依存側と結果側の両方）が正しさの核であり、どちらかを欠くと古い値のコピーが残る誤コンパイルになる。式キーには演算子種別・オペランドの定数値・Place（投影含む）が全て含まれる必要があり、キーの情報落ちは異なる式の誤同一視を生む。

### CopyPropagation

- 実装: `src/internal/mir/passes/scalar/propagation.cpp:10`（`run`）、`propagation.cpp:90`（`process_block`）、宣言は `propagation.hpp:15-48`。
- 目的: `_x = copy(_y)` のコピー連鎖を追跡し、以降の `_x` 使用を `_y` へ置き換えて中間コピーを不要化する（除去自体はDCEが担う）。
- アルゴリズム: ブロック内前方走査で `コピー先 → コピー元` の写像を維持し、`resolve_copy_chain`（`propagation.cpp:239`）で循環検出付きの連鎖解決を行う。事前検出した複数回代入ローカルと関数引数、asm出力オペランドは除外する。コピー登録の条件として、コピー元と先の型一致を要素型まで再帰比較する `same_type`（`propagation.cpp:72-88`）を要求し、これによりインターフェース強制変換を含むコピーの伝播を防ぐ。Deref書き込みで表全体をクリアし、投影付き代入ではベースが変更されるため「そのベースをコピー元とする全エントリ」と「ベース自身」を無効化する（`propagation.cpp:207-226`）。キャストを含む代入はキャスト元の表エントリも消す。終端命令のオペランドにも伝播する。
- 実行条件: O1以上で1回、O2以上でもう1回（`manager.cpp:52`・`manager.cpp:72`）。js/ts/webターゲットは `no_aggregate_prop_` により構造体・配列・ユニオン型のコピー登録を抑止する（`propagation.cpp:168-177`、設定は `build.cpp:491-493`）。
- 不変条件: 集約抑止フラグの意味論を壊さないこと（jsは構造体コピーが深いクローンのため、伝播するとコピー元経由の変異が別実体に化ける）。型の再帰比較を浅い比較へ戻すと `*Shape` と `*Sq` のような別ポインタ型が同一視され、インターフェース型強制の意味が失われる。投影付き代入時のベース無効化を欠くと `_a = _b; _b.field = v; use(_a)` で古い値が読まれる。

### DeadStoreElimination

- 実装: `src/internal/mir/passes/cleanup/dse.cpp:9`（`run`）、`dse.cpp:21`（`process_block`）。
- 目的: 同一ブロック内で使用されずに上書きされるストア（`x = a; x = b` の前者）をNop化する。
- アルゴリズム: ブロック内前方走査で `ローカル → 最後の定義文ポインタ` を維持し、同一ローカルへの再定義時に前定義が未使用ならNop化する。文中の使用を収集して該当ローカルの追跡を解除し、Derefを含む使用が現れたらエイリアスの可能性から表全体をフラッシュする保守的設計である（`dse.cpp:52-55`）。StorageDeadに到達した時点で未使用の定義もNop化する。Call終端は表全体をフラッシュする（`dse.cpp:127-128`）。投影なしの直接代入のみを追跡対象とする。
- 実行条件: O1以上（`manager.cpp:55`）。
- 不変条件: `no_opt` の定義は削除対象にも追跡対象にもしない（`dse.cpp:34-44`・`dse.cpp:73-74`）。グローバル変数はブロック単位のこのパスでは触れず、関数横断の可視性の考慮はDCE側の `is_global/is_static` 保護に依存するため、追跡対象を広げる際は外部から観測可能なストアを消さないことを再検証する必要がある。Derefフラッシュを外すとポインタ経由で読まれるストアが消え、値の消失というバグのクラスになる。

### SimplifyControlFlow

- 実装: `src/internal/mir/passes/cleanup/simplify_cfg.cpp:8`（`run`）。
- 目的: 到達不能ブロックの削除、直列ブロック（AのGoto先Bで、Bの先行がAのみ）の統合、文を持たない単一Gotoブロックのバイパスによる CFG の縮約。
- アルゴリズム: `build_cfg` でCFG情報を更新してから3種の変換を試み、いずれかが1件成功するたびにループ先頭へ戻る局所不動点反復（上限100回、`simplify_cfg.cpp:12-46`）。到達可能性はエントリからのBFS、統合はBの文をAへ移してBの終端を引き継ぐ、バイパスは全先行ブロックの終端の飛び先を `redirect_jumps`（`simplify_cfg.cpp:183-212`）で書き換えてから当該ブロックをnull化する。
- 実行条件: O1以上（`manager.cpp:58`）。
- 不変条件: `redirect_jumps` はGoto・SwitchInt（targets/otherwise）・Call（success/unwind）の全飛び先を網羅する必要があり、終端の種類を追加したときの更新漏れは宙吊り辺（存在しないブロックへのジャンプ）を作る。自己ループの空ブロック（`target == i`）はバイパスしてはならない（無限ループの意味が変わる）。1変換ごとにCFGを再構築する構造を崩すと、古いpredecessor情報にもとづく誤統合が起きる。

### FunctionInlining

- 実装: `src/internal/mir/passes/interprocedural/inlining.cpp:12`（`run_on_program`）、宣言は `inlining.hpp:20-59`。
- 目的: 文数が閾値（`INLINE_THRESHOLD = 10`）以下の小関数を呼び出し側へ展開する。
- アルゴリズム: プログラム全体パスとして関数名→本体の写像を作り、各Call終端について呼び出し先本体のローカル・ブロックをオフセット付きで複製し、IDをリマップして呼び出し側へ接続する（`perform_inlining`、`inlining.cpp:143-228`）。呼び出し元→先の組ごと（上限2回）とプログラム全体（上限20回）の回数制限で無限展開を防ぐ。ラムダ・クロージャ・asm文を含む関数は対象外（`inlining.cpp:116-141`）。
- 実行条件: O1以上に登録されている（`manager.cpp:59`）が、現行loweringが呼び出し先を `FunctionRef` で発行するのに対し本パスは旧形式の `Constant(文字列)` のみ認識するため、実質的に全呼び出しが対象外の休眠状態である（`inlining.cpp:66-73` のコメント）。最終的なインライン展開はLLVM側インライナが担う。
- 不変条件: 文複製時に `no_opt` フラグを複製先へ引き継ぐこと（`clone_statement`、`inlining.cpp:234`）。休眠の前提を変えて有効化する場合、`perform_inlining` の再設計（デストラクタ実行順序・戻り値の扱い・リマップの検証）とセットで行わなければ、デストラクタ順序の破壊やアロケータのクラッシュという既知のバグのクラスが広範に露出する。

### TailCallElimination

- 実装: `src/internal/mir/passes/interprocedural/tail_call_elimination.cpp:10`（`run`）。
- 目的: 自己再帰の末尾呼び出しを検出して `CallData::is_tail_call` をマークし、LLVMコード生成でtail call属性を付けさせる。MIR上の構造変換は行わない。
- アルゴリズム: 各ブロックのCall終端について、呼び出し先が自関数名の `FunctionRef` であること（`is_self_call`、`tail_call_elimination.cpp:44-59`）、継続ブロックがReturn終端で、文があっても戻り値ローカルへの代入のみであること（`is_tail_position`、`tail_call_elimination.cpp:61-88`）を検査してフラグを立てる。呼び出しをループへ書き換える `transform_to_loop`（`tail_call_elimination.cpp:90-161`）も実装されているが `run` からは呼ばれない未使用ヘルパである。
- 実行条件: O1以上（`manager.cpp:61`）。
- 不変条件: 末尾位置判定は「継続ブロックに戻り値代入以外の文がない」ことが本質であり、これを緩めると呼び出し後に実行されるべき文（デストラクタ呼び出し等）が飛ばされる。マークは冪等（マーク済みはスキップ）で、変更なし時にfalseを返すことが収束判定の前提である。

### LoopInvariantCodeMotion

- 実装: `src/internal/mir/passes/loop/licm.cpp:10`（`run`）、`licm.cpp:27`（`process_loop`）。
- 目的: ループ内で値が変わらない純粋な計算（Use・BinaryOp・UnaryOp・Cast・FormatConvert）をループ手前のpre-headerブロックへ移動する。
- アルゴリズム: 支配木（`src/internal/mir/analysis/dominators.cpp`）と自然ループ検出（`src/internal/mir/analysis/loop_analysis.cpp`）を構築し、ループ木を内側から再帰処理する。ループ内で変更されるローカル（代入先・asm出力・Call格納先）の集合を作り、ヘッダブロックの文のうちオペランドが全て不変（定数・FunctionRef・変更されない投影なしローカル）のものを移動候補とする（`is_invariant`、`licm.cpp:223-267`）。pre-headerは既存の単一Goto先行ブロックを再利用するか、全エントリ辺の飛び先を書き換えて新設する（`get_or_create_pre_header`、`licm.cpp:125-221`）。Ref（アドレス取得）を含むrvalueはメモリアクセス扱いで移動しない（`licm.cpp:269-271`）。
- 実行条件: O1以上（`manager.cpp:64`）。
- 不変条件: 走査対象がヘッダブロックの文に限られる点が安全性の一部を担っており（ヘッダは毎周回必ず実行される）、本体ブロックへ広げる場合は条件付き実行文の投機移動（例外・トラップの前倒し）を別途正当化する必要がある。`no_opt` 文は移動しない（`licm.cpp:83-84`）。pre-header新設時はGoto・SwitchInt・Callの全エントリ辺を書き換える必要があり、漏れるとループへの横入り辺が残って移動の前提が崩れる。

### DeadCodeElimination

- 実装: `src/internal/mir/passes/cleanup/dce.cpp:9`（`run`）。
- 目的: 到達不能ブロックの除去、関数内で読まれないローカルへのストアのNop化、Nop文の物理削除の3段クリーンアップ。
- アルゴリズム: (1) `update_successors` 後にエントリからBFSで到達可能集合を作り、不達ブロックをnull化して `build_cfg` する（`dce.cpp:30-79`）。(2) 全文・全終端から使用ローカル集合を収集し（`collect_used_locals`、`dce.cpp:187-342`）、未使用ローカルへの副作用なし代入とStorageLive/DeadをNop化する。戻り値・引数・グローバル/静的ローカルは無条件に使用扱いとする（`dce.cpp:87-100`）。(3) Nop文を `remove_if` で物理削除する（`dce.cpp:162-185`）。
- 実行条件: O1以上のパス列末尾で1回、O2以上でもう1回（`manager.cpp:67`・`manager.cpp:73`）。さらにCompileコマンド時（sv除く）はドライバが最適化レベルと無関係に全関数へ直接実行する（`build.cpp:526-535`）。
- 不変条件: グローバル・静的ローカルの保護を外すと `g = 999` のような外部から観測可能なストアが消える。`no_opt` 代入はターゲットローカル自体を使用済みにマークしてデッドストア扱いを防ぐ（`dce.cpp:209-211`）。デストラクタ関数では到達不能ブロック除去をスキップする（`dce.cpp:12-19`）。asm文のオペランドは全て使用扱いにする必要があり、漏れるとasmが参照する変数の初期化が消える。

## ドライバ・バックエンドが単独駆動するパス

### ProgramDeadCodeElimination

- 実装: `src/internal/mir/passes/cleanup/program_dce.cpp:9`（`run(MirProgram&)`）。基底の `OptimizationPass` ではなく `MirProgram` を直接受ける独立クラスである（`program_dce.hpp`）。
- 目的: 呼び出しグラフから到達しない関数と、どこからも参照されない構造体をプログラム単位で削除し、特に単相化・自動導出が生成した未使用関数を落とす。
- アルゴリズム: エントリポイント（`main`・`_start` 等）・組み込みランタイム関数・export関数をルート集合とし、ワークリストで呼び出しグラフを辿って使用関数を収集する。Call終端の呼び出し先（FunctionRef/旧Constant文字列の両対応）、文中のFunctionRefオペランド、呼び出し引数のFunctionRef（クロージャ）を辿り、モジュール修飾名は単純名へのフォールバック解決を行う（`program_dce.cpp:120-134`）。さらに `Type__method` 命名の型が1つでも使用されていれば同型の全メソッドを保持し、vtableエントリの実装関数を無条件に保持する（`program_dce.cpp:208-241`）。構造体は使用関数のローカル・引数・配列要素型から収集し、フィールド型を再帰的に辿って未使用のものを削除する。
- 実行条件: Compileコマンド時のみ、svターゲットを除き常時（`build.cpp:537-544`）。インタプリタ実行では動的ディスパッチがあるため実行しない。svは全関数をハードウェアモジュールとして保持する。
- 不変条件: 関数参照の収集経路（Call終端・FunctionRefオペランド・引数）を1つでも欠くと、間接的にしか呼ばれない関数が削除されリンクエラーまたは実行時の未定義呼び出しになる。vtable保持を欠くと動的ディスパッチ先が消える。ルート集合の組み込みリストはランタイムのシンボル群と同期が必要である。

### UndefinedCheckInstrumentation

- 実装: `src/internal/mir/passes/instrumentation/undefined.cpp:258`（`run`）、プログラム全体入口は `instrument_undefined_checks`（`undefined.cpp:325`）。
- 目的: `--sanitize=undefined` 時に、整数のDiv/Modのゼロ除数と生ポインタDerefのnull参照の直前へ実行時ガードを挿入し、違反時は `panic("runtime error: ...")` へ分岐させる。MIRへ挿入するためnative/wasm/jit/jsの全実行系で同一の検出動作になる。
- アルゴリズム: 各文・各終端からガード対象（整数除算の除数クローン、Deref投影の手前までのポインタPlace）を収集し、対象文の直前でブロックを分割して SwitchInt によるガード連鎖（ゼロ除算は `除数==0→panic`、null参照は `Eq(ptr, null)` の結果で分岐）を差し込む（`undefined.cpp:2-4` のコメントと `undefined.cpp:258-322`）。分割で移動した文・終端の再計装はポインタ同一性の処理済み集合で防ぎ、Moveオペランドは二重消費を避けるためCopyへ変換してクローンする（`undefined.cpp:34-49`）。浮動小数除算（IEEE 754で定義済み）と参照型（生成時に非null保証）は対象外である。
- 実行条件: `--sanitize=undefined` 指定時に、MIR最適化完了後の1回のみドライバから適用される（`build.cpp:647-651`）。
- 不変条件: 最適化パイプラインの後段で1回だけ実行する順序が本質であり、パイプライン内へ移すと挿入したガードが定数伝播・DCEに消される。処理済み集合を外すとブロック分割で送られた文が再計装され、ガードの重複挿入で無限にブロックが増える。挿入後の `build_cfg` を欠くと後続処理の支配木が壊れる。

### BoundsCheckInstrumentation

- 実装: `src/internal/mir/passes/instrumentation/bounds.cpp:71`（`run`）、プログラム全体入口は `instrument_bounds_checks`（`bounds.cpp:173`）。
- 目的: `--sanitize=bounds` 時に、スライスアクセスのランタイム関数呼び出し（`cm_slice_get_*`・`cm_slice_delete`）の直前へ `0 <= index < len` の境界検査を挿入し、違反時は `cm_bounds_error(index, len)` へ分岐して即時終了させる。静的サイズの固定長配列を検査するLLVMの `BoundsCheckingPass`（`src/internal/codegen/llvm/native/codegen.cpp:989`）と補完関係にある。
- アルゴリズム: Call終端の呼び出し先名が検査対象パターンに一致するブロックについて、`cm_slice_len` 呼び出しで長さを取得するブロック、負インデックス検査、長さ比較検査、エラーブロック（`cm_bounds_error → Unreachable`）を新設して元の呼び出しの手前へ直列に差し込む（`bounds.cpp:1-8` のコメント）。処理済み終端はポインタ同一性の集合で再計装を防ぐ。2インデックス契約の `cm_slice_get_subslice` は対象外である（`bounds.cpp:23-29`）。
- 実行条件: `--sanitize=bounds` 指定時に、svを除きMIR最適化完了後の1回のみドライバから適用される（`build.cpp:652-658`）。
- 不変条件: 検査対象の関数名パターンはスライスランタイムの命名と同期が必要で、新しいアクセス関数の追加漏れは検査の抜け（サニタイザの偽陰性）になる。インデックス・スライスオペランドのクローンはCopy化して二重Moveを避けること。UndefinedCheckInstrumentation同様、最適化後の適用順序が前提である。

### NoStdChecker

- 実装: `src/internal/mir/passes/validation/no_std_checker.cpp:10`（`check`）、宣言は `no_std_checker.hpp:13-27`。`OptimizationPass` を継承しない検証専用クラスである。
- 目的: ベアメタルターゲットで、OS依存機能（標準出力・ヒープ・プロセス制御・ファイルI/O・ネットワーク・スレッド）の呼び出しをコンパイルエラーとして検出する。MIRを変更しない読み取り専用パスである。
- アルゴリズム: 全関数の全Call終端から呼び出し先名（FunctionRef/Constant文字列の両対応）を取り出し、禁止関数の静的集合と `cm_print*`・`cm_file_*` 等の接頭辞規則で照合して、カテゴリ別のi18nエラーメッセージを蓄積する。
- 実行条件: LLVMバックエンドのターゲットがBaremetal系（`Baremetal`・`BaremetalX86`・`BaremetalUEFI`）のとき、コード生成前に常時実行され、違反があればコンパイルを中断する（`src/cmd/cm/backend/llvm.cpp:151-164`）。
- 不変条件: 禁止リスト・接頭辞規則はランタイム関数の命名と同期が必要で、更新漏れはベアメタルバイナリへのOS依存シンボル混入（リンク失敗または実行時クラッシュ）として現れる。

## 休眠パス

### ArrayBaseExtraction

- 実装: `src/internal/mir/passes/scalar/array_base_extraction.cpp:10`（`run`）、宣言は `array_base_extraction.hpp:22-48`。
- 目的: 多次元配列アクセス `a[i][j]` の行ベースオフセット `i*stride` を一時ローカルへ抽出・再利用し、内側ループでLLVMのLICMがベース計算を巻き上げられる形にする。
- アルゴリズム: ブロック内の文を走査し、複数のIndex投影を持つPlaceを検出したら `(配列ローカル, 行インデックスローカル)` をキーとするキャッシュでベースオフセット計算文を共有し、アクセスを1次元化した形へ書き換える。
- 実行条件: どこにも登録されていない。`create_standard_passes` にもドライバにも呼び出し箇所がなく、現在は実行されない休眠実装である。
- 不変条件: 有効化する場合はストライド計算（`get_array_stride`）がLLVMのDataLayoutと一致することの検証が必要で、不一致は隣接行への誤アクセス（メモリ破壊）という最悪クラスのバグになる。

## 落とし穴とケア

- パスの追加・変更時に全パス共通で守るべき不変条件（`no_opt` の尊重、asm出力の追跡除外、非SSA前提の再代入無効化、CFG更新APIの使用、実行順の依存関係）は [MIRの設計の落とし穴とケア](mir-design.md#落とし穴とケア) に集約されており、本書の各節はパス固有の条件のみを述べている。
- 収束判定は「変更あり」の自己申告に依存する: 各パスの `run` は実際に書き換えたときだけtrueを返す契約であり、変更なしでtrueを返すと `run_until_fixpoint`（`src/internal/mir/passes/core/base.cpp:59`）の実行回数上限消費と循環検出の誤作動、falseの誤申告は前回変更なしスキップによる最適化の取りこぼしを招く。
- 同名パスの複数登録は実行回数上限を共有する: 上限管理はパス名文字列をキーにするため（`base.cpp:86-105`）、O2で2回登録される `ConstantFolding` 等は合計30回の上限を分け合う。パス名の変更は上限・スキップ判定・デバッグ出力の全てに影響する。
- ブロック単位パスとプログラム単位パスの境界に注意する: `FunctionInlining` と `ProgramDeadCodeElimination` は `run_on_program`/`run(MirProgram&)` を持ち、関数の追加・削除を行う。関数リストのイテレーション中に要素を消す変更を加える場合、`OptimizationPass::run_on_program` の既定実装（`base.hpp:34-42`）が全関数を順に回す前提と衝突しないか確認すること。
- 計装パスはブロックを増やしながら走査する: `add_block` は `basic_blocks` の再確保を起こすため、ブロックポインタは反復のたびに取得し直す必要がある（`undefined.cpp:265-267` のコメント）。保持したままのポインタ経由の書き込みはuse-after-freeになる。
- 検査対象・保護対象の名前リストはランタイムと同期する: `ProgramDeadCodeElimination` のルート組み込み関数、`BoundsCheckInstrumentation` のスライスアクセス関数名、`NoStdChecker` の禁止リスト、`StringReassignFree` のfresh/非保持ホワイトリストは、いずれもランタイム関数の命名に対する文字列照合であり、ランタイム側の追加・改名時にこれらの更新を怠ると削除しすぎ・検査漏れ・リークが静かに発生する。
- パス単体の回帰は手組みMIRで検証する: パス単体テストとパイプライン通過の回帰テストの配置・単一情報源は [MIRの設計](mir-design.md#落とし穴とケア) の記載に従うこと。

## 関連資料

- [MIRの設計（データ構造・実行機構・パス実行順の概観）](mir-design.md)
- [コンパイルパイプライン全体像](overview.md)
- [クロージャのlowering](../lowering/closures.md)
