# キャスト`as`と型判別`is`のLLVM IR対訳

`as` キャストは `src/internal/codegen/llvm/core/rvalue.cpp` のCast右辺値処理が唯一の生成箇所で、変換の組み合わせに応じて `trunc/sext/zext/sitofp/uitofp/fpext/fptrunc/ptrtoint/inttoptr` と飽和intrinsic `llvm.fptosi.sat`/`llvm.fptoui.sat` へ変換される。
ユニオン型に対する `is` はタグフィールドのロードと `icmp eq` によるタグ比較であり、実行時型情報のテーブル参照などは行わない。
本書の抜粋は `CM_DUMP_IR=2 ./cm compile -O3` の実IR（O0比較は `CM_DUMP_IR=1 -O0`）で、`args()` 由来の実行時値と `println` アンカーで畳み込みを避けている。

### 整数の拡大（sext/zext）

```cm
int a = args().length() as int;
long widen = a as long;         // 符号付き: sext
uint ua = a as uint;
ulong uwiden = ua as ulong;     // 符号なし: zext
```

```llvm
; -O0では素直に出る
%sext = sext i32 %load to i64
%zext = zext i32 %load to i64

; -O3ではLLVMが同値の別形へ書き換えることがある
%sext29 = shl i64 %3, 32
%sext = ashr exact i64 %sext29, 32          ; sextの正規化形
%zext = and i64 %3, 4294967295              ; zextのマスク形
```

ソースが符号付きなら `sext`、符号なし（`bool`/`char` 含む）なら `zext` が選ばれ、`utiny 255 as int` が255になるC言語と同じ規則になる。
O3のIRでは `sext` が `shl`+`ashr exact` に、`zext` が `and` マスクに正規化されて見えることがあるが意味は同じである。
選択規則の実装は[数値出力とキャストの一貫性](../../codegen-native/numeric-and-casts.md)と[型キャストの設計](../../types/casts.md)を参照。

### 整数の縮小（trunc）

```cm
tiny narrow = a as tiny;
```

```llvm
%sext30 = shl i32 %trunc, 24
%sext8 = ashr exact i32 %sext30, 24     ; trunc i32→i8 の後にprintln用へsextした正規化形
```

整数の縮小は `trunc` による下位ビット切り捨て（ラップ）で、範囲検査は行われない。
O0では `trunc i32 %v to i8` がそのまま現れる。

### 整数と浮動小数の相互変換（sitofp/uitofp/fptosi.sat）

```cm
double d = a as double;         // 符号付き→浮動小数
int ti = d as int;              // 浮動小数→符号付き
uint tu = d as uint;            // 浮動小数→符号なし
```

```llvm
%sitofp = sitofp i32 %trunc to double
%fptoint_sat = tail call i32 @llvm.fptosi.sat.i32.f64(double %sitofp)
%fptoint_sat23 = tail call i32 @llvm.fptoui.sat.i32.f64(double %sitofp)
```

整数→浮動小数は符号に応じて `sitofp`/`uitofp` になり、`uint 4000000000 as double` が負値化しない。
浮動小数→整数は生の `fptosi`（範囲外がpoison）ではなく飽和intrinsicへ統一され、範囲外は型の最大・最小へクランプ、`nan` は0になって全バックエンドで同じ値を返す。
この意味論の背景は[数値出力とキャストの一貫性](../../codegen-native/numeric-and-casts.md)に詳しい。

### 浮動小数どうし（fpext/fptrunc）

```cm
double x = (a as double) * 0.5;
float f = x as float;           // fptrunc
double back = f as double;      // fpext
```

```llvm
%fptrunc = fptrunc double %fmul to float
%fpext_arg = fpext float %fptrunc to double
```

`double as float` はIEEEの丸めを伴う `fptrunc`、`float as double` は正確な拡張の `fpext` になる。
`println` は `float` を `double` へ拡張して受け取るため、`float` 値の出力前には常に `fpext` が入る。

### ポインタと整数（ptrtoint/inttoptr）

```cm
int* p = &a;
ulong addr = p as ulong;        // ptrtoint
int* q = addr as int*;          // inttoptr
println(*q);
```

```llvm
%ptrtoint = ptrtoint ptr %local_1 to i64
%inttoptr = inttoptr i64 %load12 to ptr     ; -O0の形
%field_load = load i32, ptr %local_1        ; -O3: qがpと同一と判明しinttoptrは消える
```

ポインタ→整数は `ptrtoint`、整数→ポインタは `inttoptr` で、アドレス値の再解釈以外の変換は行われない。
O3では往復キャストが元のポインタへ畳み込まれ、`inttoptr` が消えて直接の `load` になることがある。

### ユニオンの型判別 `is`（タグ比較）

```cm
typedef Value = int | string;
Value v = a;
println(v is int);
```

```llvm
; -O0（CM_DUMP_IR=1）: ユニオンは{タグ, ペイロード}の構造体
%is_tag_ptr = getelementptr inbounds %__anon_union_8, ptr %local_11, i32 0, i32 0
%is_tag = load i32, ptr %is_tag_ptr, align 4
%union_is = icmp eq i32 %is_tag, 0          ; intの変種タグ=0との比較
```

ユニオン値は先頭にタグ（`i32`）を持つ構造体で表現され、`is` はタグのロードと変種番号の `icmp eq` に落ちる。
代入時には対応するタグ定数が書き込まれる（`store i32 0, ptr %tag_ptr` 等）ため、O3で代入からの経路が追跡できる場合はタグ比較ごと定数化・分岐消去される。
タグ割り当てとペイロード表現は[ユニオン型の設計](../../types/union-types.md)を、`as` によるタグ検査付き取り出しは[型キャストの設計](../../types/casts.md)を参照。

## 関連資料

- [算術演算子のLLVM IR対訳](arithmetic.md)
- [ビット演算子のLLVM IR対訳](bitwise.md)
- [型キャストの設計](../../types/casts.md)
- [ユニオン型の設計](../../types/union-types.md)
- [数値出力とキャストの一貫性](../../codegen-native/numeric-and-casts.md)
