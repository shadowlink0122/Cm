# 実装設計: 宣言の命名規則チェック（check/lint --strict）

## 背景・課題

Cmの識別子はC/C++/Rustの世界標準の命名規則（型はPascalCase、関数・変数はsnake_case、定数はUPPER_SNAKE_CASE）を推奨しているが、コンパイラによる検査は未実装だった（`check_naming_conventions()` はスタブ、linterフィクスチャ `bad_naming.cm` は期待ルールIDが存在しない状態）。

`cm fmt` は整形専任とし、命名規則の検査は `cm check` / `cm lint` に `--strict` オプションで実装する。

## 設計

### CLI

- `cm check --strict <file>` / `cm lint --strict <file>` で命名規則チェックを有効化する
- 既存のグローバルオプション `--strict`（`--force-check` の別名）を再利用し、check/lintコマンド時はTypeCheckerの命名チェックを有効化する
- `cm fmt` は変更しない（整形専任）

### 命名規則（宣言の種類ごとの許容ケース）

| 宣言 | 許容ケース | 例 |
|---|---|---|
| struct / enum / interface / typedef / union型名 | PascalCase | `AdderIo`, `Point` |
| ジェネリック型パラメータ | PascalCase | `T`, `TKey` |
| 関数・メソッド名 | snake_case | `calc_sum` |
| 変数（グローバル/ローカル）・パラメータ | snake_case | `total_count` |
| 構造体フィールド | snake_case | `led_ready` |
| グローバルconst | UPPER_SNAKE_CASE | `CLK_FREQ` |
| ローカルconst | snake_case / UPPER_SNAKE_CASE | `base`, `MAX_N` |
| enumバリアント | PascalCase / UPPER_SNAKE_CASE | `North`, `CTRL_00` |
| モジュール名（module文） | snake_case | `hdmi_out` |

camelCase（`camelCase`）はどの宣言でも許容しない。判定は先頭のアンダースコアを除去した後に行う（`_unused` は snake_case として許容）。

### 除外（チェック対象外）

- `extern struct` とそのフィールド（GowinベンダプリミティブのようにSV出力へ実名で出るため: `OSC`, `TLVDS_D2`, `OSCOUT` 等）
- `extern "C"` ブロック内の関数（Cシンボル名は外部で固定）
- コンストラクタ `self()` / デストラクタ `~self()` / 演算子オーバーロード（名前を持たない）
- `main` 関数
- `__` プレフィックスの内部生成名（`__prelude` 注入宣言等）
- 標準ライブラリ名前空間（`std` / `native` / `js` / `uefi` / `web`）のモジュール内宣言（importプリプロセッサがユーザーコードへインライン展開するため、ユーザーの責任範囲外）

### 診断

- 既存カタログの `L001` naming-convention を使用し、warningレベルで報告する
- メッセージ形式: `<宣言種別> '<名前>' は <期待ケース> 命名規則に従っていません [L001]`
- 既存のlint設定（`.cmlint` 等のConfigLoader）で `L001` の無効化・レベル変更が可能

### 実装

- ケース判定（`is_snake_case` / `is_pascal_case` / `is_upper_snake_case`）を `src/frontend/types/naming_rules.hpp` の自由関数へ分離する（unitテスト可能にする。TypeCheckerの静的メソッドは同関数への委譲に変更）
- `TypeChecker::check_naming_conventions(ast::Program&)` をAST走査として実装し、`check()` の末尾で `enable_naming_check_` が有効な場合に実行する
- 走査対象: トップレベル宣言、ModuleDecl配下（ライブラリ名前空間を除く）、impl内メソッド、関数本体のLetStmt（ネストしたブロック・if/for/while/switch/defer/must内を含む）

## テスト計画

- unitテスト: `tests/unit/` にケース判定関数の単体テスト（snake/pascal/upper判定の境界: 先頭アンダースコア、数字、単一文字、camelCase拒否）
- linterフィクスチャ: `tests/linter/fixtures/invalid/bad_naming.cm` の期待ルールIDをL001へ更新し、種類別の違反（型/関数/変数/定数/フィールド/enumバリアント）を網羅
- 回帰テスト: `--strict` なしでは報告されず、`--strict` で報告されることをE2Eで確認（`tests/linter` 実行スクリプト経由）

## 将来拡張（本設計の対象外）

- `.cmlint` 設定での宣言種別ごとの規則カスタマイズ（例: フィールドをcamelCase許可）
- `cm fmt` との連携による自動リネーム
