# 特殊化後正準化パスの一般機構化とユニオン型引数の正準エンコード（リファクタリング提案）

## 概要

総称関数の本体は型パラメータTが未確定のままMIRへローワされるため、HIR段の型駆動脱糖（配列/スライス等値の内容比較展開・ユニオン等値のタグ+ペイロード比較展開・集約print診断など）が総称本体内の式には適用されない。
特殊化はローワ済みMIRへの型置換であり、置換後に「本来なら脱糖されていたはずの生命令」が残る——この族の第1号が総称derive特殊化のスライス比較SIGSEGV（生MIR Eqがcodegenのstrcmp catch-allへ落ちた）で、対処として特殊化後正準化`canonicalize_specialized_equality`（mono/specialize.cpp）を新設した。
現状この正準化は等値Eq/Ne×ユニオン/動的スライスに限定した個別対応であり、同族の穴（総称本体内の他の型駆動脱糖対象）が特殊化のたびに再発しうる構造は残っている。
あわせて、ユニオンを型引数に渡す特殊化は特殊化キー（typekey）にユニオンの正準エンコードがなく、typedef名（`Box__IU`）と表示形（`Box__int | string`＝JS識別子として不正）の別特殊化に分裂するため、現在は特殊化時診断で塞いでいる。

## 実測

- `Box<int[]>`のEq: 総称本体の`self.v == other.v`が生MIR Eqのまま特殊化され、LLVMのptr-ptr Eq catch-all（cm_strcmp）がスライスヘッダをC文字列として読んでいた（環境依存SIGSEGV。2026-08-11修正済み）。
- `Box<IU>`（typedef IU = int | string）のEq: 生Eqがタグのみ比較になり`Box{v:1} == Box{v:2}`がtrueの誤値、さらに同一ユニオンがtypedef名と表示形で二重特殊化された（現在は「union type arguments are not supported for derive specialization」の診断で拒否）。
- 型駆動脱糖のHIR側実装箇所: 配列/スライス等値・ユニオン等値・集約整形診断・HOFの要素型ディスパッチ（slice_dispatch.hpp）——いずれも「Tでは判定不能・特殊化後なら判定可能」の同型構造を持つ。

## リファクタリング方針

- 特殊化後正準化をモノモーフィゼーションの正式な後段パスとして一般化する: 「特殊化で型が確定したローカルに対し、HIR脱糖と同じ判定表で生命令を正準形へ書き換える」走査を1箇所に集約し、等値以外の書き換え（将来の整形・ハッシュ等）も同じ走査へ追加できる形にする。
- 書き換えロジックはHIR/MIRローワの既存正準実装（cm_lower_union_equality・cm_slice_equal呼び出し生成）を再利用し、正準化パス内での再実装を禁止する（現行のcanonicalize_specialized_equalityの方式を踏襲）。
- typekeyへユニオンの正準エンコード（例: `$U`+変種数+各変種キーの長さ接頭辞形式）を導入し、typedef名を実体解決してからエンコードすることで同一ユニオンの特殊化を単一化する。
- エンコード導入後、derive特殊化のユニオン型引数診断を解除し正常系へ昇格する（宣言フィールドのユニオンEqは既に動作しており、`tests/common/interface/derive/union_field.cm`が境界テスト）。

## 段階分割

1. 正準化パスの一般機構化: canonicalize_specialized_equalityを「判定表+書き換え登録」の枠組みへ再編し、現行の等値書き換えを最初の登録項目にする（挙動同一）。
2. typekeyのユニオン正準エンコード導入とtypedef実体解決: struct_symbol_key/arg_symbol_keyがユニオン型引数を受理し、単一の特殊化キーへ収束することをunitで固定する。
3. ユニオン型引数deriveの解禁: validate_derive_instantiationの診断を解除し、`Box<IU>`のEqを全バックエンドで検証する（エラーテストを正常系テストへ移設）。

## リスク

- typekeyエンコードの追加は特殊化シンボル名の生成規約に触れるため、フラット名全廃（mono-flat-name-elimination）の移行順序と干渉しないよう、$エンコード枠組みの中で完結させる。
- 正準化パスの走査対象拡大は特殊化コストを増やす（対象命令種を書き換え登録から導出し、登録がない命令種は走査しない）。

## テスト計画

- unit: ユニオン型引数のtypekeyエンコードがtypedef形と表示形で同一キーに収束すること。
- regression: 特殊化後正準化パスが生Eq/Neを書き換えたMIRの形（cm_slice_equal/タグ比較CFG）。
- integration: `Box<IU>`のEq（等値・不等・変種違い）を全バックエンド一致で検証し、既存のgeneric_slice_arg・union_fieldと合わせてマトリクス化する。

## 検出経緯

総称derive特殊化のスライス比較SIGSEGV修正（2026-08-11）で、個別対応の正準化パスを新設した際に一般化の必要性とユニオン型引数の前提不足を確認し、恒久解として起案した。
