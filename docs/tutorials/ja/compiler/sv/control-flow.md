---
title: SVバックエンド - 制御構文とループ
parent: Tutorials
nav_order: 14
---

[English](../../../en/compiler/sv/control-flow.html)

# SVバックエンド - 制御構文とループ

[SystemVerilogバックエンド](index.html) の詳細ページです。分岐・ループの変換規則と演算子の意味論を解説します。

---

## if / else if / else

```cm
if (rst) {
    counter = 0;
} else if (enable) {
    counter = counter + 1;
} else {
    // idle
}
```
```systemverilog
if (rst) begin
    counter <= 32'd0;
end else if (enable) begin
    counter <= counter + 32'd1;
end else begin
end
```

## switch → case

```cm
switch (state) {
    case(0) { next_state = 1; }
    case(1) { next_state = 2; }
    else { next_state = 0; }
}
```
```systemverilog
case (state)
    32'd0: begin next_state <= 32'd1; end
    32'd1: begin next_state <= 32'd2; end
    default: begin next_state <= 32'd0; end
endcase
```

> **注意:** Cmの switch 構文は `case(パターン) { ... }` 形式です。
> デフォルトは `else { ... }` で記述します。

---

## ループ（whileループ再構成）

プロセス内の `for` / `while` ループは、SVの手続き的 `while` ループとして再構成されます（v0.15.1 2026-07-04更新）:

```cm
async void accumulate(posedge clk) {
    uint total = 0;
    for (uint i = 0; i < 4; i = i + 1) {
        total = total + i;
    }
    sum = total;   // ループの後に実行される
}
```

```systemverilog
always @(posedge clk) begin
    total = 32'sd0;
    i = 32'sd0;
    _t1002 = i;
    _t1003 = 32'sd4;
    _t1004 = _t1002 < _t1003;
    while (_t1004) begin
        total = total + i;
        i = i + 32'sd1;
        _t1002 = i;              // 条件はループ末尾で再計算される
        _t1003 = 32'sd4;
        _t1004 = _t1002 < _t1003;
    end
    sum <= total;                // ループ後のコードも正しい位置に出力
end
```

内部動作:
1. MIRのCFGから**支配関係に基づいて自然ループ**（バックエッジ）を検出
2. ループ本体を `while (cond) begin ... end` として出力
3. ヘッダブロックの条件計算文を本体末尾で再実行（条件の一時変数は2回代入されるためインライン展開の対象外となり、レジスタとして残る）
4. ループ後のコード（exitブロック）はループの後に出力

> **旧バージョンの注意:** 以前はバックエッジが消えて「本体最大1回・ループ後コード到達不能」の誤ったSVが生成されていました。回帰テスト `tests/sv/control/for_loop`（シミュレーションで sum=6 を検証）で保証されています。

### break（disable方式）

ループからの脱出は、名前付きブロックへの `disable`（Verilog-1995互換）として
出力されます。SV-2005の `break` キーワードは古いIcarus Verilog（v11以前）や
一部の合成ツールが未対応のため使用しません:

```cm
while (true) {
    if (c >= limit) {
        break;
    }
    c = c + 1;
}
```

```systemverilog
begin : __loop0
    while (1'b1) begin
        if (c >= limit) begin
            disable __loop0;   // break相当: 名前付きブロックを脱出
        end else begin
            c = c + 32'sd1;
        end
    end
end
```

### 定数ループの静的展開（generate相当）

初期値・境界・増分がすべて定数のループは、SVターゲットのコンパイル時に
直列のブロック列へ**静的展開**され、生成SVに `while` が残りません
（合成ツールは動的whileを展開できないため）:

```cm
for (uint i = 0; i < 4; i = i + 1) {
    acc = acc ^ (din >> i);
}
```

```systemverilog
// ループ判定なしの直列コードに展開される
acc = acc ^ din;
acc = acc ^ (din >> 1);  // i=1相当（実際はテンポラリ経由）
// ... 4回分
```

- 展開上限: 1024イテレーション / 展開後50,000文
- `while (true) + break` や動的境界のループは従来どおり while + disable で出力
- 回帰テスト: `tests/sv/control/const_loop_unroll`

### ネストしたループ

ネストしたループも正しく再構成されます（内側ループの判定は自然ループの帰属で行われるため、外側ループのバックエッジと混同しません）。回帰テスト: `tests/sv/control/nested_loop`。

### 合成に関する注意

- ループ回数が**静的に決まる**場合、合成ツール（Gowin/Vivado等）はループを展開して合成します
- ループ回数が入力に依存する場合、多くの合成ツールでエラーになります（シミュレーションは可能）。1クロックで完了させる必要がない処理は、クロックごとに1ステップ進むFSMとして書くことを推奨します
- `while (true)` + `break` のような**無条件ヘッダのループは未対応**です（[実装提案](../../../../design/sv_backend_missing_features.html)参照）

---

## 演算子

| Cm | SV | 備考 |
|----|----|------|
| `+` `-` `*` `/` `%` | 同じ | 算術 |
| `&` `\|` `^` `~` | 同じ | ビット演算 |
| `<<` | `<<` | 左シフト |
| `>>` | `>>` / `>>>` | **符号付き型は `>>>`（算術シフト）** |
| `==` `!=` `<` `<=` `>` `>=` | 同じ | 比較（符号付き定数は`'sd`で出力） |
| `&&` `\|\|` | 同じ | 論理演算 |
| `!x` | `~x` | 論理否定→ビット反転に統合 |
| `x as T` | `N'(x)` 等 | 幅変更はサイズキャスト、符号変更は`$signed`/`$unsigned` |

### 優先順位の保証

Cmソースの式の構造（括弧・評価順序）は生成SVでも保持されます。
SVでは `==` が `&` より優先されるため、括弧が失われると意味が変わりますが、
コンパイラが必要な括弧を必ず出力します:

```cm
if ((r_qm & 256) == 0) { ... }
// → if (((r_qm & 32'd256) == 32'd0))
```

---

← [プロセスと代入](processes.html) | [データ構造](data.html) →
