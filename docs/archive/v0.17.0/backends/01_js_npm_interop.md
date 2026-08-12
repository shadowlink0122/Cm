# 実装設計: JSバックエンドのnpmパッケージ連携（Phase 1）

## 背景・課題

React/Node等の一般的なWebフレームワーク・ライブラリをCmから活用し、フロントエンド・サーバ開発をCmで行えるようにすることが最終ゴール。
その基盤として、node_modulesのts/jsパッケージ利用と構造体・コールバック・メソッドの互換をPhase 1で確立する。

## 現状調査の結果（v0.16.2時点で既に動作していたもの）

- `use "パッケージ名" { 宣言 }` → `const alias = require("パッケージ名")` の出力と `alias.func(args)` 呼び出し（Node組み込みモジュール・node_modulesの両方）
- 構造体互換: Cm structはJSオブジェクト（フィールド名保持）として渡り、返却オブジェクトもstructで受けてフィールドアクセスできる
- コールバック: 関数ポインタ・ラムダをJS関数へそのまま渡せる

## Phase 1で実装したもの

### 関数型フィールドの呼び出し（obj.field(args)）

JSオブジェクトのメソッドはJSでは「関数値を持つプロパティ」のため、構造体の関数型フィールドをメソッド構文で起動できるようにした。

- **TypeChecker**（`checking/call/method.cpp`）: メソッド解決に失敗した場合のフォールバックとして、構造体の同名フィールドが関数型（`TypeKind::Function`）なら引数を検証し戻り値型を返す
- **HIR**（`HirCall::indirect_callee` 新設）: 呼び出し先を関数値の式として保持する。lower_memberはimplメソッド名へのマングリングより前に関数型フィールドを検出してこの形へ落とす（文字列補間ミニパイプラインでは `seeded_struct_fields_` から解決）
- **MIR**（`expr_call.cpp`）: `indirect_callee` がメンバ式なら `get_member_place` でPlace（`obj.field`）のまま呼び出しオペランドにする。これによりJSバックエンドは `obj.field(args)` を直接出力し、**JSのthis束縛が保持される**（一時変数へ取り出すと `this` がundefinedになる）
- **LLVMコード生成のバグ修正**（`statement/assign.cpp`）: 関数参照代入のSSAショートカットが投影を無視しており、`ops.apply = f` のフィールド代入で構造体ローカルのスロット自体が関数値に上書きされてSEGV/不正GEPになっていた。投影がある場合は通常のstore経路で関数ポインタをフィールドへ書き込む

これによりnative/wasm/jit/jsの全バックエンドで関数型フィールドの代入・差し替え・呼び出し・引数渡しが動作する。

### テスト基盤

- `tests/common/pointer/function_field.cm`: 全バックエンドスイートで関数型フィールドを検証
- `tests/js/ffi/`: ローカルfixtureパッケージ `node_modules/cmtestpkg`（コミット対象、npm install不要）を新設し、ランナーが `NODE_PATH` で解決する。構造体互換・コールバック・メソッドthis束縛・組み込みモジュールの4テスト

## 将来フェーズ（ロードマップは docs/design/js_interop_roadmap.md）

Phase 2以降（TypeScript型定義の出力・Promise/async連携・ESM・React戦略）は前方検討としてロードマップ文書に記載する。
