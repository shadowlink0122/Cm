# R21: derive/with自動実装のジェネリック型引数・フィールド型ギャップ（無言誤値・no-op・リンク失敗）

**ステータス:** 未修正（第8ラウンド検出。既知ギャップ [auto-impl-generic-gaps-and-cleanup.md](auto-impl-generic-gaps-and-cleanup.md) の実証＋新規詳細）
**重大度:** Critical（ジェネリック×スライス型引数のEq）/ High（ユニオン型引数・非Eq/Ordトレイトのno-op）/ Medium（enumフィールド）

`with`/`#[derive(...)]`の自動実装は、非ジェネリック構造体では全トレイト（Eq/Ord/Clone/Hash/Debug/Display/Css）が健全に動作するが、ジェネリック型引数とフィールド型に穴が集中している。既知ギャップ文書は「モノモーフ化MIRパスにSlice/Union分岐がない」ことを記録済みで、本ラウンドで具体的な破綻を実機実証し、新規詳細（バックエンド分裂・非Eq/Ordトレイトの無言no-op・enumフィールド）を追加した。

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
