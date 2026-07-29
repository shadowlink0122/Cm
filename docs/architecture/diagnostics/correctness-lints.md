# 正しさ検査のlint群（確定代入・return網羅・use-after-move）

Cmの型検査は、型整合に加えて「黙って壊れる」クラスのバグを静的に捕まえる正当性系lintを持つ。
本文書は、確定代入解析（未初期化変数のフロー考慮検出）、return網羅検査、use-after-move診断の実装、E/W/L番号によるエラーコード体系、および`--strict`での警告→エラー昇格の方針を記述する。

## 概要

- 確定代入解析は`TypeChecker`の`initialized_variables_`集合をif分岐でfork/joinさせ、「生き残る全経路で初期化された変数」だけを合流後に初期化済みとみなす（src/internal/types/checking/stmt.cpp:522-563）。
- return網羅は構造的終端判定`cm_stmts_terminate`で、非void関数の一部経路が値を返さず末尾へ到達するケースを診断する（src/internal/types/checking/stmt.cpp:23-120、decl.cpp:1082-1095）。
- use-after-moveは`Scope`のシンボルが持つ`is_moved`フラグで移動済み変数の使用をエラーにする（src/internal/types/scope.hpp:23、src/internal/types/checking/expr/primary.cpp:452-458）。
- これらの診断はcheck/lintコマンドで警告として常時有効になり、`--strict`ではエラーへ昇格する。通常コンパイルの挙動は変えない（破壊的変更回避の段階導入）。
- 診断コードはE（エラー）/W（警告）/L（lint提案）の番号帯で`DiagnosticCatalog`に一元登録され、本文はi18nカタログの`MsgId`を参照する（[i18n-messages.md](i18n-messages.md)）。

## データ構造とアルゴリズム

### 確定代入解析（definite assignment）

追跡対象は関数ローカル変数で、状態は`initialized_variables_`（初期化済み変数名の集合。src/internal/types/checking/checker.hpp:291）である。

- 宣言時初期化・代入・関数パラメータ（decl.cpp:1067）・selfは`mark_variable_initialized`で集合へ追加される（src/internal/types/checking/utils/diagnostics.cpp:70-72)。
- 識別子参照時に`check_uninitialized_use`が集合を照合する（primary.cpp:433、diagnostics.cpp:74-102）。グローバル変数・定数は宣言時点で初期化されるため対象外で、ローカル変数のフローだけを見る（diagnostics.cpp:87-91）。
- if文はfork/joinで分岐を合流する。分岐前の集合を保存し、then/elseをそれぞれ評価した後、returnやbreak等で終端した分岐は合流に参加させず、両分岐が生き残る場合は積集合を取る。

```cpp
// src/internal/types/checking/stmt.cpp:548-562（要約）
    // 合流: return等で終端した分岐は合流に参加しない
    if (then_terminates && else_terminates) {
        initialized_variables_ = before_init;
    } else if (then_terminates) {
        initialized_variables_ = else_init;
    } else if (else_terminates) {
        initialized_variables_ = then_init;
    } else {
        // then_init と else_init の積集合
    }
```

この設計が防ぐのは「片方の分岐だけで初期化された変数が合流後も初期化済みと誤認され、未初期化読み取りが無警告で通る」クラスのバグである（フラット集合だけでは検出できない）。
集約型（構造体・固定長配列・スライス）はランタイムのゼロ初期化/暗黙構築が保証されるため対象外とし、偽陽性を避ける。
ループ本体の初期化は意図的にフラット扱い（ループ内初期化を合流後も初期化済みとみなす）で、偽陰性側に倒している。厳密化するとループ経由初期化の既存コードが大量に警告されるためで、MIR CFG上のデータフロー化は将来課題である（設計は[アーカイブ文書](../../archive/v0.17.0/definite-assignment-and-correctness-lints.md)参照）。

### return網羅検査（return coverage）

`cm_stmts_terminate`（stmt.cpp:23-120）は文列が「その先へフォールスルーしない」ことをASTの構造で判定する。

- 終端とみなす文: `return`、`exit(code)`呼び出し、then/else両方が終端するif、defaultを持ち全ケースが終端するswitch、breakを含まない`while(true)`（イベントループ。stmt.cpp:94-105）、終端するブロック/mustブロック。
- `for_function`フラグで「関数からの脱出」と「分岐からの脱出（break/continueを含む）」を区別し、同じ判定器を確定代入の分岐終端判定（stmt.cpp:532、544）と共用する。

検査本体は`check_function`の末尾にあり、非void関数が構造的に終端しない場合に`TypeNotAllPathsReturn`を発行する。

```cpp
// src/internal/types/checking/decl.cpp:1084-1095（要約）
        bool is_non_void = func.return_type && ... != ast::TypeKind::Void && ... != Inferred;
        bool exempt = func.is_extern || func.is_always || func.is_async || func.body.empty() ||
                      func.name == "main";
        if (is_non_void && !exempt && !cm_stmts_terminate(func.body, true)) {
            if (enable_naming_check_) {
                error(func.name_span, i18n::msgf(i18n::MsgId::TypeNotAllPathsReturn, func.name));
            } else {
                warning(...);
            }
        }
```

この検査が防ぐのは「非void関数が値を返さず末尾から抜け、下流バックエンドがスタックゴミ・未定義値を返す」クラスのバグである。

### use-after-move（線形フロー診断）

移動状態はスコープのシンボル自体が持つ（`Symbol::is_moved`。scope.hpp:23、親スコープへ伝搬するmark/unmark/照会はscope.cpp:107-131、175-181）。
移動元は式検査中に`mark_variable_moved`でマークされ（変数の値渡し・フィールドmove等。primary.cpp:231、254）、以後の識別子参照は`check_use_after_move`が`TypeUseAfterMove`エラーを発行する（primary.cpp:436、452-458）。
確定代入・未使用変数（W001）・const推奨と同じく識別子参照の一点（`infer_ident`。primary.cpp:421-441）に検査が集約されており、新しい式種を追加しても参照がこの経路を通る限り診断が漏れない。

### エラーコード体系

全診断は`DiagnosticCatalog`（src/internal/diagnostics/catalog.hpp:67-94）へ`{ID, 名前, 既定レベル, MsgId, 検出段階, 自動修正可否}`で登録される。番号帯はカテゴリを表す。

| 帯 | カテゴリ | 定義箇所 |
|---|---|---|
| E001-E099 | 構文エラー | src/internal/diagnostics/definitions/errors.hpp:20-28 |
| E100-E199 | 型エラー（E100 type-mismatch・E101 undefined-variable等） | errors.hpp:30-41 |
| E200-E299 | 所有権エラー（E200 use-after-move・E201 modify-while-borrowed） | errors.hpp:43-48 |
| E300-E399 | ポインタエラー | errors.hpp:50-64 |
| E400-E499 | ジェネリクスエラー | errors.hpp:66-80 |
| E500-E599 | enum/matchエラー（E500 non-exhaustive-match等） | errors.hpp:82-96 |
| E600-E699 | リテラル/定数エラー | errors.hpp:98-106 |
| W001-W499 | 警告（W001 unused-variable・W002 unreachable-code等） | warnings.hpp:20-82 |
| L001-L499 | lint提案（L001 naming-convention・性能・スタイル等） | lints.hpp:20-83 |

診断レベルは`DiagnosticLevel`（Error/Warning/Suggestion/Hint/Note/Help。src/internal/diagnostics/levels.hpp:11-25）、検出段階は`DetectionStage`（Lexer〜Codegen。levels.hpp:28-35）で表す。
`DiagnosticEngine`はルール単位の無効化とレベルオーバーライドを持ち（src/internal/diagnostics/engine.hpp:50-53）、`report`時に`MsgId`から現在言語の本文を組み立てる（engine.cpp:16-36）。
check/lintの出力側は、メッセージ末尾の`[W001]`形式からルールIDを抽出してインラインコメントによる行単位無効化と突き合わせる（src/cmd/cm/check.cpp:148-159）。

### --strictでの警告→エラー昇格

方針は「新しい正当性診断はまず警告で導入し、標準ライブラリとtests/commonの全数で発火ゼロを確認してから`--strict`でエラー昇格する」である。

- check/lintは`set_enable_lint_warnings(true)`で正当性系を含むlint警告を常時有効化する（check.cpp:133）。
- `--strict`（内部フラグは`opts.force_check`）は`set_enable_naming_check(true)`を立て（check.cpp:135-137）、確定代入（diagnostics.cpp:94-101）とreturn網羅（decl.cpp:1088-1095）がこのフラグで`warning`から`error`へ切り替わる。命名規則チェック（L001）も同フラグで有効になる。
- use-after-moveは段階を経ず常時エラーである（所有権違反は実行時の破壊に直結するため）。
- 通常コンパイル（`cm build`/`run`）は`--force-check`/`--strict`指定時のみ非export importの警告等を有効化する（src/cmd/cm/build.cpp:179-182）。

## 実装箇所

| ファイル | 役割 |
|---|---|
| src/internal/types/checking/checker.hpp | `initialized_variables_`・move追跡・lint/strictフラグの宣言 |
| src/internal/types/checking/stmt.cpp | `cm_stmts_terminate`・if分岐のfork/join合流 |
| src/internal/types/checking/decl.cpp | 関数末尾のreturn網羅検査・パラメータの初期化済み登録・関数単位のクリア |
| src/internal/types/checking/utils/diagnostics.cpp | `check_uninitialized_use`・未使用変数（W001）・const推奨の実装 |
| src/internal/types/checking/expr/primary.cpp | 識別子参照での未初期化/use-after-move検査・move元のマーク |
| src/internal/types/scope.cpp | `Symbol::is_moved`の伝搬（mark_moved/unmark_moved/is_moved） |
| src/internal/diagnostics/definitions/ | E/W/L診断コードのカタログ登録（errors.hpp・warnings.hpp・lints.hpp） |
| src/internal/diagnostics/engine.cpp | 報告・レベルオーバーライド・出力整形 |
| src/cmd/cm/check.cpp | check/lintでのlint有効化と`--strict`昇格の配線 |

## 落とし穴とケア

- **不変条件: 終端した分岐は合流に参加しない**。`int v; if (!ok) { return -1; } v = ...;`型のガード節を誤検出しないため、fork/joinの合流規則（stmt.cpp:548-562）を変更する際は終端判定との整合を必ず保つ。
- **不変条件: 偽陽性はエラー昇格の前に潰す**。正当性系diagはエラー化すると正しいコードを弾くため、昇格前にtests/commonとlibs/stdの全数で発火ゼロを確認する運用を維持する（`while(true)`イベントループ・集約型のゼロ初期化・ループ経由初期化が既知の偽陽性源）。
- **`initialized_variables_`は関数単位でクリアする**（decl.cpp:1099）。クリア漏れは前の関数の初期化状態が漏れて偽陰性になる。
- **識別子参照の検査一点集約を崩さない**。未初期化・use-after-move・未使用マークは`infer_ident`（primary.cpp:421-441)に集約されている。識別子解決の新経路を追加する場合は必ず同じ検査を通すこと。
- **診断コードの追加はカタログ経由**。新しいE/W/L番号は該当番号帯のdefinitionsファイルへ登録し、本文は`MsgId`を新設してi18nカタログに置く。メッセージ文字列の直書きは言語切替と行単位無効化の対象外になる。
- **回帰テスト**: tests/common/errors/use_after_move.cm・move_field_then_use.cm・move_then_interpolate.cm・borrow_then_move.cm（move系負テスト）、tests/i18n/h6_strict_promotion.cm（`--strict`でのエラー昇格）、tests/linter/（lint設定・無効化コメントの機能テスト）。

## 関連資料

- [i18n-messages.md](i18n-messages.md) — 診断本文のi18nカタログと`MsgId`接続
- [../../archive/v0.17.0/definite-assignment-and-correctness-lints.md](../../archive/v0.17.0/definite-assignment-and-correctness-lints.md) — 確定代入・return網羅の設計とMIR CFG化の将来課題
- [../../archive/v0.17.0/misc-diagnostics-and-low-priority.md](../../archive/v0.17.0/misc-diagnostics-and-low-priority.md) — 黙殺禁止インバリアントに関する監査所見の記録
