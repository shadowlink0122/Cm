# R25: 並行処理の最適化・戻り値の穴（spin-waitがO1+でコンパイル不能・join戻り値のint32切り詰め）

**ステータス:** 未修正（ライブラリ・自動実装調査で検出）
**重大度:** Medium（spin-wait）/ Low〜Medium（join切り詰め）

`native::sync`/`native::thread`の低レベルAPI（mutex/rwlock/channel/atomic + thread spawn/join）はランタイム挙動が健全（実並行実行・Mutex排他・macOS RwLock SIGILL回避・O2/O3での共有変数無破壊をD5で確認）。残る穴は2点で、いずれもコンパイル時または戻り値経路の欠陥。

## 症状（実測: cm 0.17.0、プローブ `.tmp/bughunt8/{concurrency,verify}/`）

### バグ1【Medium】プレーン共有フラグのspin-waitがO1+でコンパイル中断

```cm
int main() {
    int flag = 0;
    while (flag == 0) { }   // 別スレッドがflagを立てるポーリングidiom
    return flag;
}
```
実測: O0はハング（正常なポーリング挙動）だが、**O1/O2/O3はコンパイル自体がabort**:
```
Error: Infinite loop risk detected
Hint: Try -O1 or -O0 option
LLVM code generation error: ... infinite loop or excessive complexity detected
```
最適化器が`flag`をループ不変とみなし（別スレッド更新を認識できない）、無限ループ検出ガードが発火してcodegen全体を止める。**正当なcross-threadポーリングidiomがO1以上でコンパイル不能**。無言デッドロック/miscompileでなくコンパイルエラーなので安全側だが、`flag`にatomic/volatileセマンティクスがないのが根因。対照: `cm_atomic_load_i32`を使うspin-waitはO0/O2/O3すべて正常動作する（正しいパターンは全レベルで動く）。

### バグ2【Low〜Medium】join()が64bitスレッド戻り値をint32に切り詰め

workerが`4294967303`（0x1_0000_0007）を`void*`で返し`join()`で受けると`join returned=7`（上位32bit欠落）。`join()`の戻り型が`int`のため。L7の「int64のみ」は実際にはjoin経路でint32で、pthreadは64bit `void*`を返すためポインタ返却も破損する。戻り型から自明ではあるが無言の欠落。

### 付随【Low】WASMがthread/mutexプログラムを無言でコンパイル

`native::thread`/`sync`を`--target=wasm`でコンパイルすると「スレッド非対応」の診断なしにpthreadシンボルを参照する.wasmを生成する（jsは`void*`禁止で明確に拒否）。これはR23（クロスターゲットFFIの能力ガード欠如）と同一根で、R23の修正でまとめて解消する。

### 健全だった点（D5で確認）

thread spawn/join（実測0.11sで真の並行実行）・Mutex排他（インターリーブ強制でlockなし=50/Mutex=200の対照で有効性を証明）・RwLock（256Bバッファ、macOS SIGILLなし）・全デッドロック系が安全にハング（rc=142、クラッシュ・誤値なし）・Mutex保護変数のO0〜O3無破壊・atomic spin-waitの全レベル動作。

## 修正方針

- **バグ1**: 共有変数のポーリングにatomic/volatileセマンティクスを与える手段を用意する（`volatile`修飾子の実装＝R11、またはatomic読みの言語サポート）。無限ループ検出ガードは、ループ本体が副作用のあるextern呼び出し（atomic load等）を含む場合は発火しないよう緩める。少なくとも「atomicを使え」という誘導診断にする。
- **バグ2**: `join()`の戻り型を`long`（64bit）にし、`void*`戻り値をポインタ幅で受ける。L7の「int64」を実装に一致させる。
- **付随**: R23で対応。

## テスト計画

`tests/llvm/{thread,sync}/`へ: atomicポーリングのO0〜O3動作・join()の64bit戻り値保持の回帰。プレーン共有フラグspin-waitがO1+で（緩和後は）コンパイルできる、または「atomicを使え」の誘導診断が出ることの確認。
