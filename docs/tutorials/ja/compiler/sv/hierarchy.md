# SVバックエンド - モジュール階層の保持

既定では `import` は全シンボルをフラット化して単一モジュールに展開しますが、import先が**exportされたIO構造体**（方向属性フィールドを持つ `export struct`）を宣言している場合、その相対importは**別モジュールのインスタンス化**として階層のまま保持されます。

## 使い方

サブモジュール（`alu.cm`）は、公開インターフェース契約となるIO構造体を `export` して宣言します:

```cm
//! platform: sv

export struct AluIo {
    #[input] uint a;
    #[input] uint b;
    #[input] utiny op;
    #[output] uint result = 0;
};

AluIo io;

void alu_comb() {
    if (io.op == 0) {
        io.result = io.a + io.b;
    } else if (io.op == 1) {
        io.result = io.a - io.b;
    } else {
        io.result = io.a ^ io.b;
    }
}
```

トップモジュール（`top.cm`）は、`<モジュール名>::<IO構造体名>` の修飾名でインスタンス化します:

```cm
//! platform: sv

import ./alu;   // export struct宣言があるため、フラット化されず別モジュールとして保持される

#[input] posedge clk;
#[input] uint x;
#[input] uint y;
#[output] uint sum = 0;

uint alu_out = 0;
utiny op_add = 0;

// 構造体リテラルでポートを接続（フィールド名 = サブモジュールのポート名）
alu::AluIo alu0 = alu::AluIo { a: x, b: y, op: op_add, result: alu_out };

async void update(posedge clk) {
    sum = alu_out;
}
```

生成SV（1ファイルに両モジュールが連結される）:

```systemverilog
module top ( ... );
    alu alu0 (
        .a(x),
        .b(y),
        .op(op_add),
        .result(alu_out)
    );
    ...
endmodule

module alu ( ... );
    ...
endmodule
```

型が通常のモジュールシステムで解決されるため、`cm check` / `cm lint` も特別扱いなしで通ります。

> v0.16.2で `//! sv: hierarchy` ディレクティブは廃止されました。記述されていても単なるコメントとして無視され、exportされたIO構造体を持たない相対importは従来どおりフラット化されます。

## IOの明示的構造体宣言（IOインスタンス）

モジュールのIOはC/C++スタイルの構造体宣言とそのインスタンスで定義します。

```cm
// alu.cm
export struct AluIo {
    #[input] uint a;
    #[input] uint b;
    #[output] uint result = 0;
};

AluIo io;

void alu_comb() {
    io.result = io.a + io.b;
}
```

- `#[input]`/`#[output]` フィールドを持つ構造体のグローバル変数（IOインスタンス）は、フィールドがそのままモジュールポートへ展開されます（ポート名 = フィールド名。個別のポート宣言は不要）
- モジュール内のアクセスは `io.field` で行い、SV出力ではポート名へフラット化されます
- `#[output]` フィールドの既定値（`= 0`）はポートの電源投入時初期値になります
- 構造体宣言の末尾セミコロン（`};`）を許容します（C/C++互換）
- クロック等の直接ポート宣言（`#[input] posedge clk;`）とは併用できます。階層化の対象にするモジュールでは、クロックも `#[input] bool clk;` としてIO構造体のフィールドで宣言します（`async void f(posedge clk)` のトリガは従来どおり機能します）
- IO構造体はデータ型（`typedef struct packed`）としては出力されません
- `#[test]` 関数内でも `io.field` で参照・駆動でき、テストベンチではポート名へフラット化されます
- 親側のインスタンス接続でも `io.field` を値として指定できます（`alu::AluIo { a: io.x, ... }` → `.a(x)`）
- フィールドに付与した `#[sv::pin]` 属性はピン制約（.cst/.xdc）へ反映されます

## 仕組みと制約

- import先のexportされたIO構造体から extern struct が生成され、import文を置換します（インスタンスの型名 = ファイル名のstem。`alu.cm` → `alu`。親ソース中の `alu::AluIo` は `alu` へ置換されます）
- import先は個別にSVコンパイルされ、トップの `.sv` に連結されます。ネストした階層import・循環import検出に対応
- インスタンス出力に接続された信号（上記の `alu_out`）は宣言初期値が出力されません（インスタンスが駆動するため）
- 対象は単純な相対import（`import ./name;`）のみ。選択import（`::{...}`）やエイリアスは従来どおりフラット化されます

回帰テスト: `tests/sv/hierarchy/hier_top`

## モジュールパラメータ（#[sv::parameter]・#[sv::param]）

サブモジュール側で `#[sv::parameter]` を付けた const は `module #(parameter ...)` として出力され、ポート・内部信号の幅も記号のまま（`[WIDTH-1:0]`）保たれます。インターフェース契約としては、IO構造体に `#[sv::param]` フィールドを宣言します（インスタンス側からの上書きを型検査可能にするため）:

```cm
// shifter.cm
#[sv::parameter] const uint WIDTH = 8;

export struct ShifterIo {
    #[sv::param] uint WIDTH = 8;
    #[input] bool clk;
    #[input] bit[WIDTH] din;
    #[output] bit[WIDTH] dout = 0;
};

ShifterIo io;

async void shift(posedge clk) {
    io.dout = io.din;
}
```

```systemverilog
module shifter #(
    parameter WIDTH = 8
) (
    input  logic clk,
    input  logic [WIDTH-1:0] din,
    output logic [WIDTH-1:0] dout
);
```

階層インスタンス化では、structリテラルのフィールドとしてパラメータを上書きできます（省略時はデフォルト値）:

```cm
import ./shifter;

shifter::ShifterIo sh0 = shifter::ShifterIo { WIDTH: 16, clk: clk, din: data_in, dout: wide_out };
// → shifter #(.WIDTH(16)) sh0 (.clk(clk), ...);
```

パラメータ境界のループ（`for (uint i = 0; i < N; i = i + 1)`でNが`#[sv::parameter]`）は、合成ツールが受理するSVのfor文として出力されます（v0.17.0。whileは合成不能のためfor形へ再構成されます）:

```cm
#[sv::parameter] const uint N = 4;

async void update(posedge clk) {
    uint acc = 0;
    for (uint i = 0; i < N; i = i + 1) {
        acc = acc ^ (din >> i);
    }
    out = acc;
}
// → for (i = 32'sd0; i < N; i = i + 32'sd1) begin ... end
```

パラメータ幅のメモリ配列（`bit[WIDTH][DEPTH]`）は`logic [WIDTH-1:0] mem [0:DEPTH-1];`として出力され、添字で読み書きできます（v0.17.0）:

```cm
#[sv::parameter] const uint WIDTH = 8;
#[sv::parameter] const uint DEPTH = 4;

bit[WIDTH][DEPTH] mem;

async void update(posedge clk) {
    mem[waddr] = din;
    dout = mem[0];
}
```


---

<!-- nav -->
← 前: [SVバックエンド - メモリ初期化（ROM/RAM）](memory.html) ｜ [目次](index.html) ｜ 次: [SVバックエンド - 実機I/O（ピン制約・トライステート・CDC）](board-io.html) →

## モジュールインスタンス配列（#[sv::instance_array]・v0.17.0）

同一サブモジュールをN個並べる（PE配列・並列レーン・多チャンネル）には、インスタンス宣言へ`#[sv::instance_array(N)]`を付けます。generate-forとして出力され、結線先が配列信号なら各レーンへ分配、スカラ信号なら全レーンへブロードキャストされます:

```cm
import ./pe_xor;

uint[2] pa;
uint[2] pb;
uint[2] pr;

#[sv::instance_array(2)]
pe_xor::PeXorIo lanes = pe_xor::PeXorIo { a: pa, b: pb, r: pr };
```

```systemverilog
genvar __gi_lanes;
generate
    for (__gi_lanes = 0; __gi_lanes < 2; __gi_lanes = __gi_lanes + 1) begin : lanes_gen
        pe_xor lanes (
            .a(pa[__gi_lanes]),
            .b(pb[__gi_lanes]),
            .r(pr[__gi_lanes])
        );
    end
endgenerate
```

Nには`#[sv::parameter]`のパラメータ名も使えます。
