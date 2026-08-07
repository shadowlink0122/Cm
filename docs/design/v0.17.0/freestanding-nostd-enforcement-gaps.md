# R18: フリースタンディング制約の強制漏れ（文字列連結のヒープ確保・関数ポインタ経由バイパス・float非対称）

**ステータス:** 未修正（バックエンド網羅バグ調査で検出）
**重大度:** High（ヒープ確保の黙殺・ブロックリスト回避）/ Medium（float非対称）

baremetal/UEFIのno-std制約（`src/internal/mir/passes/validation/no_std_checker.cpp`、名前ベースのブロックリスト）に穴があり、フリースタンディングで使えないはずのヒープ確保が診断なしでコンパイルを通る。直接呼び出しの拒否は健全だが、名前ベースゆえ2経路で回避される。

## 症状（実測: cm 0.17.0、プローブ `.tmp/bughunt7/{freestanding,verify}/`。ターゲット=baremetal-x86/uefi）

### バグ1【High】文字列連結（ヒープ確保）が無診断でコンパイルを通る

```cm
int main() {
    string a = "foo";
    string b = "bar";
    string c = a + b;
    return c.len() as int;
}
```
実測: baremetal-x86 / uefi 双方**rc=0**。生成オブジェクトに`cm_string_concat`が参照される（`"val=" + n`では`cm_int_to_string`・`cm_string_free`も）。`cm_string_concat`の実体（`runtime_format.c`）は`cm_str_alloc`（＝malloc）を呼ぶ。NoStdCheckerのブロックリストは`cm_print*`/`cm_println*`/`cm_file_*`/`cm_read_*`/`cm_io_*`のみで、`cm_string_*`・`cm_*_to_string`・`cm_str_alloc`が漏れている。malloc直呼びは正しく拒否されるのに、文字列連結という頻出操作でヒープ確保が黙って通る（実行時クラッシュの温床）。

### バグ2【High】関数ポインタ経由の間接呼び出しでブロックリストを回避

```cm
use libc { int putchar(int c); }
int main() {
    int *(int) f = &putchar;   // 禁止関数のアドレスを取得
    int r = f(65);             // 間接呼び出し
    return r;
}
```
実測: 直接`putchar(65)`はrc=1で拒否されるが、`&putchar`の間接呼び出しは baremetal-x86 / uefi 双方**rc=0**（オブジェクトに`putchar`シンボルが残る）。NoStdCheckerが`MirTerminator::Call`の`FunctionRef`名しか見ず、関数ポインタのcallee名を追えないため。任意の禁止libc関数をアドレス経由で持ち出せば制約を完全にすり抜ける。

### バグ3【Medium】floatがbaremetal-x86で失敗・UEFIで成功する非対称

```cm
double compute(double a, double b) { return a * b + 1.5; }
int main() { double r = compute(2.0, 3.0); return r as int; }
```
実測: baremetal-x86 rc=1（`SSE register return with SSE disabled`、ソース位置`<unknown>:0:0`）・uefi rc=0（UEFI x86_64 ABIはSSE有効）。x86 baremetalでSSE無効は妥当だが、診断がLLVM内部エラー・行番号なしでユーザーに不可解。

### 健全だった点

malloc・printlnの直接呼び出しおよびラッパー関数経由（`int* w(int n){return malloc(n);}`）は両ターゲットでrc=1で正しく拒否され、診断も一貫（`function 'w' uses 'malloc'; OS heap memory management is not available in bare-metal environments`）。

## 修正方針

- **バグ1**: NoStdCheckerのブロックリストへ`cm_string_*`・`cm_*_to_string`・`cm_str_alloc`（およびヒープ確保を伴う全ランタイムヘルパ）を追加する。理想はブロックリスト（拾い漏れが構造的に起きる）でなくホワイトリスト＋「このランタイムヘルパはヒープ確保する」フラグをレジストリ（runtime-builtin-registry）へ持たせ、フリースタンディングで確保フラグ付きを一律拒否する。
- **バグ2**: 関数のアドレス取得（`&func`）時にも、対象が禁止関数ならNoStdCheckerが検出する（FunctionRefだけでなくアドレス取得サイトも走査）。
- **バグ3**: SSE無効ターゲットでのfloat/double使用をCmレベルで「baremetal-x86では浮動小数が使えない（またはソフトfloatが要る）」の専用診断にし、LLVM内部エラーを露出させない。UEFIとの非対称は仕様として明文化。

## テスト計画

`tests/baremetal/errors/`・`tests/uefi/`へ: 文字列連結・int→string・関数ポインタ経由の禁止関数呼び出しが診断で拒否される負のテスト。baremetal-x86でのfloat使用の専用診断。UEFIとbaremetalで同一std依存コードが同一診断になる一貫性検証。
