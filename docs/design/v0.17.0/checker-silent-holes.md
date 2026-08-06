# R10: 型検査の黙殺穴（未定義型の変数宣言・型不一致マクロ・const generic半実装）

**ステータス:** 未修正（第6ラウンド検出）
**重大度:** Medium

過去ラウンドで確立した「型付きHIRは全式が非null・非error型」（typed-hir-single-source）の不変条件が、宣言・マクロ・const genericの各経路で破れている3件。いずれも「checkerを素通りして下流で顕在化」する同族で、method-resolution-unification / coercion-driver-unificationの延長で封じるべき。

## 症状（実測: cm 0.17.0、プローブ `.tmp/bughunt6/{syntax1,stdlib}/`）

### バグ1 未定義型の変数宣言が無診断で実行まで通る

```cm
int main() {
    TotallyBogusType<int, int> m;   // どこにも定義がない型
    println("compiled and ran");
    return 0;
}
```
実測: jit O0で`compiled and ran`（rc=0）。非ジェネリック`BogusPlain m;`も素通り。メソッドを呼んで初めて`Unknown method 'put' for type 'TotallyBogusType<int, int>'`という「型未定義」とは言わない誤誘導エラーになる。未実装アダプタ（`MapIterator<int,int> m;`）の参照もこの経路で黙殺される。変数宣言時に型名の存在検査がない。

### バグ2 型不一致マクロが全フロントエンド検査を素通り

```cm
macro int X = "str";
int main() { println(X); return 0; }
```
実測: `check`/`--strict`はerrors:0。jit/native/wasmは`LLVM module verification failed: Global variable initializer type does not match`（nativeはIR全文をstdoutダンプ）。js/tsは黙って`str`を出力。checkが通り経路で挙動が三分裂（check通過/LLVM内部エラー/js黙殺実行）。マクロ定義の宣言型と初期化子型の照合が欠落。

### バグ3 const genericパラメータが宣言のみ受理され実体化不能（半黙殺）

`struct Arr<SIZE: const int> { int[SIZE] data; }`の宣言はcheck無警告で受理されるが、`Arr<4> a;`（値引数での実体化）は`Expected type`構文エラー。`<N: const int> int get_n()`では`N`が値でなく型として扱われ`Return type mismatch: expected 'int', got 'N'`、`get_n<5>()`は`Empty parentheses without lambda body`という無関係な誤誘導になる。宣言が無警告で通るためユーザーは書けると誤認してから使用箇所で意味不明なエラーに遭遇する。既存テスト`const_generics.cm`は宣言パースのみで使用実体がなく見逃されていた。

## 修正方針

- バグ1: 変数宣言の型名を型検査時に解決し、未定義なら診断で停止（typed-hir不変条件の宣言サイトへの拡張）。
- バグ2: マクロ定義で宣言型と初期化子式の型を照合し、不一致を診断（数値変換分類classify_numeric_conversionの再利用）。
- バグ3: const genericの値引数実体化（`Arr<4>`の`4`を値パラメータとして束縛）を実装するか、未実装なら宣言時に「const generic未対応」の診断を出して半黙殺をやめる。

## テスト計画

エラーテスト（tests/common/errors/）: 未定義型変数宣言の拒否・型不一致マクロの拒否・const generic使用時の明示診断または実体化の値一致。
