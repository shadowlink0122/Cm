# SVバックエンド - モジュール階層の保持

既定では `import` は全シンボルをフラット化して単一モジュールに展開しますが、
`//! sv: hierarchy` ディレクティブを指定すると、相対importを**別モジュールの
インスタンス化**として階層のまま保持できます。

## 使い方

サブモジュール（`alu.cm`）:

```cm
//! platform: sv

#[input] uint a;
#[input] uint b;
#[input] utiny op;
#[output] uint result = 0;

void alu_comb() {
    if (op == 0) {
        result = a + b;
    } else if (op == 1) {
        result = a - b;
    } else {
        result = a ^ b;
    }
}
```

トップモジュール（`top.cm`）:

```cm
//! platform: sv
//! sv: hierarchy

import ./alu;   // フラット化されず別モジュールとして保持される

#[input] posedge clk;
#[input] uint x;
#[input] uint y;
#[output] uint sum = 0;

uint alu_out = 0;
utiny op_add = 0;

// 構造体リテラルでポートを接続（フィールド名 = サブモジュールのポート名）
alu alu0 = alu { a: x, b: y, op: op_add, result: alu_out };

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

## 仕組みと制約

- import先のポート宣言（`#[input]`/`#[output]`/`#[inout]`）から
  extern struct が自動生成され、import文を置換します
  （インスタンスの型名 = ファイル名のstem。`alu.cm` → `alu`）
- import先は個別にSVコンパイルされ、トップの `.sv` に連結されます。
  ネストした階層import・循環import検出に対応
- インスタンス出力に接続された信号（上記の `alu_out`）は宣言初期値が
  出力されません（インスタンスが駆動するため）
- 対象は単純な相対import（`import ./name;`）のみ。選択import
  （`::{...}`）やエイリアスは従来どおりフラット化されます

回帰テスト: `tests/sv/hierarchy/hier_top`
