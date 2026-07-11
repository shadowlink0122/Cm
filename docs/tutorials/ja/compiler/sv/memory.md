# SVバックエンド - メモリ初期化（ROM/RAM）

命令ROM・フォントROMなどのメモリ初期値を出力する3つの方法。

## 1. 配列リテラル初期値（小規模向け）

配列の初期値は `initial` ブロックとして出力されます。FPGA合成ツールはROM/RAMの初期内容として扱い、シミュレーションでは時刻0に初期化されます:

```cm
utiny[4] rom = [10, 20, 30, 40];
```

```systemverilog
logic [7:0] rom [0:3];
initial begin
    rom[0] = 10;
    rom[1] = 20;
    rom[2] = 30;
    rom[3] = 40;
end
```

`#[sv::bram]` / `#[sv::lutram]` 属性付き配列にも対応します。

## 2. `#[sv::memfile]` 属性（大規模向け）

hexファイルからの読み込み（`$readmemh`）を出力します。巨大なフォントROM等をcase文で記述する必要がなくなります:

```cm
#[sv::bram]
#[sv::memfile("font.hex")]
utiny[4096] font_rom;
```

```systemverilog
(* ram_style = "block" *) logic [7:0] font_rom [0:4095];
initial $readmemh("font.hex", font_rom);
```

- 初期値なし配列にも使用可能（hexファイルは合成/シミュレーション環境で用意）
- 配列リテラル初期値と併用した場合は `$readmemh` が優先されます

## 3. `--emit-memfile` オプション

配列リテラル初期値を `.hex` ファイルとしてコンパイル時に書き出します（出力先は生成SVと同じディレクトリ）:

```bash
cm compile --target=sv rom.cm -o rom.sv --emit-memfile
```

```cm
#[sv::memfile("table.hex")]
utiny[4] table = [16, 32, 255, 0];
```

生成される `table.hex`:

```
10
20
ff
00
```

Cmソースで初期値を管理しつつ、生成SVは `$readmemh` 参照になるため、大規模ROMでもSVファイルが肥大化しません。

回帰テスト: `tests/sv/memory/array_init`、`tests/sv/memory/readmemh`

---

<!-- nav -->
← 前: [SVバックエンド - データ構造](data.html) ｜ [目次](index.html) ｜ 次: [SVバックエンド - モジュール階層の保持](hierarchy.html) →
