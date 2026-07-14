# MIR最適化パスのテストカバレッジ

`mir_optimization_test`（`make test-integration`）が使用するケースファイルと、パスごとの対応状況の単一情報源。

## パス別カバレッジ

| パス | 実装 | テスト | ケースファイル |
|---|---|---|---|
| ConstantFolding（定数畳み込み） | scalar/folding.cpp | ConstantFolding_Simple / Comparison | constant_folding_simple.cm / constant_folding_comparison.cm |
| ConstantFolding（恒等式簡約） | scalar/folding.cpp | ConstantFolding_AlgebraicIdentity / FloatIdentityNotSimplified | algebraic_identity.cm / float_identity.cm |
| ConstantFolding（文保存モード） | scalar/folding.cpp | ConstantFolding_StatementPreserving | statement_preserving.cm |
| SCCP（疎条件付き定数伝播） | scalar/sccp.cpp | SCCP_ConditionalConstant | sccp_conditional_constant.cm |
| CopyPropagation（コピー伝播） | scalar/propagation.cpp | CopyPropagation_Simple / Chain | copy_propagation_simple.cm / copy_propagation_chain.cm |
| GVN（共通部分式除去） | redundancy/gvn.cpp | GVN_RedundantExpression | gvn_redundant_expr.cm |
| DSE（デッドストア除去） | cleanup/dse.cpp | DeadStoreElimination_OverwrittenStore | dse_dead_store.cm |
| DCE（デッドコード除去） | cleanup/dce.cpp | DeadCodeElimination_UnusedVariable / UnreachableBlock | dce_unused_variable.cm / dce_unreachable_block.cm |
| ProgramDCE（未到達関数除去） | cleanup/program_dce.cpp | ProgramDCE_UnusedFunction | program_dce_unused_function.cm |
| SimplifyControlFlow（CFG簡約） | cleanup/simplify_cfg.cpp | SimplifyControlFlow_GotoChain | simplify_control_flow_goto_chain.cm |
| FunctionInlining（インライン化） | interprocedural/inlining.cpp | FunctionInlining_CurrentlyDormant（休眠状態の固定。下記参照） | inlining_small_callee.cm |
| TailCallElimination（末尾呼び出し） | interprocedural/tail_call_elimination.cpp | TailCallElimination_SelfTailCall | tce_self_tail_call.cm |
| LICM（ループ不変式移動） | loop/licm.cpp | LICM_InvariantHoist | licm_invariant_hoist.cm |
| ConstantLoopUnroll（定数ループ展開） | loop/const_unroll.cpp | ConstantLoopUnroll_ConstantTrip | unroll_constant_trip.cm |
| パイプライン全体 | core/ + convergence/ | OptimizationPipeline_Standard / Fixpoint / IntegrationTest_ComplexOptimization | pipeline_standard.cm / pipeline_fixpoint.cm / integration_complex_optimization.cm |

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

- **コピー伝播・定数畳み込みは関数引数を保守的に除外する**ため、引数を直接使う式（`a + b`）はオペランドが使用ごとの一時変数のままとなり、GVNの共通部分式検出は単一代入ローカル経由の式のみが対象になる
- **fold_terminators=falseのConstantFoldingは文数・ブロック数・終端命令列を変えない**（SVバックエンドが依存する契約。ConstantFolding_StatementPreservingで固定）
- **LICMの巻き上げは文の移動であって削除ではない**（文数保存をLICM_InvariantHoistで固定）
- **定数ループ展開後も旧ループブロックは未到達のまま残る**（後続のDCEが除去する。展開の完了は到達可能CFGの非循環性で判定する）
