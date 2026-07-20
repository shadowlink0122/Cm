---
title: 確定代入解析とreturn網羅解析（正当性系lint）
parent: v0.17.0 Design
---

# 確定代入解析とreturn網羅解析（正当性系lint）

本文書は監査レポート `docs/design/v0.17.0/large-scale-bottleneck-audit.md` のH6・L4に対する実装設計である。
中核は「各変数が使用前に確実に代入されるか（definite assignment）」と「非void関数の全経路がreturnで終わるか（return coverage）」を、CFGベースのデータフロー解析としてMIR上に実装することである。

## 対象所見

| # | 領域 | 所見 | 状態 |
|---|------|------|------|
| H6 | 言語 | 未初期化変数の読み取り・非void関数のreturn漏れが無警告（definite assignment解析なし） | 段階1実装済み（型検査のif/else確定代入をfork/join化し片側分岐初期化の合流後使用を警告。return網羅は構造的終端判定`cm_stmts_terminate`で警告。いずれもlint警告有効時のみで、エラー昇格・ループのフロー精密化・MIR CFGベース化は残課題） |
| L4 | 言語 | lintは未使用変数・const提案等のスタイル系のみで、正当性系（未初期化・return漏れ・恒真条件・縮小キャスト）が無い | 一部実装（未初期化のフロー考慮・return漏れ・縮小キャスト(M4)・境界なし演算子(L8)が揃った。恒真条件は未着手） |

## 背景と根本原因

### 既存の未初期化チェックはフロー非依存

型検査は`TypeChecker::check_uninitialized_use`（src/internal/types/checking/utils/diagnostics.cpp:74-97）を持つが、これは`initialized_variables_`という単一のフラット集合（checker.hpp:273）への登録有無を見るだけで、制御フローを考慮しない。

- 代入・宣言初期化・パラメータ・selfは`mark_variable_initialized`（diagnostics.cpp:70-72）で集合に無条件追加される。
- `check_if`（src/internal/types/checking/stmt.cpp:336-357）は分岐ごとに`scopes_.push()/pop()`するが、`initialized_variables_`は分岐で分岐・合流（fork/join）しない。このため片方の分岐だけで初期化された変数が、合流後も初期化済みと誤認される（偽陰性）。
- 集合は関数単位でしかクリアされない（decl.cpp:820, 833, 855, 1051）。
- 実際に呼ばれるのは識別子参照時の1箇所のみ（src/internal/types/checking/expr/primary.cpp:407）で、警告は`enable_lint_warnings_`が有効なときだけ、かつErrorではなくWarningである。

つまり現状は「宣言だけして未代入で読む」単純ケースすら分岐が絡むと検出できず、正しいdefinite assignmentではない。

### return網羅解析が存在しない

`TypeChecker::check_return`（stmt.cpp:277-334）は各`return`文について、戻り値の型整合（stmt.cpp:284-306）と、値なしreturnが非voidでないか（stmt.cpp:330-333）を検査するだけである。
関数のすべての制御フロー経路がreturnに到達するか（末尾フォールスルーの有無）は一切見ていない。
このため非void関数が値を返さずに末尾から抜けても無診断で、下流バックエンドでスタックゴミ・未定義値が返る（監査H4・C1と同じ「黙って壊れる」系統）。

### MIRには利用可能なCFGが既に存在する

MIRは基本ブロックCFGを備えており、この種のデータフロー解析の自然な実装先である。

- `BasicBlock`（src/internal/mir/nodes.hpp:383）は`predecessors`/`successors`（nodes.hpp:389-390）と`terminator`（nodes.hpp:386）を持つ。
- `MirTerminator`（nodes.hpp:323）は`Return`・`Goto`・`Switch`・`Unreachable`の種別を持つ（nodes.hpp:372-375）。
- `MirFunction`（nodes.hpp:435）は`basic_blocks`（nodes.hpp:454）を保持する。
- 既存の解析基盤として`src/internal/mir/analysis/dominators.{hpp,cpp}`と`loop_analysis.{hpp,cpp}`があり、CFG走査ユーティリティを再利用できる。

## 設計方針

正当性系解析をMIR上の独立した解析パスとして新設する（`src/internal/mir/analysis/definite_assignment.{hpp,cpp}` と `return_coverage.{hpp,cpp}`）。
MIRを選ぶ理由は、明示的な基本ブロック・terminator・predecessor/successorが既に整備されており、AST/HIRのように制御フローを再構築する必要がないためである。
MIRのSpanは保持されているため、診断の行番号は劣化しない。

解析はモノモーフ化後・コード生成前のMIRに対して、`compileWithModuleInfo`のcodegen手前で走らせる。

### 確定代入解析（definite assignment, H6）

前方向データフロー解析。各基本ブロック入口で「その地点で確実に代入済みの局所変数（MIRローカル/スロット）集合」を求める。

- 束（lattice）: 局所変数集合。TOPは全変数集合、BOTTOMは空集合。
- meet（合流）: 前任ブロックのOUTの**積集合**（intersection）。ある変数は、その地点へ至る全経路で代入されている場合のみ「確定代入済み」。
- 遷移関数: ブロック内を順に走査し、代入命令（Store/Assign先ローカル）で集合に追加、読み取り命令（Use/Load元ローカル）で「集合に無ければ未確定使用」として診断候補にする。
- entryブロックの初期集合: 関数パラメータ・selfを含む（これらは常に代入済み）。
- ループ: 後退辺があっても積集合meetは単調で不動点に収束する。ループ本体で初めて代入される変数は、ループ入口では未確定として正しく扱われる。

これにより「片方の分岐だけで初期化された変数の合流後使用」を偽陰性なく検出できる。

診断は既定でエラー（`--strict`なしでも正当性違反として扱う）とするが、後方互換のため段階1では警告として導入し、段階移行でエラー化する（リスク節参照）。

### return網羅解析（return coverage, H6）

非void関数について、entryから到達可能な全経路がreturnで終わることを検証する。

- MIRのterminatorを走査し、`Return`以外で関数を抜ける経路（後続ブロックを持たず`Unreachable`でもない末尾、または非voidなのに値なしで抜ける経路）を検出する。
- 実装は後方到達可能性で行う: `Return` terminatorを持つブロックから逆辺（predecessor）を辿って「returnへ到達できるブロック集合」を求め、entryから到達可能かつこの集合に含まれないブロックがあれば「returnしない経路あり」と診断する。
- `while(true)`など終端しない無限ループ（監査H13で正当と確認済み）は、`Return`にも到達せず`Unreachable`terminatorも持たないため、フォールスルーとは区別する必要がある。無条件無限ループの後続が到達不能であることをCFGで確認し、偽陽性を避ける。

### スタイル系との分離（L4）

L4は「lintが正当性系を欠く」という指摘であり、H6の未初期化・return漏れがその中核を占める。
残る恒真条件（`if(true)`等）・縮小キャストのうち、縮小キャストのリテラルケースは既に部分実装がある（整数リテラルの縮小キャスト警告、src/internal/types/checking/expr/primary.cpp:179-180, 監査M4）。
本設計では正当性系lintを次の分類で整理し、H6の2解析を第一級で追加する。恒真条件・非リテラル縮小の検出は将来拡張として枠だけ定義する（構文例・出力例節参照）。

## 構文例・出力例

### H6: 分岐で片側だけ初期化された変数の使用（新規に診断）

```cm
int classify(int x) {
    int label;              // 未初期化宣言
    if (x > 0) {
        label = 1;          // then でのみ代入
    }
    return label;           // else 経路では未代入 → 診断
}
```

修正後の期待診断:

```
example.cm:6:12: エラー: 変数 'label' は使用前に全経路で初期化されていない可能性があります
```

### H6: 非void関数のreturn漏れ（新規に診断）

```cm
int sign(int x) {
    if (x > 0) {
        return 1;
    }
    // else 経路が return せず末尾に到達 → 診断
}
```

修正後の期待診断:

```
example.cm:1:1: エラー: 非void関数 'sign' の一部の経路が値を返さずに終了します
```

### L4: 恒真条件（将来拡張、枠のみ）

```cm
if (true) { ... }  // 将来的に W: 条件が常に真です（本設計では枠のみ定義）
```

## 実装の段階分割

1. 段階1: `src/internal/mir/analysis/definite_assignment.{hpp,cpp}`を新設し、前方積集合データフローを実装。まずWarningとして`compileWithModuleInfo`のcodegen手前に接続する。既存のフロー非依存`check_uninitialized_use`（diagnostics.cpp:74）は当面残置し、二重報告を避けるため無効化する。
2. 段階2: `src/internal/mir/analysis/return_coverage.{hpp,cpp}`を新設し、後方到達可能性でreturn漏れを検出。無限ループの偽陽性除外を含める。
3. 段階3: 両解析の診断をWarningからErrorへ昇格（段階移行。標準ライブラリ・tests/commonの全数緑を確認してから）。
4. 段階4（L4残り）: 恒真条件・非リテラル縮小キャストの検出枠を追加（別解析、優先度低）。

## テスト計画（tests/common/ 配下）

- tests/common/errors/definite_assignment_branch/ — 片側分岐初期化・ループ内初期化・switch各armの網羅を検証（H6・正/負両テスト）。
- tests/common/errors/use_before_init/ — 宣言のみで読む単純ケースと、全経路初期化済みで通る正ケース。
- tests/common/errors/return_missing_path/ — if/else・match・ループを含む非void関数のreturn漏れを検証（負テスト）と、全経路returnの正ケース。
- tests/common/errors/infinite_loop_no_return/ — `while(true)`イベントループ関数がreturn漏れと誤検出されないことを検証（H13との整合、正ケース）。
- 解析器自体のユニット/回帰: 手組みMIR（基本ブロック＋terminator）で積集合meetと後方到達可能性の単体を検証（tests/unit/ または tests/regression/、MIR最適化パス検証の既存様式に倣う）。
- 全バックエンドで診断の有無が一致することを確認する。

## リスクと非互換性

- **後方非互換**: 従来コンパイル・実行できていた「未初期化読み取り」「return漏れ」コードがエラーになる。CLAUDE.mdの破壊的変更回避方針に沿い、段階1はWarningで導入し、tests/commonと標準ライブラリの全数調査後にError昇格する。
- **偽陽性リスク**: 無限ループ・`match`の網羅（既存の網羅チェックは健全と監査で確認済み）・早期`return`を含む複雑CFGで、積集合meetや後方到達可能性が過剰検出しないよう、無限ループ除外と到達可能性の厳密判定を要する。偽陽性は正当なコードを弾くため、Error昇格前に十分な負テスト整備が必須。
- **解析対象の位置**: MIRはモノモーフ化後であり、ジェネリック実体ごとに解析が走る。同一ジェネリック本体の重複解析はコンパイル時間に影響しうるため、必要なら実体単位のキャッシュを検討する（監査H14のコンパイル時間テーマと関連）。
- MIRのローカル/スロット表現と元のソース変数名の対応が失われている箇所があると診断メッセージが不親切になる。Spanは保持されるため位置は出せるが、変数名解決の欠損時はSpanベースのメッセージにフォールバックする。

## 関連

- 監査レポート: `docs/design/v0.17.0/large-scale-bottleneck-audit.md`（H6・L4、推奨対応ロードマップ第3段の「definite assignment/return網羅解析」）
- 関連所見H13（`while(true)`の複雑度ガード誤検出）とは無限ループの扱いで接点があり、return網羅解析の偽陽性除外設計を共有する。
- 実コード: src/internal/mir/nodes.hpp, src/internal/mir/analysis/（dominators, loop_analysis）, src/internal/types/checking/utils/diagnostics.cpp, src/internal/types/checking/stmt.cpp

## 実装記録（段階1）

設計ではMIR CFG上のデータフロー解析を提示したが、段階1は型検査（AST）上のfork/joinとして実装した。
既存の`initialized_variables_`集合・診断基盤・Span解決をそのまま使え、警告位置と変数名がソースの語彙で出せるためである。

- `check_if`で分岐前の集合を保存し、then/elseそれぞれの結果を積集合で合流する。returnやbreak等で終端した分岐は合流に参加しない（`int v; if (!ok) { return -1; } else { v = ...; }` の後続使用を誤検出しない）。
- return網羅は`cm_stmts_terminate`（構造的終端判定。if/else両終端・else付き全ケース終端switch・while(true)無break・exit()を終端と見なす）で非void関数の末尾フォールスルーを警告する。
- ループ本体の初期化は従来どおりフラット扱い（ループ内初期化を合流後も初期化済みとみなす）。C#型の厳密なDAへ寄せるとループ経由初期化の既存コードが大量に警告されるため、段階1では偽陰性側に倒した。

残課題: ループ・switchの合流精密化、警告→エラー昇格、MIR CFGベースへの移行（ジェネリック実体単位の解析）、恒真条件検出。
