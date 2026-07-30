# グローバル宣言のLLVM IR対訳

トップレベルの変数・定数宣言はモジュールスコープのLLVMグローバル（`@名前 = linkage global 型 初期値`）として発行され、linkageは `export` の有無で決まる（非exportは `internal`、exportは外部リンケージ）。
初期値としてIRのinitializerに直接埋め込まれるのはリテラルの場合だけで、集約初期化子や関数呼び出しを含む初期化子は `zeroinitializer` のグローバル + `main` 先頭でのランタイムstore列に分解される。
参照サイトは常に `load`/`store` + `@名前`（集約は `getelementptr` 経由）であり、O3では書き換えられないグローバルの読み出しが即値へ畳み込まれてグローバル定義自体が消えることも多い。

### グローバル変数（スカラ）

```cm
int counter = 0;

int bump() {
    counter++;
    return counter;
}
```

O3後のIR（抜粋）:

```llvm
@counter = internal unnamed_addr global i32 0

define i32 @bump() local_unnamed_addr {
entry:
  %static_load = load i32, ptr @counter, align 4
  %add = add i32 %static_load, 1
  store i32 %add, ptr @counter, align 4
  ret i32 %add
}
```

リテラル初期化のスカラグローバルは初期値がそのままinitializerに載り、参照サイトは `load`/`store` + シンボル直接参照になる。
非exportのグローバルは `internal` linkageのためモジュール外から見えず、O3は使用実態に応じて `unnamed_addr` などの属性を追加する。

### exportグローバルとlinkage

```cm
export int shared_counter = 7;
```

O3後のIR:

```llvm
@shared_counter = local_unnamed_addr global i32 7
```

`export` を付けるとlinkage指定なし（外部リンケージ）のグローバルになり、他モジュールから `declare`/参照できるシンボルとしてオブジェクトファイルに公開される。
モジュール間の可視性設計は[可視性と重複排除](../../modules/visibility-and-dedup.md)を参照。

### グローバル定数

```cm
const int LIMIT = 42;

int main() {
    println(LIMIT);
    return 0;
}
```

最適化前IR（抜粋）:

```llvm
@LIMIT = internal constant i32 42
...
  %static_load13 = load i32, ptr @LIMIT, align 4
```

O3後のIR（抜粋）:

```llvm
  tail call void @cm_println_int(i32 42)
```

`const` 付きグローバルはIR上も `constant`（書き込み不可・rodata配置）として発行され、参照は通常のloadになる。
O3では `constant` の初期値が全loadへ定数伝播し、参照が全てモジュール内で完結していればグローバル定義自体が削除されて即値だけが残る。

### 非定数初期化子を持つグローバル

```cm
int base() { return 100; }
int origin = base() + 5;
```

最適化前IR（抜粋）:

```llvm
@origin = internal global i32 0
...
bb0:
  %2 = call i32 @base()
  ...
  store i32 %load1, ptr @origin, align 4
```

初期化子に関数呼び出しなど実行時評価が必要な式を含む場合、グローバル自体は `zeroinitializer` 相当で定義され、初期化コードは `main` の先頭ブロック（ユーザーコードより前）へ挿入される。
C++の静的コンストラクタ（`@llvm.global_ctors`）ではなく `main` 直挿しのため、初期化順はソース上の宣言順に一致する。
この例をO3でコンパイルすると `base()` のインライン展開と定数伝播で初期化コードごと畳み込まれ、`origin` の参照が `main` に閉じていればグローバル定義も消えて即値105だけが残る。

### グローバル集約（構造体・固定長配列）

```cm
int[4] table = [10, 20, 30, 40];

int pick(int i) {
    return table[i & 3];
}
```

O3後のIR（抜粋）:

```llvm
@table = internal unnamed_addr global [4 x i32] zeroinitializer

define i32 @main(...) {
entry:
  store i32 10, ptr @table, align 4
  store i32 20, ptr getelementptr inbounds ([4 x i32], ptr @table, i64 0, i64 1), align 4
  store i32 30, ptr getelementptr inbounds ([4 x i32], ptr @table, i64 0, i64 2), align 4
  store i32 40, ptr getelementptr inbounds ([4 x i32], ptr @table, i64 0, i64 3), align 4
  ...
}

define i32 @pick(i32 %arg0) local_unnamed_addr {
entry:
  %bitand = and i32 %arg0, 3
  %idx_ext = zext i32 %bitand to i64
  %flat_elem_ptr = getelementptr i32, ptr @table, i64 %idx_ext
  %field_load = load i32, ptr %flat_elem_ptr, align 4
  ret i32 %field_load
}
```

配列リテラル・構造体リテラルによるグローバル初期化は、initializerへの直接埋め込みではなく `zeroinitializer` グローバル + `main` 先頭での要素/フィールド単位storeとして展開される（構造体は `store i32 640, ptr @config` と `getelementptr (%Config, ptr @config, i32 0, i32 1)` のようなフィールドオフセットstoreになる）。
`main` 以外の関数から参照される場合はこのstore列とグローバル定義がO3でも残り、参照サイトは `getelementptr` + `load` になる。
逆に `main` 内でしか使われないグローバル集約は、O3の定数伝播とグローバル削除で定義・初期化storeごと消えて即値になる。
要素アクセスのIRパターンは[スライスと配列のコード生成](../../codegen-native/slice-and-array-codegen.md)を参照。

## 関連資料

- [変数宣言のIR対訳](var-decl.md)
- [typedefとマクロのIR対訳](typedef-macro.md)
- [MIRからLLVM IRへの変換](../../codegen-native/mir-to-llvm.md)
- [LLVM最適化パイプラインとCM_DUMP_IR](../../codegen-native/llvm-optimization.md)
- [可視性と重複排除](../../modules/visibility-and-dedup.md)
