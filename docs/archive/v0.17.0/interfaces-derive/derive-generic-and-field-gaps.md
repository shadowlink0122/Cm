# R21: derive/with自動実装のジェネリック型引数・フィールド型ギャップ（無言誤値・no-op・リンク失敗）

**ステータス:** 修正済み（バグ1〜4＋付随の診断位置まで全件処置。方針は特殊化時検証＋メソッド登録＋enumフィールドのint意味論対応）
**重大度:** Critical（ジェネリック×スライス型引数のEq）/ High（ユニオン型引数・非Eq/Ordトレイトのno-op）/ Medium（enumフィールド）

`with`/`#[derive(...)]`の自動実装は、非ジェネリック構造体では全トレイト（Eq/Ord/Clone/Hash/Debug/Display/Css）が健全に動作するが、ジェネリック型引数とフィールド型に穴が集中している。既知ギャップ文書は「モノモーフ化MIRパスにSlice/Union分岐がない」ことを記録済みで、本調査で具体的な破綻を実機実証し、新規詳細（バックエンド分裂・非Eq/Ordトレイトの無言no-op・enumフィールド）を追加した。

## 症状（実測: cm 0.17.0、プローブ `.tmp/bughunt8/{derive,verify}/`）

### バグ1【Critical】ジェネリック×スライス型引数のEqが無言誤値・バックエンド分裂

```cm
struct Box<T> with Eq { T v; }
int main() {
    Box<int[]> a = { v: [1,2,3] };
    Box<int[]> b = { v: [1,2,3] };   // 内容同一
    Box<int[]> c = { v: [1,2,9] };   // 内容相違
    println("eq_ab={a == b} eq_ac={a == c}");  // 期待: true false
    return 0;
}
```
実測（現行バイナリ）: jit-O0=`eq_ab=true eq_ac=true`（acが誤）・jit-O2=`eq_ab=false eq_ac=false`（両方誤）・native-O2=`false false`・js=`true false`（正）。**6経路で結論が3種に分裂**し、モノモーフ化Eqが要素内容でなく生バイナリ（スライスヘッダのポインタ等）を比較している。初期の調査（旧バイナリ）ではjit-O2でSIGSEGVも観測したが、調査中のリビルドでクラッシュは解消し誤値のみ残存（誤値と分裂は現行でも再現）。直接`T[] items`フィールドはcheckerが正しく拒否するため、本ギャップは**型引数経由でのみ発火**する。

### バグ2【High】ジェネリック×ユニオン型引数のEqがリンク失敗

`Box<IU>`（`typedef IU = int | string`）で`with Eq`すると、jit/nativeとも`Undefined symbols: "_IntOrStr__op_eq"`でハードリンク失敗（難読メッセージ・診断なし）。ユニオンの等価演算子が未生成のままモノモーフ化Eqが参照する。付随: `Box<int | string>`とインラインunionを型引数に書くとパーサが`Expected '>'`（ユニオン型引数の構文非対応）。

### バグ3【High】ジェネリックderiveはEq/Ordのみ実装、Clone/Hash/Debug/Displayは無言no-op

`struct G<T> with Clone { T v; }`（Hash/Debug/Displayも同様）は**宣言時に無診断で受理されるが、メソッドが一切生成されない**。`x.clone()`/`x.hash()`/`x.debug()`/`x.toString()`は全て`error: Unknown method '...' for type 'G<int>'`。非ジェネリック構造体では全トレイトが動くため、ジェネリック構造体で非Eq/Ordトレイトを`with`すると「書けるが呼べない」罠になる。宣言を拒否するか生成するかのいずれかであるべき。

### バグ4【Medium】enumフィールド＋Debug/Hashが型検査失敗

deriveした構造体がenum（ペイロード付き）フィールドを持つと、Debug/Hash derive時に`error: Unknown method 'debug'/'hash' for type 'int'`（生成コードがenumをintとみなして`.debug()`/`.hash()`を呼ぶ）。enumフィールド＋Eqは正常。

### 付随【Low】derive検証診断のソース位置が全件誤り

「Cannot derive」「Unknown interface」「Duplicate impl」等すべての位置表示が該当構造体でなくstdlib（`libs/std/io/console/input.cm`等）を指す。エラー内容は正しいが位置が無関係（R14の診断品質と同根）。

### 健全だった点

非ジェネリック構造体の全トレイト（with/#[derive]両形式・6経路一致）、固定長配列/ネスト構造体/ポインタ/**非ジェネリックのユニオンフィールド**（旧MIR手組みパスがunion ==を正しく処理）、Ord全順序性・C3回帰（string内容比較）、未定義/重複deriveの拒否、Css。

## 修正方針

既知ギャップ文書の「with/derive自動実装のソース展開」を完遂する方向。モノモーフ化されたderive本体をMIR直生成でなくCmソース合成＋通常パイプライン（derive-as-source-expansion）へ寄せれば、Slice/Union/enumフィールドやジェネリック型引数が通常の型解決・演算子解決を経るため、バグ1〜4が構造的に解消する。当面の暫定対応として、(3)ジェネリック×非Eq/Ordトレイトの`with`は宣言時に「未対応」診断で停止し無言no-opをやめる。(2)ユニオン型引数は等価演算子生成をトリガーするか診断する。

## テスト計画

`tests/common/interface/`へ: ジェネリック構造体×（プリミティブ/string/スライス/ユニオン/enum/ネスト）フィールド型引数の全トレイト（Eq/Ord/Clone/Hash/Debug/Display）が全バックエンドで値一致するマトリクス回帰。未対応の組み合わせは診断で停止する負のテスト。derive診断のソース位置検証。

## 実装記録

構造的解消（derive-as-source-expansionのジェネリック拡張）は[auto-impl-generic-gaps-and-cleanup.md](auto-impl-generic-gaps-and-cleanup.md)第3段の領分として残し、本修正は「無言の誤動作・no-opを全て診断または動作に置き換える」方針で4バグ＋付随を処置した。

- **バグ1・2（スライス/ユニオン型引数の無言誤値・リンク失敗）→特殊化時検証で診断化**: checkerへ`validate_derive_instantiation`を新設し、derive付きジェネリック構造体の特殊化（`Box<int[]>`等）で型引数を代入した後のフィールド型を宣言時と同じ規則（`derive_field_unsupported_reason`として宣言時検証`validate_derive_field_types`と共通化）で検証する。検証フックはlet宣言のH15検査（stmt.cpp）とis_valid_type（compat.cpp: グローバル/フィールド/パラメータ/戻り値等）の2箇所で、typedef経由の型引数はresolve_typedefで実体化してから判定、重複診断は特殊化名のメモで抑止。`Box<int[]>`のEq（6経路で結論3分裂の生バイナリ比較）と`Box<IU>`のEq（`_IntOrStr__op_eq`未定義の難読リンク失敗）が使用箇所を指す「Cannot derive」診断になる。
- **バグ3（ジェネリック×Clone/Hash/Debug/Displayの無言no-op）→メソッド解決を配線して動作化**: MIR側の`generate_*_for_monomorphized`は全トレイト生成済みだったが、checker側の`register_auto_{clone,hash,debug,display}_impl`がメソッドを基底名（`G`）でしか登録せず、特殊化レシーバの検索キー（`G<T>`）から到達不能だった。ジェネリック構造体は`G<T>`キー（implブロック登録と同形）でも登録し、cloneの戻り値型は型引数付き（`G<T>`）にして呼び出し時に`G<int>`へ置換されるようにした。jit/native/wasm/js/tsの全経路で`clone()`/`hash()`/`debug()`/`toString()`が動作。あわせてモノモーフ化Debug/Displayの表示名をマングル名（`G__int`）から基底名（`G`）へ修正。
- **バグ4（enumフィールド＋Debug/Hashの型検査失敗）→値enumはint意味論で動作化・タグ付きは診断**: derive展開（macro/derive.cpp）へプログラム中のenum宣言の収集（`EnumTaggedMap`）を追加し、値enumフィールドはHashで`(self.f as int)`混合・Debug/Displayで数値整形（`c: 5`）に振り分けた（従来はStruct扱いで`.debug()`/`.hash()`を呼びenumのint正規化後にUnknown methodだった）。ペイロード付きenumフィールドは展開対象外にしてchecker検証（`tagged enum fields are not supported`）で明示拒否する（checkerの共通判定にもenum_defs_参照のタグ付きenum規則を追加）。
- **付随（derive診断の位置が全件stdlibを指す）**: 宣言登録パスの`current_span_`（直前に処理した無関係の宣言）でなく構造体名の`name_span`を診断位置に使うよう修正。
- **テスト**: `tests/common/interface/`へジェネリック×（int/string/ネスト構造体）×全トレイトの実行回帰と値enumフィールドのEq/Hash/Debug/Display回帰、`tests/common/errors/`へスライス型引数・ユニオン型引数・タグ付きenumフィールドの診断3本。unit/regression/interpreter/llvm/wasm/js/ts/sv/libsの全スイートPASS。
- **チュートリアル**: with-keyword.md（ja/en）の対応範囲表へ値enum行とペイロード付きenumの区別を追加し、特殊化時検証とジェネリックメソッド対応を明文化。
- **残課題（本修正の範囲外）**: スライス/ユニオン型引数のderiveを「動作」させる構造的解消は単一ソース化（同文書第3段）の領分。値enumのDebug表示を変種名（`Green`）にする改善は将来検討。debug()結果を同一補間内の後続プレースホルダと混在させると出力が崩れるのはR24（補間の実行時値再スキャン）の既知バグで別件。
