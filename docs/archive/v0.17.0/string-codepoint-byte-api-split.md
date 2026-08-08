# R2: 文字列APIのコードポイント/バイト単位不一致（len()とcharAt()の分裂・バックエンド差）

**ステータス:** 修正済み（構文網羅バグ調査で検出）
**重大度:** High

## 症状（実測: cm 0.17.0、プローブ `.tmp/bughunt6/stdlib/`）

H9で`len()`をUTF-8コードポイント数へ変更したが、`charAt(i)`が生バイトを返すため、両者の添字単位が不一致になっている。非ASCII文字列を`len()`+`charAt()`で走査する全コードが破綻する。

実測（jit O0）: `"日本語".len()`=3（コードポイント数）だが`"日本語".charAt(0)`=230（UTF-8先頭バイト0xE6）。この不整合により`std::json`の`json_parse`は非ASCII文字列を全て-1で失敗する（パーサのループが閉じ引用符へ到達する前に`pos >= len`で打ち切られる）。**jsバックエンドは正常**（`json_parse("\"日本語\"")`→`["日本語"]`）に動くため、native/jit=失敗・js=成功のバックエンド分岐になっている。

## 影響範囲

`len()`とインデックスアクセスを組み合わせて文字列を走査するコードすべて。stdlibでは`std::json`が顕在化したが、ユーザーコードの文字列処理・パーサ全般に潜在する。H9で`substring`/`slice`/`indexOf`の添字はコードポイント単位へ統一されたが、`charAt`/`codepoint_at`の単位定義とlen()との組み合わせ規約が抜けている。

## 期待仕様（提案）

`len()`（コードポイント数）と組み合わせる要素アクセスもコードポイント単位に揃える。バイト単位が必要な走査には`byte_len()`+バイトアクセスを対にする。すなわち「コードポイント系（len/codepoint_at/chars）」と「バイト系（byte_len/byte_at）」の2系統に分離し、混用を型または命名で防ぐ。stdlibのjsonパーサはどちらか一方の系統で書き直す（バイト走査が高速）。

## 修正方針

- 文字列APIの単位規約をCANONICAL_SPECへ明文化（H9の変換表の隣）。
- `charAt`の単位を確定（コードポイント返しに変更、またはバイト系へ改名）し、jsonパーサをバイト系走査へ統一する。
- native/js/wasmの実装を同一単位へ揃える（H9でlenは揃ったがcharAt系が取り残された）。

## テスト計画

`tests/common/strings/`へ: 非ASCII文字列を`len()`+要素アクセスで走査するテストがnative/jit/wasm/jsで一致・jsonの非ASCIIラウンドトリップの4系一致。マルチバイト境界（2/3/4バイト文字混在）の走査回帰。
## 実装記録（2026-08-08）

「コードポイント系とバイト系の2系統分離」の提案通りに実装した。

- charAt/at: バイト添字→**コードポイント添字**へ変更（len()と同単位）。戻り型charは1バイトでASCIIのみ忠実に表現できるため、ASCII（<=0x7F）のコードポイントのみ値を返し、非ASCIIコードポイントと範囲外は'\0'を返す（値が必要な場合はcodepoint_at）。ASCII文字列では従来と同値のため既存コードは非破壊。jsバックエンドは従来charCodeAt（UTF-16単位）でnative（バイト単位）とも分裂していたが、コードポイント定義で全経路一致になった。
- byte_at(i)を新設: バイト系（byte_len()と対）の生バイトアクセス。バイト添字の生バイト値0..255をintで返し範囲外は0（i8のchar返しは0x80以上で符号・出力表現がバックエンド間で割れるためintを選択）。native/wasmランタイム・js（TextEncoder）・SV（ASCIIバイト==コードポイントのためcharAtと同一生成）・checker・HIR・ビルトインレジストリへ追加。
- std::jsonをコードポイント走査へ書き直し: posをコードポイント添字に統一し、json_parseで一度だけchars()を実体化してpeekをO(1)化（len()の毎回呼び出しもO(n)走査のため長さをキャッシュ）。peekは非ASCIIコードポイントに対しJSON構造文字と衝突しない番兵0x80を返し、文字列内容はrun切り出しのsubstring（コードポイント添字）で取り出す。\uXXXX復号はバイトをcharとして連結する方式（js/tsでUTF-16文字扱いになり文字化け）をやめ、utiny[]を組み立ててfrom_bytesで復号する全バックエンド共通経路にした。これによりmatch_literalのsubstring（コードポイント添字）とも単位が一致。
- 仕様: CANONICAL_SPEC 10.4へ2系統の単位規約表を明文化。チュートリアル（length.md ja/en・advanced/strings.md ja/en）とアーキテクチャ文書（architecture/strings/utf8.md）を追従。

テスト: tests/common/strings/utf8_charat_byteat_test.cm（charAt/at/byte_at/走査の全経路一致）、tests/common/std/json_nonascii.cm（非ASCIIキー・値・絵文字・\uサロゲートペア・ラウンドトリップ）、libs/std/json/mod_test.cmへ非ASCIIケース追加。jit/native/wasm/js/tsの5経路で手動確認済み+全スイートPASS。

残課題: first()/last()は先頭/末尾バイト返しのバイト系のまま（コードポイント系はcodepoint_at(0)等で代替可能。architecture文書に使い分けを明記）。バイト添字の部分列API（byte_substring等）は未提供で、必要になったらbyte_接頭辞で追加する。
