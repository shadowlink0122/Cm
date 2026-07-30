---
title: native/jit網羅検証で検出したバグ（move・クロージャ・補間式添字）
parent: v0.17.0 Design
---

# native/jit網羅検証で検出したバグ（move・クロージャ・補間式添字）

## 概要

moveセマンティクスを中心にテスト不足領域を対象として、JIT（`cm run`）とnative（`cm compile` -O0/-O2）の差分実行・guard malloc実行・checker受理判定の3面から網羅検証を実施した。
検証プログラムは約80本（move×全型・分岐/ループ/defer/match/ジェネリクス/インターフェース相互作用・数値境界・クロージャ・補間式）で、既知所見（監査H系・構文網羅B1〜B9・差分プローブN1〜N8）と重複しない新規バグをV1〜V8として記録する。
基本的なmove（構造体・固定/動的配列・enumペイロード・ジェネリクス・インターフェース実装型・move連鎖・ブロック外持ち出し・関数戻り値）はnative/jit/最適化レベル間で一致し、guard malloc下でも二重解放・UAFは検出されなかった。

## 新規バグ一覧

| # | 領域 | 概要 | 重大度 |
|---|------|------|--------|
| V1 | 補間 | `{arr[i + 1]}`等の二項式添字が全バックエンドでガベージ値（jit/O0/O2で異なる値） | Critical |
| V2 | 補間 | `{arr[1 + 1]}`が`stoi`切り詰めで`arr[1]`を出力（無診断で誤要素） | Critical |
| V3 | 補間 | 動的配列の式添字`{dyn[i + 1]}`が黙って添字0へフォールバック（全バックエンド一致のため差分検証でも不可視） | Critical |
| V4 | 補間 | `{move s}`がガベージ数値を出力（checkerは受理） | Medium |
| V5 | クロージャ | キャプチャ付きクロージャをユーザー関数の引数に渡すと環境が失われ、欠落引数をゴミ値として読む（jit=1・O0=2・O2=1と分裂、直接呼び出しは正常） | Critical |
| V6 | クロージャ | クロージャを構造体フィールドに格納して呼ぶと同様に環境喪失（jit/O2=誤値10、O0=ゴミ値） | Critical |
| V7 | クロージャ | クロージャを動的配列に格納して呼ぶと`<indirect>`という未解決シンボルを発行しJITはシンボルエラー・nativeはリンクエラー | High |
| V8 | 数値 | `int`の型幅以上シフト`1 << 32`がnative O0=1、jit/O2=0と分裂（仕様未規定・`long << 64`は全経路1） | Medium |

## 再現コード

### V1〜V3: 補間内の式添字

```cm
import std::io::println;
int main() {
    int[3] arr = [10, 20, 30];
    int i = 1;
    println("{arr[i + 1]}");   // 期待30 → jit/native各々異なるガベージ（V1）
    println("{arr[1 + 1]}");   // 期待30 → 20を出力（V2）
    println("{arr[(i + 1)]}"); // 括弧で包むと30で正常（複雑式経路に乗る）
    println("{arr[i + 1] + 100}"); // 130で正常（同上）
    int[] dyn;
    dyn.push(7);
    dyn.push(8);
    dyn.push(9);
    println("{dyn[i + 1]}");   // 期待9 → 7（dyn[0]）を全バックエンドが出力（V3）
    string s = "hello";
    println("{s[i + 1]}");     // 期待l → ガベージ（V1と同族）
    return 0;
}
```

`h.data[i + 1]`（構造体メンバ配列）と`arr[i - 1]`・`arr[i * 2]`も同様にガベージになる。

原因: `src/internal/mir/lowering/expr_println.cpp`の補間ミニパイプラインが添字文字列を`lower_interp_index()`（同21行）で解決するが、対応は数字列（`std::stoi`）と単一変数名のみで、二項式は解決不能になる。
`stoi("1 + 1")`は例外を出さず1を返すためV2は無診断で誤要素になり、スライス経路は解決失敗時に明示的に定数0へフォールバック（同649〜657行）するためV3も無診断になる。
静的配列経路は解決失敗時に未初期化一時をそのまま出力するためガベージになる（V1）。
`{arr[(i + 1)]}`や`{arr[i + 1] + 100}`は複雑式判定により正規の式パイプラインへ回り正常動作することから、修正方針はN1と同様にテキスト再解析の特別扱いを廃し、`name[任意式]`も式パイプラインへ委譲するのが本筋。

### V4: 補間内のmove式

```cm
import std::io::println;
int main() {
    string s = "i" + "i";
    println("{move s}");  // 期待ii → 65751328等のガベージ数値（checkerは受理）
    return 0;
}
```

補間内容`move s`が変数名として解決できず未初期化一時の出力に落ちる。
式パイプライン委譲で解決するか、補間内move式を構文エラーにするかのどちらかに倒す。

### V5〜V7: クロージャの第一級性欠如

```cm
import std::io::println;
int apply(const int*(int) f, int v) {
    return f(v);
}
int main() {
    int base = 7;
    const int*(int) add = (int x) => { return x + base; };
    int direct = add(1);      // 8（正常）
    int via = apply(add, 1);  // 期待8 → jit=1・O0=2・O2=1（V5）
    println("direct={direct} via={via}");
    return 0;
}
```

```cm
// V6: 構造体フィールド格納
struct Holder { const int*(int) f; }
int base = 5;
Holder h = {f: (int x) => { return x + base; }};
h.f(10);  // 期待15 → jit/O2=10・O0=1873784570
```

```cm
// V7: 動的配列格納
const int*(int)[] fs;
fs.push((int x) => { return x + base; });
fs[0](1);  // JIT: Symbols not found: [ _<indirect> ] / native: リンクエラー
```

原因: クロージャはラムダリフティング実装で、値としては生の関数ポインタしか持たない。
`src/internal/mir/lowering/expr_call.cpp`の間接呼び出しは、呼び出し先ローカルの`is_closure`フラグが静的に見える場合のみ`closure_func_name`直呼び＋キャプチャ引数前置に書き換える（275〜284行）ため、引数渡し・フィールド格納・配列格納で情報が消えると、リフト後関数`fn(cap..., param...)`をキャプチャ引数なしで呼び、欠落引数がABI残留値になる（バックエンド・最適化レベルで値が分裂するのはこのため）。
closures-multi-capture.mdの残課題はreduce/sort等builtin高階関数の明示拒否のみを挙げており、ユーザー関数引数・集約格納で黙ってゴミ値になる本件は未記載。
恒久対応は{fnptr, env}のファットポインタ化（js-ts側のbind方式とのABI整合を含む）で、当面は関数型の引数・フィールド・要素へのキャプチャ付きクロージャ代入を型検査で診断付き拒否して黙殺を排除するのが安全。

なお関数型を戻り値とする関数宣言`const int*(int) make(int base)`は構文エラーになり、クロージャ返却は現状表現手段がない（機能ギャップとして記録）。

### V8: 型幅以上のシフト

```cm
int v = 1;
int s = 32;
int r = v << s;   // native O0=1・jit/O2=0
int t = v << 33;  // native O0=2・jit/O2=0
long lv = 1;
long lr = lv << 64;  // 全経路1
```

LLVMの`shl`は型幅以上のシフトがpoisonのため、定数畳み込みされる経路（jit/O2）は0、実行時演算になるnative O0はAArch64のmod挙動で1/2になる。
cm_grammar.md・CANONICAL_SPEC.mdともシフト境界の規定がなく、仕様を決めて（C同様UBにせずマスクまたは0保証）全経路で同一コードを発行すべき。

## checkerの既知課題の具体化（H12系・修正時の回帰ケースとして）

以下はmemory-drop-and-lifetime.md H12が「分岐・ループをまたぐ厳密なCFGフロー解析は将来課題」とする制限の具体的な顕在化で、新規原因ではないが再現ケースとして固定する価値がある。

- 偽陰性（すり抜け）: ループ本体でのmove（`for`/`while`とも）は2周目に移動済み変数を再moveするが受理され、実行時は移動済み値が読める（`while (i < n)`の条件変数を本体でmoveしても同様）。
- 偽陰性: deferブロックが参照する変数をdefer登録後にmoveしても受理され、defer実行時に移動済み値を出力する。
- 偽陰性: ラムダにキャプチャされた変数のmoveは受理され、以後のラムダ呼び出しはmove済み変数を読む（キャプチャは借用として追跡されない）。
- 偽陽性: `if`のthen側でmoveした変数をelse側で使用すると、排他パスにもかかわらずuse-after-moveで拒否される（matchの別アーム間も同様）。
- 偽陽性の帰結: move再代入禁止仕様と組み合わさり、2変数のswap（`t = move a; a = move b; b = move t;`）が記述不能になっている。

現状はランタイムのdropが保守的（fresh限定解放）であるため上記すり抜けはguard malloc下でもクラッシュしないが、memory-drop-and-lifetimeの解放強化が進むと二重解放・UAFに転化するため、CFG化の際に本ケース群を回帰テストへ入れること。

## 検証で問題がなかった領域（記録）

- move基本系: プリミティブ全型・構造体（ネスト/string持ち）・固定長配列・string配列・動的配列・enumペイロード・move連鎖・ブロック外持ち出し・`return move`・ジェネリック関数内move・impl型move後のメソッド呼び出し・クロージャ変数自体のmove（代入経由はキャプチャ情報が伝播し正常）。
- 値意味論: 構造体コピー後の独立変更・大きめ配列の値渡し・インターフェース引数渡し（呼び出し先変更が呼び出し元に非伝搬）。
- 数値境界: `INT_MIN / -1`（wrap）・ゼロ除算（共通ランタイムエラー）・fptosi飽和・符号なしwrap・負数剰余・char/short切り詰め・ulong境界・浮動小数出力はnative/jit/O0/O2一致。
- その他: フォーマット指定子（x/X/o/b/精度/幅）・関数呼び出しを含むグローバル初期化・2次元配列（式添字の直接出力含む）・ネストenumの入れ子match・ガード付きmatchのペイロードmove持ち出し。

## 検証手法（再利用可能）

`.tmp/nativejit-bughunt/harness.sh`は各.cmをJIT・native -O0・native -O2で実行し出力と終了コードの三者比較を行う。
全バックエンド一致でも誤る系（V3）があるため、差分比較に加えて期待値の目視確認とguard malloc（`DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib` + MallocScribble）併用が必要。
## 解決記録（V1〜V8全件実装済み）

### V1〜V4: 式パイプライン委譲

方針どおりテキスト再解析の特別扱いを廃し、式パイプラインへ委譲した。
interp_content_is_expression（interp_internal.hpp）が角括弧内を素通ししていたのを、添字内の演算子（+ - * / % 等）も式マーカーとして検出するよう拡張し、`{arr[i + 1]}`・`{arr[1 + 1]}`・`{dyn[i + 1]}`・`{s[i + 1]}`・`{h.data[i + 1]}`が全て本物の式パーサ経路（lower_interp_expression）で評価されるようになった。
`{move s}`も先頭トークン判定で式パイプラインへ委譲し正しく文字列を出力する（V4）。
多層防御としてlower_interp_indexの数値判定を全桁検証にし、stoi("1 + 1")の黙った切り詰め（V2）を経路残存時も防ぐ。
回帰テスト: tests/common/basic/interp_expr_index.cm（jit O0/O3・native・js・wasmの5モードで検証）。

### V5〜V7: キャプチャ付きクロージャの診断拒否

方針どおり当面対応として型検査で黙殺を排除した。
TypeCheckerにclosure_vars_（キャプチャ付きラムダを束縛したローカルの追跡、コピー伝播あり）を導入し、(1)ユーザー関数の関数型引数への受け渡し（V5）、(2)構造体の関数型フィールドへの格納（V6）、(3)関数型スライスへのpush（V7）を、回避策（ローカル束縛+直接呼び出し、組み込み高階メソッド）を案内する診断付きコンパイルエラーにした。
キャプチャなしラムダ・通常関数の引数渡し、キャプチャ付きクロージャの直接呼び出し・組み込みHOF（map/filter等）は従来どおり動作する（tests/common/lambda/closure_allowed_patterns.cmで固定）。
恒久対応（{fnptr, env}のファットポインタ化）は将来課題として残す。
回帰テスト: tests/common/errors/closure_capture_as_arg / closure_capture_in_field / closure_capture_in_slice。

### V8: mod幅シフト意味論の確定

仕様を「シフト量は左オペランドの型幅でmodを取る」（Rust/AArch64/x86実挙動と同じ、Cのような未定義動作にしない）に確定し、cm_grammar.mdとCANONICAL_SPEC.md（10.1節）へ規定を追記した。
実装: LLVMコード生成のShl/Shrでシフト量を幅-1でマスク（poison回避）、MIR定数畳み込み（folding/sccp）の固定&63を結果型幅マスクへ変更、jsのwide64（BigInt）シフトへ&63nを追加（32bit以下はJSシフトが元来31マスクで一致）。
`1 << 32`=1・`1 << 33`=2・`long << 64`=1・`-16 >> 34`=-4がjit O0/O3・native O0/O2・js・wasmの全経路で一致する。
回帰テスト: tests/common/basic/shift_width_mask.cm。

### checkerの既知課題（H12系）

本文書に固定した偽陰性・偽陽性ケース群はH12のCFGフロー解析化（将来課題）の回帰材料としてそのまま残す（今回の修正対象外）。
