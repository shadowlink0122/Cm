---
title: 診断・低優先度所見のまとめ
parent: v0.17.0 Design
---

# 診断・低優先度所見のまとめ

## 対象所見

| # | 領域 | 所見 | 状態 |
|---|------|------|------|
| M18 | バックエンド | SVバックエンドで`println`が無診断で捨てられ空moduleが生成される（合成不能文のドロップ警告なし） | 実装済み（合成モジュール内のprintln等のI/O組み込みを警告[SV008]付きでスキップし、未定義関数の無言emitを廃止。段階導入のためまず警告） |
| L1 | 識別子 | グローバル変数のみidサフィックス無しで`sanitizeIdentifier`の多対一縮退衝突がありうる | 実装済み（宣言・参照が共有する`globalVarName`ヘルパで宣言順の連番`_gN`を付与し、ローカルと同じ一意化規約へ揃えた） |
| L2 | 型システム | 文字列補間`{}`が内側の引用符・波括弧を誤パース（`{m.get(Key{name:"x"})}`が壊れる） | 実装済み（MIRのextract_named_placeholdersと型検査のextract_placeholder_exprsを、波括弧深度+引用符状態を追跡するスキャナへ置換。深度0・引用符外の`}`のみを終端、`:`判定も同様） |
| L3 | 型システム | `var`が推論キーワードでなく未解決型名として扱われ紛らわしいエラーになる（`auto`は動作する） | 実装済み（案(a): リポジトリ内に`var`識別子の使用が無いことを確認し、`auto`の別名キーワードとしてKwAutoへ写像。チュートリアル・VSCode文法も更新） |
| L5 | 言語 | fmtはインデント正規化中心（演算子・波括弧の空白は不統一） | 未着手（一般演算子の空白正規化はリポジトリ全体の再フォーマット差分を伴うため見送り。冪等性テストと合わせて別途実施） |
| L6 | 言語 | LSP・パッケージマネージャは未実装、`#[test]`のassertは`assert_eq`が無い | assert_eq/assert_ne実装済み（`std::debug`のジェネリック関数`<T: Eq>`として追加。失敗時に両値を表示してexit(1)。LSP・パッケージマネージャは対象外のまま） |
| L7 | 言語 | 並行処理はnativeバックエンド専用でチャネルはint64スカラのみ | 制約の明文化のみ（拡張はランタイム共通コア化と連動する将来項目） |

## 背景と根本原因

### M18: SVバックエンドで合成不能文が無診断で落ちる

監査の「空moduleが生成される」という表現は厳密には不正確で、実際は**未定義関数`cm_println_string(...)`としてそのまま出力され、合成不能である旨の診断が一切ない**。

- 合成モジュールのCall処理: src/internal/codegen/sv/emit_control.cpp:400-638（`emitBlockRecursive`）。`assert`（:400）・`__builtin_concat`/`__builtin_replicate`（:426）・`__builtin_string_charAt`（:504）を特別扱いし、残りは汎用else（:590-637）で`func_name(args);`をそのまま出力（:634）。println等の非合成呼び出しに対する診断・警告・スキップは無い（末尾コメント:639「その他の関数呼び出しはスキップ」）。
- 診断が出るのはテストベンチ文脈のみ: src/internal/codegen/sv/testbench.cpp:411-427（`#[test]`内ではprintln→`$display`（:423）、その他呼び出しは`SV007`エラーをthrow（:425））。合成モジュール側には対応する診断が存在しない。

**方針**: 合成モジュール本体（emit_control.cpp:590-637の汎用else）で、合成不能な組み込み（println等のI/O）を検出したら`SV007`相当の診断（エラーまたは警告）を出す。テストベンチ側（testbench.cpp:411-427）と同じ判定ロジックを共有し、合成文脈では未定義関数の無言emitをやめる。黙殺禁止インバリアント（監査ロードマップ第2段）と整合させる。

### L1: グローバル変数の識別子サフィックス欠落

- `sanitizeIdentifier`（src/internal/codegen/js/types.hpp:27-60）は多対一縮退を持つ。`@`→`_at_`（:32-34）、`::`→`__`（:37-39）、`[]`→`_arr`（:42-44）、`unsafe="<>, []"`の各文字（`<` `>` `,` 空白 `[` `]`）を全て`_`へ置換（:47-52）。異なる型名が同一識別子へ衝突しうる。
- グローバル変数のみidサフィックス無し: src/internal/codegen/js/emit_function.cpp:397-398（`getLocalVarName`）で`if (local.is_global) return "__global_" + sanitizeIdentifier(local.name);`（id付与なし）。ローカルは`sanitizeIdentifier(name) + "_" + id`（:407）で一意化されるのに、グローバルだけ縮退衝突がサフィックス無しで残る。

**方針**: グローバル変数にも一意id（宣言順の連番等）をサフィックス付与し、`__global_<name>_<id>`とする。ローカルと同じ一意化規約へ揃える。通常構文では未再現の潜在バグだが、機械生成コードで縮退が起きうるため予防的に閉じる。

### L2: 文字列補間のネスト誤パース

- src/internal/hir/string_interpolation.cpp（`StringInterpolationProcessor`）はネストした波括弧・引用符を扱わない。素朴な`find('{')`/`find('}')`のみ。
- `extractInterpolations`:38,45 で`end = str.find('}', pos)`（最初の`}`を終端）。`{m.get(Key{name:"x"})}`は内側`Key{name:"x"}`の`}`で早期終了する。:53 の`content.find(':')`も最初の`:`をフォーマット指定子区切りに誤認し、`name:`をformat_specとして切り出す。引用符・エスケープ・ネスト深度カウントは全経路で不在（`{{`/`}}`エスケープ以外の構造対応なし）。

**方針**: 補間式の抽出を、`find`ベースから「深度カウント付きスキャナ」へ置換する。波括弧のネスト深度を追跡し、文字列リテラル（`"..."`）内の`}`・`{`・`:`を無視する。深度が0へ戻った`}`を式の終端とし、format_spec区切りの`:`はトップレベル（深度0・引用符外）のもののみ採用する。

### L3: varが型推論キーワードでない

- `auto`はキーワード: src/internal/syntax/lexer/lexer.cpp:75（`{"auto", TokenKind::KwAuto}`）。パーサで型推論へ: src/internal/syntax/parser/parser_type.cpp:141-143（`KwAuto`→`TypeKind::Inferred`）、宣言先読みでも型キーワード群に含む（parser_stmt.cpp:175）。
- `var`はキーワードでない: lexer.cpp の`keywords_`に`"var"`エントリが無く`KwVar`トークンも存在しない。`var`は`TokenKind::Ident`として字句解析され、parser_stmt.cpp:177の`Ident`分岐で「カスタム型名」扱いになる。結果`var x = ...`は未解決の型名エラーになる。

**方針**: 2択で検討する。(a)`var`を`auto`の別名キーワードとしてlexer.cpp:75へ追加し、parser_type.cpp:141の`KwAuto`と同じく`TypeKind::Inferred`へ落とす。(b)`var`を予約せず、`Ident`型名の解決失敗時に「型推論には`auto`を使う」旨の診断を出す。破壊的変更回避（既存識別子`var`を使うコードがある可能性）の観点では(b)が安全だが、他言語からの移行者の期待に沿うのは(a)。既存コードでの`var`識別子使用の有無を確認して選ぶ。

### L5: fmtの空白正規化が限定的

- src/internal/fmt/formatter.cpp の`format`（:144-184）は多数のパスを実行する（trim/tabs_to_spaces/normalize_blank_lines/enforce_kr_braces/normalize_indentation/normalize_operator_spacing/wrap_long_lines/align_inline_comments等）。監査の「インデント正規化のみ」は不正確。
- ただし`normalize_operator_spacing`（formatter.cpp:634-725）はカンマ後の空白挿入（:693-699）とパイプ`|`前後の空白（:702-718）の2種のみで、`=` `+` `==` `<`等の一般二項演算子の空白正規化は無い。波括弧内側の空白調整も無い。`enforce_kr_braces`（:298〜）は配置のK&R化のみ。

**方針**: `normalize_operator_spacing`へ一般二項演算子（代入・算術・比較・論理）の前後空白正規化を追加する。冪等性（監査で確認済み）を維持し、文字列リテラル・コメント内は触らない。段階的に演算子カテゴリを増やし、各追加で冪等性テストを回す。

### L6: LSP・パッケージマネージャ・assert_eq未実装

- LSP: src/配下に該当ディレクトリ無し（`*lsp*`/`*server*`・`LanguageServer`/`textDocument`のgrepゼロ）。
- パッケージマネージャ: src/配下に`*package*`/`*pkg*`無し。
- assert: マクロでなく組み込み。組み込み登録 src/internal/hir/lowering/expr.cpp:865（`"assert"`）、実行時は失敗でexit(1)（src/cmd/cm/backend/run.cpp:77）。条件式1個＋任意メッセージのみで、`assert_eq`/`assert_ne`（期待値/実測値差分）はsrc/・libs/全体でゼロ。

**方針**: 本文書の範囲では`assert_eq`/`assert_ne`の追加を優先する（最も小さく実害がある）。組み込み`assert`（expr.cpp:865）の隣に`assert_eq(a, b)`/`assert_ne(a, b)`を追加し、失敗時に期待値・実測値を差分表示してからexit(1)する。全バックエンド（native/js/sv）で同じメッセージ形式にする。LSP・パッケージマネージャは規模が大きく、本文書では対象外（別途大規模設計文書が必要）とだけ記す。

### L7: 並行処理がnative専用・チャネルがint64スカラのみ

- チャネルペイロード型=int64（`long`）: libs/native/sync/channel.cm:8-9,31-51（`send(long handle, long value)`/`recv(long handle, long* value)`）。
- 実装本体はnative限定: libs/native/sync/channel_runtime.cpp・libs/native/thread/thread_runtime.cpp（`libs/native/`配下）。ランタイム束縛 src/internal/codegen/llvm/core/runtime/system.cpp:227-269（`cm_channel_create`/`send`/`recv`/`cm_thread_create`等、コメントで`int64_t value`明記）。
- 他バックエンド非対応: src/internal/codegen/js/・sv/にchannel/spawn実装ゼロ。

**方針**: 本文書では現状の制約を明文化するにとどめ、拡張は段階的に扱う。将来的にはチャネルペイロードをジェネリック化（任意型を搬送、内部でサイズ付きコピー）し、js（Promise/async基盤）・wasmへの並行処理移植を検討する。ただしこれは大規模でランタイム共通コア化（監査ロードマップ第3段）と連動するため、優先度は低く保つ。

## 構文例・出力例

該当する範囲のみ。

L3（対応後、案(a)採用時）:
```cm
var x = 42;      // auto と同じく型推論（現状は未解決型名エラー）
auto y = 42;     // 従来通り動作
```

L6（assert_eq追加後）:
```cm
#[test]
void test_add() {
    assert_eq(add(2, 3), 5);   // 失敗時: expected 5, got 4 のように差分表示
}
```

M18・L1・L2・L5・L7はユーザー向け構文の追加は無い（診断・内部生成・整形の改善）。

## 実装の段階分割

各所見は独立に着手できる。優先度順（実害と修正コストの比）。

1. M18: SV合成モジュールで合成不能文の診断を追加（黙殺解消、testbench.cppの判定を共有）。
2. L2: 文字列補間を深度カウント付きスキャナへ置換（誤コンパイル解消）。
3. L6（assert_eqのみ）: `assert_eq`/`assert_ne`組み込みを追加。
4. L1: グローバル変数へ一意idサフィックス付与。
5. L3: `var`の扱いを決定（(a)キーワード化 or (b)診断改善）し実装。
6. L5: fmtの一般二項演算子の空白正規化を追加。
7. L7: 現状制約の明文化のみ（拡張は将来）。

## テスト計画（tests/common/配下）

- M18: `tests/common/sv/` 相当に、合成モジュールでprintlnを使ったケースを追加し、診断が出る（無言emitされない）ことを確認。svスイート（`make test-sv`）で検証。
- L2: `tests/common/formatting/interpolation_nested_test.cm` + `.expect`: `{m.get(Key{name:"x"})}`のようなネスト波括弧・引用符を含む補間が正しく展開されることを全バックエンドで確認。
- L6: `tests/common/errors/assert_eq_test.cm` + `.expect`（またはmacro/testスイート）: `assert_eq`成功・失敗時の差分メッセージと終了コードを確認。
- L1: `tests/common/global_var/global_name_collision_test.cm` + `.expect`: 縮退しうる名前のグローバルが衝突せず正しく動くことをjsバックエンドで確認。
- L3: `tests/common/basic/var_inference_test.cm` + `.expect`: `var x = ...`が`auto`と同じ結果になる（案(a)）か、明確な診断が出る（案(b)）ことを確認。
- L5: fmtの冪等性テスト（既存枠組み）へ、一般演算子空白の正規化ケースを追加。
- L7: 現状の制約明文化のみのため新規テストは不要（既存のnative並行処理テストを維持）。

## リスクと非互換性

- L3で`var`をキーワード化（案(a)）すると、`var`を識別子（変数名・型名）に使う既存コードが壊れる。破壊的変更のため、既存コードでの`var`使用を確認し、使われていれば案(b)（診断改善のみ、非破壊）を選ぶ。
- L5のfmt空白正規化拡張は、既存の整形結果が変わる。冪等性を維持し、リポジトリ内の既存.cmの再フォーマット差分を確認する。
- M18で診断をエラーにすると、これまで（誤って）通っていたsvビルドが失敗する。まず警告から始め、段階的にエラー化する選択肢を残す。
- L6のassert_eq追加は非破壊（新規組み込み）。LSP・パッケージマネージャは本文書の対象外であり、着手しないことを明記する。
- L1・L2は潜在バグの予防・修正で、正しく動いていたコードの挙動は変えない（誤動作していたケースのみ改善）。

## 関連

- 監査レポート: docs/design/v0.17.0/large-scale-bottleneck-audit.md（M18, L1, L2, L3, L5, L6, L7）
- M18: src/internal/codegen/sv/emit_control.cpp:400-639, src/internal/codegen/sv/testbench.cpp:411-427
- L1: src/internal/codegen/js/types.hpp:27-60, src/internal/codegen/js/emit_function.cpp:397-398
- L2: src/internal/hir/string_interpolation.cpp
- L3: src/internal/syntax/lexer/lexer.cpp:75, src/internal/syntax/parser/parser_type.cpp:141-143, parser_stmt.cpp:175-177
- L5: src/internal/fmt/formatter.cpp:144-184,:634-725
- L6: src/internal/hir/lowering/expr.cpp:865, src/cmd/cm/backend/run.cpp:77
- L7: libs/native/sync/channel.cm, src/internal/codegen/llvm/core/runtime/system.cpp:227-269
