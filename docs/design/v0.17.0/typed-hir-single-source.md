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
