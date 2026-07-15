# MIR最適化のテストカバレッジ

パスごとの対応状況の単一情報源。テストは3層構成に分かれる:
- **unit**（`tests/unit/mir_pass_test.cpp`、`make test-unit`）: 手組みMIRに対して各パスの.cppを単体で検証（フロントエンド・lowering非依存）
- **regression**（`tests/regression/mir_optimization_test.cpp`、`make test-regression`）: Cmソースをパイプラインに通した上で最適化パイプライン全体（複数パスの組み合わせ・収束反復）を検証
- **integration**: リリースビルドのcmバイナリに対するO0〜O3のバックエンドスイート実行（`make test`）が最適化込みの動作を検証

## パス別カバレッジ

| パス | 実装 | テスト | ケースファイル |
|---|---|---|---|
| ConstantFolding（定数畳み込み） | scalar/folding.cpp | unit: ConstantFolding_FoldsConstantBinaryOp / FoldsComparison | 手組みMIR |
| ConstantFolding（恒等式簡約） | scalar/folding.cpp | unit: ConstantFolding_AlgebraicIdentity / FloatIdentityNotSimplified | 手組みMIR |
| ConstantFolding（文保存モード） | scalar/folding.cpp | unit: ConstantFolding_TerminatorFoldControl | 手組みMIR |
| SCCP（疎条件付き定数伝播） | scalar/sccp.cpp | unit: SCCP_PropagatesAcrossBlocks | 手組みMIR |
| CopyPropagation（コピー伝播） | scalar/propagation.cpp | unit: CopyPropagation_PropagatesThroughChain | 手組みMIR |
| GVN（共通部分式除去） | redundancy/gvn.cpp | unit: GVN_EliminatesRedundantExpression / InvalidatedByReassignment | 手組みMIR |
| DSE（デッドストア除去） | cleanup/dse.cpp | unit: DSE_RemovesOverwrittenStore | 手組みMIR |
| DCE（デッドコード除去） | cleanup/dce.cpp | unit: DCE_RemovesUnusedAssignment / RemovesUnreachableBlock | 手組みMIR |
| ProgramDCE（未到達関数除去） | cleanup/program_dce.cpp | unit: ProgramDCE_RemovesUnreachableFunction | 手組みMIR |
| SimplifyControlFlow（CFG簡約） | cleanup/simplify_cfg.cpp | unit: SimplifyCFG_CollapsesGotoChain | 手組みMIR |
| FunctionInlining（インライン化） | interprocedural/inlining.cpp | unit: FunctionInlining_CurrentlyDormant（休眠状態の固定。下記参照） | 手組みMIR |
| TailCallElimination（末尾呼び出し） | interprocedural/tail_call_elimination.cpp | unit: TCE_MarksSelfTailCall / IgnoresNonSelfCall | 手組みMIR |
| LICM（ループ不変式移動） | loop/licm.cpp | unit: LICM_HoistsInvariantOutOfLoop | 手組みMIR |
| ConstantLoopUnroll（定数ループ展開） | loop/const_unroll.cpp | unit: ConstUnroll_UnrollsConstantTripLoop | 手組みMIR |
| パイプライン全体 | core/ + convergence/ | regression: OptimizationPipeline_Standard / Fixpoint / IntegrationTest_ComplexOptimization | pipeline_standard.cm / pipeline_fixpoint.cm / integration_complex_optimization.cm |

## 単体テスト対象外のパスと理由

| パス | 理由 |
|---|---|
| scalar/array_base_extraction.cpp | 標準パイプライン（create_standard_passes）に未接続の実験的パスで、実行経路が存在しないためテスト対象外。接続時にテストを追加する |
| validation/no_std_checker.cpp | 最適化ではなく検証パス。UEFI/baremetalのエラーテスト（println/malloc等のno_std違反検出）で統合カバーされる |
| convergence/（収束管理） | 単体の変換を持たないインフラ。OptimizationPipeline_Fixpointで間接カバーされる |

## 既知の問題: インライン化パスは休眠状態

FunctionInliningは呼び出し先オペランドを旧形式の `Constant(文字列)` として期待するが、現行のMIR loweringは `FunctionRef` を発行するため、**実運用では一度も作動していない**（2026-07-14のテスト整備で発見）。
FunctionRefを認識させて有効化する実験では、perform_inliningの潜在バグ（デストラクタ実行順序の破壊・メソッドチェーンの誤結果・アロケータのSIGSEGV等）がインタプリタスイートで広範に露出したため、修正を差し戻した。
有効化にはperform_inliningの再設計（ローカル/ブロックのリマップ・デストラクタと戻り値の扱いの検証）が必要。現状はFunctionInlining_CurrentlyDormantテストが休眠状態を固定しており、なお最終的なインライン展開はnative/baremetal/JIT/WASMではLLVM側のインライナが担っている。

## テストで確認された実装上の性質（前提知識）

- **コピー伝播・定数畳み込みは関数引数を保守的に除外する**ため、パイプライン実運用ではloweringが生む使用ごとの一時変数コピーが正規化されず、引数を直接使う式（`a + b`）はGVNの共通部分式検出の対象にならない（単一代入ローカル経由の式のみ対象）
- **fold_terminators=falseのConstantFoldingは文数・ブロック数・終端命令列を変えない**（SVバックエンドが依存する契約。ConstantFolding_StatementPreservingで固定）
- **LICMはループヘッダブロック内の不変式のみ巻き上げる**（本体ブロック内の不変式は対象外）。巻き上げは文の移動であって削除ではない（文数保存をLICM_HoistsInvariantOutOfHeaderで固定）
- **定数ループ展開は「switch [1→本体], otherwise 出口」の形のヘッダのみ受理する**。展開後も旧ループブロックは未到達のまま残る（後続のDCEが除去する。展開の完了は到達可能CFGの非循環性で判定する）
