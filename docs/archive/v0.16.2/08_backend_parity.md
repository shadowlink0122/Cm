# 実装設計: バックエンド機能パリティ（JSのundefinedサニタイザ・SVのinterface対応）

## 背景・課題

- `--sanitize=undefined` はMIRレベル計装のためバックエンド非依存だが、JSターゲットはCLI検証で一律拒否されていた
- SVターゲットは `interface` / `impl` メソッドが使えなかった（implメソッドの `self` が構造体ポインタ引数になり、ポインタ非対応のSVで `SV002` エラーになる）

## 設計

### JSのundefinedサニタイザ

- CLI検証（build.cpp）を「js/webはundefinedのみ許可」へ変更（LLVM計装パス・サニタイザランタイムが前提のaddress/thread/memory/boundsは従来どおり拒否）
- MIR計装は既存の `instrument_undefined_checks` をそのまま使用（panic呼び出し・SwitchInt・UnreachableはJSコード生成が既に対応済み）
- **JSのポインタ等値比較のnull安全化**: 従来の `a.__arr === b.__arr && a.__idx === b.__idx` 直接展開は、null（JSの `null`）に対して `.__arr` アクセスでTypeErrorになる潜在バグがあった（null計装ガード自体が落ちる）。null/undefined・fat pointer・オブジェクト参照の3表現を扱う `__cm_ptr_eq` ランタイムヘルパーへ置き換えた（Neは `!__cm_ptr_eq`）

### SVのinterface対応（selfの値渡し化）

`src/internal/codegen/sv/self_param.cpp` の `lower_self_pointer_params` がSVコード生成の直前にMIRを変換する:

1. 構造体ポインタ引数（`*T self`）の型を `T` へ変更し、本体の `(*self).field` アクセスから先頭のDeref投影を除去する
2. 呼び出し側の「`_t = &place; call f(copy(_t))`」を「`call f(copy(place))`」へ書き換え、Ref代入をNop化する（メソッド内でのself転送は型情報のみ更新）
3. 値渡しで意味が変わるケースは診断エラー:
   - `SV010`: selfのフィールドへ書き込むメソッド（値渡しでは呼び出し元に反映されない）
   - `SV011`: interface型変数経由の動的ディスパッチ（vtableはハードウェアへ合成できない）
   - `SV012`: selfポインタ値のメソッド呼び出し以外への逃避（再借用・別変数への保存等）

あわせてSVコード生成の構造体フィールドアクセスを修正した: 従来の `p[0]`（packed structではビット選択になり誤った値を読む）を、構造体定義（`MirProgram::structs`）から引いたメンバ名アクセス `p.x` へ変更（ネスト構造体の型追跡も追加）。

### import/export構文のバックエンド互換性（調査結果）

import/export/moduleはプリプロセッサ層（バックエンド分岐より前）で処理されるため、構文は全バックエンドで同一。
同一の `export` 関数 + `import ./lib/mathutil as mu;` がjit/native/wasm/js/svの5バックエンドすべてで動作することを実証した（`use js {}` / `use "package" {}` のFFIブロックのみ意図的にプラットフォーム固有で、`//! platform:` ディレクティブで分岐する）。

## テスト計画

- SV: `tests/sv/interface/` 新設 — `method_call`（複数メソッドの値検証、iverilogシミュレーション）・`mutating_self`（SV010の失敗を`.error`マーカーで期待）
- JS: サニタイザE2Eへ `undefined/js` 3ケース（ゼロ除算panic・null参照panic・正常系無影響）と `bounds×js` 拒否を追加
- 回帰: SVフルスイート・JSフルスイートで既存テストの無影響を確認
