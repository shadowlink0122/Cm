# R17: baremetal-armターゲットが起動コードのmemcpy型不一致で全滅

**ステータス:** 未修正（バックエンド網羅バグ調査で検出）
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
