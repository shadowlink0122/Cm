---
title: 型付きHIRの単一情報源化（下流での型再推論の禁止）
parent: v0.17.0 Design
---

# 型付きHIRの単一情報源化（下流での型再推論の禁止）

## 概要

TypeCheckerが型を計算するにもかかわらず、その結果がHIRへ完全に載らないため、HIR lowering・MIR lowering・モノモーフ化・各バックエンドが型を再推論している。
「HirExpr.typeは型検査後に必ず非nullかつ非errorであり、下流は型を計算せず参照だけする」という不変条件を確立し、再推論コードを削除する。
rustcのHIR（全ノードがtypeck結果のTypeckResultsで型を引ける）に相当する規律で、B6/B7（メソッド戻り値型void化）・N2（T型残留のptrtoint化）・W5（影の型チェッカー）はすべてこの不変条件の欠如が根因だった。

## 現状の実測と問題

- ASTを2回歩く構造: TypeCheckerがASTを検査して型を計算するが、HirLoweringは別walkで独自に型を付け直す（checkerの結果はast::Expr.typeへの部分的な書き込みでしか伝わらない）。
- MIR loweringにresolve_typedef呼び出しが48箇所・make_error/is_errorの防衛が25箇所あり、型が信頼できない前提のコードが常態化している。
- 型不明時のフォールバックが値破壊として顕在化する: convertOperandの型不明ptrtoint（N2でアドレスが値として出力）、println引数の既定int化（fixup_println_dispatchで事後補正）、既定elem_size=4（レイアウトずれ）。
- モノモーフ化後の宛先ローカル型パッチ（N2修正）や補間の影の型チェッカー（W5修正）は、いずれも「下流で型を復元する」対症療法として追加された。

## 簡素化方針

1. 型検査とHIR loweringの統合: TypeCheckerの走査結果（式型・解決済みシンボル・特殊化情報）を消費しながらHIRを構築する単一walkへ再編し、HirLowering独自の型付けロジックを削除する（compiler-architecture-restructure.mdのcm_typeckがHIRを出力する形）。
2. 不変条件の機械的検証: 型検査成功後のHIRに対し「全HirExpr.typeが非null・非error・TypeAlias解決済み」を検証するデバッグアサート走査を導入する（違反はその場でICE報告）。
3. MIR loweringの再推論禁止: resolve_typedefと型推測分岐を段階的に削除し、HIRの型を read-only に使う。型が必要な新規コードはHIRへ情報を足す（下流で計算しない）規約をレビュー基準にする。
4. ジェネリクスは型付きHIR上の特殊化として扱い、モノモーフ化が名前から型を逆算する必要をなくす（monomorphization-typed-instantiation.mdと連動）。

## 対症療法コードの削除対象（実装完了時）

- fixup_println_dispatch（N2）・mono宛先型パッチ（N2）・annotate_interp_expr_types（W5、補間脱糖でも消える）・convertOperandのptrtointフォールバック・既定int/既定elem_sizeフォールバック群。

## 段階分割

1. 第1段: 不変条件アサート走査を導入し、現状の違反箇所（type null/error到達）を計測・列挙する（挙動変更なしの棚卸し）。
2. 第2段: 違反箇所の上流修正（checkerが型を書く/HIR構築時に必ず付与）を領域別に進め、アサートを常時有効化する。
3. 第3段: 型検査とHIR loweringの単一walk化（AST 2回走査の解消）。
4. 第4段: MIR側の再推論・防衛コード削除と、対症療法コードの撤去。

## テスト計画

- 全スイート+O0/O1検証を各段で完走させる。
- 不変条件アサートをdebugビルドのregressionで常時実行し、新規コードの違反を即検出する。
- B6/B7/N2/W5の回帰テスト群が対症療法削除後も通ることで根治を証明する。

## 進捗

### 第1段（不変条件アサート走査の導入と計測）: 実装済み

- `src/internal/hir/type_audit.cpp` を新設した。HIRプログラム全体（関数・implメソッド・演算子実装・グローバル初期化子・SV initial）を走査し、全HirExprのtype null/errorをノード種別ごとに集計する。`CM_HIR_TYPE_AUDIT=1` でHIR lowering直後に報告し、`=2` で違反サンプル（関数名・種別・内容）も出力する。
- 初回計測: tests/common全517ファイル中59ファイルに違反（null=0・error型のみ。error型は「checkerが走査しない式のexpr.type未注釈がHIR loweringのフォールバックでerror化したもの」）。種別分布はLiteral/Binary/VarRef/Memberに集中し、違反は6クラスに帰着した。

### 第2段（違反の上流修正とアサートの常時有効化）: 実装済み

計測で特定した6クラスをすべて上流（checker注釈またはHIR構築時の型付与）で修正し、コーパス違反0件を達成した。
1. 演算子impl本体が型検査されていなかった（最大クラス。user定義・derive生成の両方でself/other/メンバ/二項が全てerror型）。check_implへメソッドと同型の本体検査ループを追加した。
2. for-in（インデックス脱糖）のupdate式が合成ASTのlower_expr経由で部分式未注釈だった。型付きHIRの直接構築へ置換した。
3. for-in（イテレータ脱糖）の合成ノード2箇所が明示的にtype=nullptrで構築されていた。イテレータ構造体型とそのポインタ型を付与した（iter_letのmove後参照によるSIGSEGVを踏んだため、型の捕捉はmove前に行う）。
4. switch caseパターン値（単一値・範囲・OR）が未走査だった。パターン再帰のinfer_typeを追加した。
5. ジェネリック構造体メソッド（Vector<int>.push等）の実引数が未走査だった。型引数代入済みパラメータ型を期待型としてinfer_type_expectingを追加した。
6. デフォルト引数式とグローバル変数初期化子が期待型なし（または未走査）だった。パラメータ型・宣言型を期待型としてinfer_type_expectingへ接続した。
- 常時有効化: tests/regression/hir_type_audit_test.cpp（7ケース: 基本式・for-in両脱糖・switchパターン・演算子impl・デフォルト引数・構造体メソッド・三項/キャスト）がparse→型検査→HIR loweringを通して違反0を常時検証する。
- 検証: interpreterスイート612件（失敗0）を維持し、checkerの走査拡大（演算子本体・caseパターン・ジェネリック引数）による新規診断の回帰はなかった。

### 第3段（型検査とHIR loweringの単一walk化）: 不採用判断

- 実測に基づき物理的な単一walk化は不採用とする。型の単一情報源は「checkerのinfer_typeが全式をast::Expr.typeへ注釈し（infer_type出口の一括書き込み）、HIR loweringは注釈を唯一の型源として消費する（expr.cpp冒頭の`type = expr.type ? expr.type : make_error()`）」という現行構造で既に成立しており、第1〜2段の完全注釈化+機械的検証で不変条件として確立した。
- 残る利得はAST走査1回分の定数コストのみで、TypeChecker（約20ファイル）とHirLoweringの全面再編・unity buildの再バッチ化という移行コストに見合わない。「下流は型を計算せず参照だけする」という本文書の目的は走査回数ではなく情報源の一意性で担保する。

### 第4段（対症療法・防衛コードの削除）: 処置確定

- annotate_interp_expr_types（W5の影の型チェッカー）: 削除済み（type-resolution-simplification第4段cで補間ミニパイプラインごと削除）。
- convertOperandの型不明ptrtointフォールバック: 現存しない（N2修正時の再編で撤去済みであることを確認）。terminator/invoke.cppに残るptrtointはtypedef不整合の引数変換シムで、型不明フォールバックではない。
- fixup_println_dispatch（N2）とモノモーフ化の宛先ローカル型パッチ（N2）: モノモーフ化が名前ベースのMIR書き換えである限り機能的に必要なため、monomorphization-typed-instantiation.mdの型駆動インスタンス化と同時に削除する（移譲）。
- MIR loweringのresolve_typedef呼び出し（44箇所）: HIRの型がtypedef未解決のまま運搬される現状では必要。「TypeAlias解決済み」不変条件への昇格は型ノード一元化（同上文書）と併せて扱う（移譲）。error防衛は補間ミニパイプライン削除等の再編を経て25箇所から15箇所へ減少している。
