# typedefとマクロのLLVM IR対訳

`typedef` と `macro` はどちらもLLVM IRに固有の表現を一切残さない宣言であり、typedefは型検査時に基底型へ解決され、マクロはパース時（関数マクロ）またはHIR lowering時（定数マクロ）に展開が完結する。
そのためIRを読むときの対訳は「typedef名は基底型のLLVM型として現れる」「定数マクロは使用箇所に即値リテラルとして現れる」「関数マクロは通常の関数シンボルとして現れる」の3行に尽きる。
本文書ではそれぞれを実際の生成IR（arm64-apple-darwin、`CM_DUMP_IR=2 ./cm compile -O3`）で確認する。

### typedef（スカラ型の別名）

```cm
typedef MemAddr = ulong;
typedef Kilometers = int;

MemAddr shift(MemAddr base, Kilometers d) {
    return base + (d as MemAddr);
}
```

O3後のIR:

```llvm
define i64 @shift(i64 %arg0, i32 %arg1) local_unnamed_addr {
entry:
  %sext = sext i32 %arg1 to i64
  %add = add i64 %sext, %arg0
  ret i64 %add
}
```

`MemAddr` は関数シグネチャ上で基底型 `ulong` のLLVM型 `i64` として現れ、typedef名に対応するIR上の型エイリアスやメタデータは生成されない。
つまりtypedefは型検査時に完全に解決される純粋なフロントエンド構文であり、`MemAddr` と `ulong` を混在させてもIRは同一になる。
`as` キャストの拡張規則（ここでは `sext`）は[数値演算とキャストの一貫性](../../codegen-native/numeric-and-casts.md)を参照。

### typedef（構造体型の別名）

```cm
struct Vec2 {
    int x;
    int y;
};

typedef Position = Vec2;

int norm1(Position p) {
    return p.x + p.y;
}
```

O3後のIR:

```llvm
%Vec2 = type { i32, i32 }

define i32 @norm1(%Vec2 %arg0) local_unnamed_addr {
entry:
  %arg0.fca.0.extract = extractvalue %Vec2 %arg0, 0
  %arg0.fca.1.extract = extractvalue %Vec2 %arg0, 1
  %add = add i32 %arg0.fca.0.extract, %arg0.fca.1.extract
  ret i32 %add
}
```

構造体の別名でも同様で、IRに現れる名前付き型は元の `%Vec2` だけであり、`Position` というシンボルはモジュールのどこにも残らない。
なお別名を構造体リテラルの型として使う書き方（`Position p = {x: ..., y: ...};`）は現状の型検査では受理されないため、リテラル側は元の構造体名で書く。

### 定数マクロ

```cm
macro int VERSION = 13;
macro string APP_NAME = "CmApp";

int show(int n) {
    println(VERSION + n);
    println(APP_NAME);
    return n;
}
```

O3後のIR（抜粋）:

```llvm
@strh = private unnamed_addr constant <{ i32, i32, i32, i32, [6 x i8] }> <{ i32 1129141041, i32 5, i32 1395741251, i32 0, [6 x i8] c"CmApp\00" }>, align 16

define i32 @show(i32 returned %arg0) local_unnamed_addr {
entry:
  %add = add i32 %arg0, 13
  tail call void @cm_println_int(i32 %add)
  tail call void @cm_println_string(ptr nonnull getelementptr inbounds (<{ i32, i32, i32, i32, [6 x i8] }>, ptr @strh, i64 0, i32 4, i64 0))
  ret i32 %arg0
}
```

`macro TYPE NAME = リテラル;` はHIR loweringで使用箇所の識別子がリテラルへインライン置換されるため、IRには `VERSION` に対応するグローバルもシンボルも存在せず、即値 `13` が演算に直接埋め込まれる。
string型マクロも同様に使用箇所が文字列リテラルへ置換され、通常の文字列リテラルと同じヘッダ付き定数（`@strh`）として発行される（レイアウトは[文字列の内部表現](../../strings/representation.md)を参照）。
この構文はO3の効果ではなくHIR段階の展開なので、`-O0` でも同じく即値だけが現れる。
展開タイミングと内部表現の詳細は[マクロシステムと展開](../../macro/expansion.md)を参照。

### 関数マクロ

```cm
macro int VERSION = 13;
macro int*(int, int) add = (int a, int b) => a + b;

int combine(int n) {
    return add(n, VERSION);
}
```

O3後のIR（抜粋）:

```llvm
define i32 @add(i32 %arg0, i32 %arg1) local_unnamed_addr {
entry:
  %add = add i32 %arg1, %arg0
  ret i32 %add
}

define i32 @combine(i32 %arg0) local_unnamed_addr {
entry:
  %add.i = add i32 %arg0, 13
  ret i32 %add.i
}
```

値がラムダ式のマクロはパース時点で通常の関数宣言へ変換されるため、IRにはマクロ由来であることを示す痕跡のない実関数 `@add` が定義され、呼び出しサイトも通常のcall（O3ではインライン展開の対象）になる。
上の例では `add(n, VERSION)` がインライン展開と定数マクロ置換の合成で `add i32 %arg0, 13` の1命令に畳み込まれており、関数マクロと手書き関数のコスト・最適化挙動は完全に等価である。
テキスト置換ではなく型検査を通る宣言である点も含め、詳細は[マクロシステムと展開](../../macro/expansion.md)を参照。

## 関連資料

- [マクロシステムと展開](../../macro/expansion.md)
- [グローバル宣言のIR対訳](global-decl.md)
- [変数宣言のIR対訳](var-decl.md)
- [型推論の設計](../../types/inference.md)
- [文字列の内部表現](../../strings/representation.md)
