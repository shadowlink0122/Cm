# v0.16.0 実装設計 9: バックエンド間ギャップの解消（JS以外）

優先度: 追加（ユーザー要望 2026-07-11）
目的: バックエンド間の構文・機能サポートの乖離のうち、JS以外に起因するギャップを解消し、サポート範囲を単一のドキュメントで宣言する。

## 背景

SVコード生成の精査（設計08）に続くバックエンド横断調査で、以下の乖離が確認された（詳細な証拠はこの文書の各節に記載）。

1. SVコード生成に「静かな握り潰し」経路が残っており、非対応構文がエラーにならずコメントや `0` として出力されるケースがある
2. 正式仕様（CANONICAL_SPEC.md / cm_grammar.md）がバックエンド適用範囲を定義しておらず、可否の情報がエラーコード・チュートリアル・skipファイルに分散している
3. 共通テストスイートに理由が記録されていないskipや期待値ファイル欠落があり、非JSバックエンドの実ギャップ（WASM 2件等）が棚卸しされていない
4. ドキュメントと実装の不整合（SV004の「警告」表記、FEATURES.mdの古いバックエンド表）

## 対象範囲と方針決定

- **対象**: SV・WASM・LLVM(Native/JIT)・UEFI・baremetal に関するギャップと、サポート範囲のドキュメント化
- **対象外**: JSバックエンドのギャップ（ポインタ系23カテゴリのskip・53bit精度・狭整数ラップ）は本設計の範囲外とする
- **方針決定（2026-07-11 ユーザー判断）**: baremetal・UEFI・SV では共通テストスイート（tests/common）を実行しなくてよい。これら3バックエンドは専用スイートのみで検証する方針を、サポートマトリクス文書に明記する

## G1. SVコード生成の静かなフォールバックを明示エラー化【実装】

### 現状

SV002/004/005/006 の型検査で捕捉されない非対応構文が、以下の4箇所でサイレントに出力される（監査08のA項に未掲載だった残存箇所）。

| 箇所 | 関数 | 現在の出力 |
|---|---|---|
| codegen.cpp:1326 | `emitRvalue` default | `/* unsupported rvalue */` |
| codegen.cpp:1738 | `emitStatement` default | `// unsupported statement` |
| codegen.cpp:4837 | `emitHirExpr` フォールバック（テストベンチ経路） | `0 /* unsupported expr */` |
| codegen.cpp:4917 | `emitHirStmt` フォールバック（テストベンチ経路） | `/* unsupported stmt */` |

コメント化は「合法だが意味の違うSV」を生む（例: 式が `0` になり回路が縮退する）。設計08で「静かに壊れたSVの温床」と指摘済み。

### 設計

- 新しいエラーコード **SV007（非対応構文）** を導入し、4箇所すべてで `std::runtime_error` により即時コンパイル停止する
- エラーメッセージには構文種別（MirRvalue::Kind / MirStatement::Kind / HIRノード種別の名前）と、テストベンチ経路の場合は「`#[test]` 内で使用できない構文」であることを含める
- 出力例:

```
エラー[SV007]: SVターゲットで非対応の構文です: MirRvalue::Aggregate（集約構築）
エラー[SV007]: #[test] 関数内で非対応の文です: HirFor（テストベンチではstep/assert/代入/println/ifのみ使用できます）
```

- 既存コーパス（tests/sv 全112件・CmCPU 全13回路・tests/cmtest）でこれらの経路に到達しないことを実装時に確認する。到達する場合はその構文を先に正式対応するか、SV002系の型検査へ昇格させる

## G2. WASMの共通テストskip 2件の解消【実装・要調査】

### 現状

`tests/common` のうちWASMのみskipされているテストが2件ある（skipファイルに理由の記載なし）。

| テスト | skip内容 |
|---|---|
| collections/nested_vector_lifecycle_test | `llvm-wasm` |
| memory/array_ptr_cast | `llvm-wasm` |

### 設計

1. 各テストをWASMで実行して失敗内容を記録する（wasmtime実行系）
2. 根本原因がランタイム（`libs/wasm`・メモリモデル）の欠陥であれば修正する
3. WASMの実行環境上の本質的制約（例: ホストメモリアドレスに依存する操作）であれば、skipファイルへ理由を1行で記載し、サポートマトリクスの既知の差異に転記する

## G3. skip・期待値欠落の棚卸し【実装（テスト整備）】

### 現状（2026-07-11 のフルスイート実行ログより）

| 分類 | 対象 | 問題 |
|---|---|---|
| 空のskipファイル（全バックエンドでスキップ・理由なし） | common/fs/file_io_test、common/file_io/file_read_write、common/memory/address_interpolation、common/advanced_modules/import_features | なぜスキップされているか記録がない |
| 期待値ファイル欠落（No expect/error file） | common/types/enum_char_value、sv/import/vga_timing、sv/import/alu_lib、sv/edge-cases/multi_clock_domain、sv/edge-cases/empty_concat、sv/edge-cases/deep_nesting | テストとして機能していない |
| 死んだskipファイル | common/enum/multi_field_extract.skip（内容が日本語の理由文でバックエンドパターンに一致せず、実際にはスキップされない。テスト自体はPASSしている） | 記述と実態の乖離 |
| 理由なしskip | llvm/io/input | 対話入力が必要と推測されるが記録がない |

### 設計

- 各テストを実行して現状を確認し、(a) 通るなら有効化（skip削除・expect追加）、(b) 通らないなら理由をskipファイルの1行コメントに記録、のどちらかに倒す
- multi_field_extract.skip はテストがPASSしているため削除する
- ランナー改善（小）: 空のskipファイルに `[SKIP]` 表示で「理由未記載」と警告を出し、新規の理由なしskipを抑止する

## G4. LLVM O3 Linux x86_64 のSIGILL【ドキュメント化のみ】

`functions/recursive_function` と `interface/operator_explicit` が `llvm-o3:linux:x86_64` でスキップされている。LLVM O3の到達不能コード最適化が `ud2` 命令を生成する既知のプラットフォーム固有問題で、`mir_to_llvm.cpp` に到達可能性解析の回避策が実装済み。macOS/ARM64には影響しない。ローカルに再現環境がないため、本設計ではサポートマトリクスへの既知の問題としての記載のみ行い、修正はLinux環境での再現調査とセットで別途扱う。

## G5. バックエンド対応マトリクスの新設【ドキュメント】

`docs/design/backend_support_matrix.md` を新設し、構文・機能グループ×バックエンドの対応表を単一情報源として管理する。

- 行: 言語コア構文 / 型（ポインタ・浮動小数点・string・動的配列等）/ 標準ライブラリ各モジュール / プラットフォーム固有機能
- 列: JIT / LLVM Native / WASM / JS / SV / UEFI / baremetal
- 記号: ✅ 対応 / ⚠️ 制限つき（注記必須） / ❌ 明示エラー（コード付き） / ― 対象外（設計上の非目標）
- テスト方針（どのスイートがどのバックエンドで走るか、common非実行3バックエンドの決定を含む）も同文書に記載する
- CANONICAL_SPEC.md と FEATURES.md からリンクし、仕様を読む人が可否に到達できるようにする

## G6. ドキュメント不整合の修正【ドキュメント】

| 対象 | 問題 | 修正 |
|---|---|---|
| tutorials/{ja,en}/compiler/sv/types.md | `float`/`double` を「警告（SV004）」と記述しているが、v0.16.0で明示エラーに変更済み | エラーである旨に更新 |
| docs/FEATURES.md | バックエンド表がv0.14.0時点のままで SV/UEFI/baremetal を欠き、診断コード表に SV0xx 帯がない | バックエンド表・診断表を現状へ更新し、マトリクスへのリンクを追加 |

## 実施結果（2026-07-11）

- **G1 完了**: SV007を4箇所+`#[test]`内の未対応呼び出し（計5箇所）に実装。ゴールデンテスト2件（sv007_inline_asm / sv007_test_unknown_call）を追加し、既存コーパスがフォールバック経路に到達しないことをフルスイートで確認
- **G2 完了**: memory/array_ptr_cast はO0で成功するため有効化し、O3のみ理由つきskipに限定（O3で配列ポインタキャスト後の読み出しが0になる実バグを記録）。collections/nested_vector_lifecycle_test は実バグを確認（ネストVecの2行目push後に `row1.len()` が3→4に破壊される。ネイティブは正常）。skipへ理由とllvm-wasmパターンを記録し、修正は別課題とする
- **G3 完了**: multi_field_extract.skip（死にファイル）を削除しテストを有効化。enum_char_value に期待値を追加して有効化。fs系2件（std::fs未実装）・import_features（改行を含む選択的import構文が未パース）・address_interpolation（実アドレス出力で非決定的）・llvm/io/input（対話入力）に理由を記録。SVの期待値欠落5件に COMPILE_OK 期待値を追加して有効化。ランナーを「コメントのみのskip=理由つき全バックエンドスキップ」に対応させ、空skipには理由記載を促す警告を追加
- **G4〜G6 完了**: Stage 1（マトリクス・ドキュメント修正）で対応済み

## 段階分割

1. **Stage 1（本設計と同時・ドキュメント）**: G5 マトリクス新設、G6 不整合修正、G4 既知の問題の記載
2. **Stage 2（実装）**: G1 SV007エラー化 + 回帰テスト（ゴールデンテストで `EXPECT_THROW` を追加）
3. **Stage 3（テスト整備）**: G3 棚卸し（expect追加・skip理由記録・死にファイル削除・ランナー警告）、G2 WASM 2件の調査と解消

## テスト計画

- Stage 2: sv_codegen_test に SV007 の `EXPECT_THROW` ゴールデンケースを追加し、既存 tests/sv 全件・CmCPU全回路でフォールバック経路に到達しない（=全PASS）ことを確認する
- Stage 3: 有効化したテストが対象バックエンドでPASSすること、skipファイルの理由行がランナーで正しく解釈されること（誤ってスキップ解除されないこと）を確認する
- 完了条件: フルスイート（make test）で「理由が記録されていないskip」と「期待値のないテスト」がゼロになる
