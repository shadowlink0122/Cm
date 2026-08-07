# R17: baremetal-armターゲットが起動コードのmemcpy型不一致で全滅

**ステータス:** 修正済み（size型のポインタ幅整合＋起動コードの意味修正＋armコンパイルゲート追加）
**重大度:** Critical

`--target=baremetal-arm`は最小プログラムすらコンパイルできない。`tests/baremetal/allowed/`の既知の正常系テストも全て失敗する。テストスイートがx86のみ実行しているため露見していなかった。

## 症状（実測: cm 0.17.0、プローブ `.tmp/bughunt7/{freestanding,verify}/`）

```cm
int main() {
    const int x = 42;
    return x;
}
```
実測:
- `--target=baremetal-arm`: **rc=1**。`Call parameter type does not match function signature! %4 = call ptr @memcpy(ptr, ptr, i64 %3)`（宣言は`declare ptr @memcpy(ptr, ptr, i32)`）→ `LLVM code generation error: LLVM module verification failed`。生のLLVM IR全体がstderrにダンプされ、ソース位置・行番号がない。
- `--target=baremetal-x86`: **rc=0**（正常）。
- `tests/baremetal/allowed/allowed_struct.cm`・`allowed_arithmetic.cm`・`baremetal/minimal_compile.cm`もarmで全て同じ失敗。

真因: `src/internal/codegen/llvm/native/target.cpp`の`generateStartupCode`（`config.target == BuildTarget::Baremetal`＝armのみ発動）が呼ぶ`generateDataInit`・`generateBssInit`が、`memcpy`/`memset`を第3引数`Int32Ty`で宣言する一方、`CreatePtrDiff`の戻り値（i64）をそのまま実引数に渡す。armは32bitポインタ（`set_target_pointer_size`でpointer=4）なのにsize実引数がi64になり型不一致でLLVM検証失敗。x86は`generateStartupCode`が早期returnするためこの経路を通らず助かっている。

## 修正方針

- `generateDataInit`/`generateBssInit`の`memcpy`/`memset`宣言の第3引数を、ターゲットのポインタ幅に一致するsize型（armは`Int32Ty`、x86_64は`Int64Ty`）にするか、`CreatePtrDiff`の結果を宣言済みsize型へ`trunc`/`sext`で合わせる。ポインタ幅は`set_target_pointer_size`が持つ値を参照する。
- baremetal-armをCIのコンパイルゲートへ追加し、x86のみ実行の盲点を塞ぐ（`tests/baremetal/allowed/`をarm/x86両方でコンパイル検証）。
- LLVM検証失敗時に生IRダンプでなくソース位置付きの内部エラー診断を出す（R14の診断品質改善と共通）。

## テスト計画

`tests/baremetal/allowed/`の全ケースを`--target=baremetal-arm`でコンパイル成功させる回帰。data/bssセクション初期化を伴うプログラム（初期化子付きグローバル）でのmemcpy/memset生成の型整合をarm/x86両方で検証。

## 実装記録

- **型不一致の修正（`src/internal/codegen/llvm/native/target.cpp`）**: `generateDataInit`/`generateBssInit`の`memcpy`/`memset`宣言のsize引数を`module.getDataLayout().getIntPtrType()`（ターゲットのポインタ幅。arm=i32）にし、`CreatePtrDiff`の結果を`CreateIntCast`で同型へ揃えた。DataLayoutは`configureModule`が`generateStartupCode`より先に設定するため参照可能。最小`int main`が`--target=baremetal-arm`でrc=1（LLVM検証失敗）→rc=0になることを新旧バイナリ比較で確認した。
- **起動コード自体の意味修正2件（実装中に発見・同時修正）**: (1)リンカシンボル`_sdata`/`_edata`/`_sidata`/`_sbss`/`_ebss`はアドレス自体が境界なのに、旧実装はグローバルとして`load`した値をポインタに使っており、データセクション先頭の内容をアドレスとして解釈する誤った初期化コードだった——loadを廃してシンボルのアドレスを直接使う標準Cイディオム（`&_edata - &_sdata`）に修正。(2)`CreatePtrDiff`の要素型がポインタ型でバイト差がポインタ幅で除算されておりコピー/クリアサイズが1/4（arm）になっていた——i8要素のptrdiffでバイト数を得るよう修正。生成armオブジェクトの逆アセンブルでGOT経由のシンボルアドレス取得→バイト数減算→memcpy/memset呼び出しの正しい列になることを確認した。あわせて`memset`の第2引数をC標準シグネチャどおりi32へ（旧i8）。
- **armコンパイルゲート**: `tests/runner/{execute,drivers}.sh`の`llvm-baremetal`ドライバを、x86コンパイル成功時にarmでも検証する二段構成にした（エラーテストはx86失敗時点で判定されるため経路不変）。`make test-baremetal`（11件）はallowed全件がarm/x86両方を通過して全PASS、旧バイナリではarmゲートがrc=1で正しく検出することを確認した。UEFI（21件）・unit・regression・llvmスイートも全PASS。
- **範囲外**: LLVM検証失敗時に生IRをダンプする診断品質（ソース位置なし）はR14の領分として残置。実機（qemu等）での起動実行検証はテスト基盤外の将来課題（本修正はコンパイルゲートと逆アセンブル確認まで）。チュートリアルにbaremetal-arm/x86ターゲットの既存ページはなく、新規構文・APIも増えていないため更新対象なし。
