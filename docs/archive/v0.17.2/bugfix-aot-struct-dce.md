# v0.17.2 バグ修正: ポインタ経由参照のみの構造体がプログラムDCEで削除される（AOT限定の不正IR）

セルフホスティング向け標準ライブラリの実装中に発見した6件目の不具合の記録。
`AtomicSharedPtr`（制御ブロック `AtomicSharedCount` をポインタでのみ保持する）が最初の顕在化点で、v0.17.2で修正済み。

## 症状

`cm run`（JIT）では正常動作するプログラムが、`cm compile`（AOT）だけ「LLVM module verification failed: Load operand must be a pointer」で失敗する。
生成IRを見ると、`&self.count->strong` のような二段ポインタ越しのフィールドアドレス計算から最終フィールドGEPとstoreが**黙って脱落**し、未初期化ローカルの読み出しや `load ptr, i32 1` のような不正命令が残る。

## 真因

コンパイルパイプラインの2段の問題の合成。

1. **プログラムDCEの使用中struct収集がポインタを辿らない**（`mir/passes/cleanup/program_dce.cpp`）: 使用中構造体の収集が「値型のローカル」と「構造体フィールドの値型」だけを見ており、`Ctr*` のようにポインタ・型引数経由でしか現れない構造体を「未使用」と誤判定して定義ごと削除していた。
2. **codegenのフィールド投影が型解決失敗を黙って飲む**（`codegen/llvm/core/operand.cpp`）: struct定義が消えているためフィールド投影のstruct型解決が失敗し、`getPlaceAddress` がnullptrを返して当該文の命令生成がスキップされる（エラーはデバッグログのみ）。結果として不正IRが検証段まで進む。

プログラムDCEはAOT（`cm compile`）のパイプラインのみで走るため、JITでは顕在化しない。
`SharedPtr` が同じ構造（`SharedCount*`）でも動いていたのは、他の生存判定経路で偶然structが残っていたため。

## 修正

DCEの使用中struct収集に型ノードの再帰走査を導入した（`collect_struct_names_from_type`）。
ポインタ（`element_type`）・配列・型引数（`type_args`）を再帰的に辿り、Struct/Generic種別の名前を全て「使用中」として収集する。
フィールド走査側も同じ再帰ヘルパーで統一し、`Ctr*` フィールドのようなポインタ経由参照も生存扱いになる。

デバッグの決め手は `CM_DUMP_IR=1` によるJIT/AOTのIR直接diff（AOTだけGEP+storeが欠落）と、フィールド投影失敗点への一時診断出力（`structName='Ctr'` の解決失敗を直接確認）。

## 残課題（設計課題）

codegen側の「型解決失敗で命令を黙ってスキップする」挙動は不正IRの温床であり、ハードエラー化（該当関数名・投影位置付きの診断）が望ましい。
v0.17.2ではDCE側の修正でIR不正の実経路は塞がったため、codegen側のエラー化は今後の課題として記録する。

## 回帰テスト

- `tests/common/structs/ptr_only_struct.cm` — ポインタ経由でのみ参照される構造体（ジェネリックimpl内の `Ctr*` フィールド・二段デリファレンスの読み書き）の最小再現（本修正で新規追加）
- `tests/common/stdlib/smart/atomic_shared.cm` — 顕在化点の実利用テスト（llvmスイートでAOT経路を通る）
