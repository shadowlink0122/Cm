# R16: SVコード生成が不正な構文を無診断で受理（bit[0]・桁あふれ・入力ポート書込み・always_ff黙殺・属性タイポ）

**ステータス:** 未修正（バックエンド網羅バグ調査で検出）
**重大度:** Medium（不正SV生成・ピン制約欠落）/ Low（桁あふれ・引数型・native流入診断）

SV固有構文の誤用をCm側が検査せず、iverilogが弾く不正SVを生成するか、ピン制約を静かに落とす。R7（属性タイポ黙殺）のSV名前空間版を含む。いずれもCmレベルの診断がなく、ユーザーはiverilogのエラーか、最悪は合成後の配線欠落で初めて気づく。

## 症状（実測: cm 0.17.0、プローブ `.tmp/bughunt7/{sv,verify}/`。生成SVは`iverilog -g2012`で検証）

| # | ケース | Cm挙動 | 下流 | 重大度 |
|---|--------|--------|------|--------|
| 1 | `#[sv::pinn("38")]`（pinのタイポ） | 無診断でコンパイル成功（`#[sv::pin]`と区別されず） | ピン制約が静かに欠落（配線したつもりが無ピン） | Medium |
| 2 | `#[input]`ポートへ代入（`a = 5;`） | 無診断で`a <= 5;`（aはinput wire）を生成 | iverilog `'a' is not a valid l-value` | Medium |
| 3 | `always_ff void f()`（エッジ無し） | 無診断で`always_comb begin`へ変換（always_ff指定を黙殺） | NBA残存でiverilog警告・意図と異なる回路 | Medium |
| 4 | `bit[0] z`（幅0） | 無診断で`output logic z = 0'd0`を生成 | iverilog `Sized numeric constant must have a size greater than zero` | Medium |
| 5 | `4'd99`（4bit桁あふれ） | 無診断で`4'd99`を出力 | iverilog truncated警告で3へ切詰め | Low〜Medium |
| 6 | `#[sv::pin(12345)]`（非文字列引数） | 無検証で制約に`12345`を出力 | 引数型バリデーションなし | Low |
| 7 | SV固有構文のnative流入（`assign y = 2;`・`posedge clk;`・`bit[8] x`） | `assign`を型名と誤解釈（`expected 'assign', got 'int'`）・`posedge clk;`は黙殺受理（未使用警告のみ）・`bit[8]`は型不一致扱い | 「SV専用構文」の誘導なし | Low〜Medium |

認識される`sv::`属性は`{pin, iostandard, lutram, bram, module_name, param, parameter, pipeline, share, sync, tri}`（`verilog::`別名あり）で、いずれも完全一致比較のため綴り違いは全て黙殺される（`src/internal/syntax/parser/module/attribute.cpp`が名前空間付き名を無検証で受理）。健全な部分: `#[sv::pin("38")]`正常系・`bit[9999]`・`bit[-1]`（パースエラーで停止）はそれぞれ正しく扱われる。

## 修正方針

- SVコード生成の前段に「SV妥当性検査」を置き、(2)入力ポートへの手続き代入・(3)エッジ指定なし`always_ff`・(4)`bit[0]`/幅0・(5)幅付きリテラルの桁あふれをCm診断で停止する（iverilog任せにしない）。診断はDiagnosticEmitter経由でソース位置付き。
- (1)(6)属性検証はR7（attribute-validation-registry）の既知属性レジストリを`sv::`名前空間へ拡張し、タイポと引数型を検査する（`sv::pin`は文字列引数必須）。
- (7)native流入は、SV専用トークン（`bit[N]`・always系・`assign`・`posedge`/`negedge`）を非SVターゲットで使ったとき「この構文はSVターゲット専用」の専用診断へ（R14の診断品質改善と併せて）。

## テスト計画

`tests/sv/errors/`へ各ケースのエラーテスト（Cm診断で停止することを固定）。`#[sv::pinn]`等タイポの警告・`sv::pin`引数型検査。native流入の専用診断。
