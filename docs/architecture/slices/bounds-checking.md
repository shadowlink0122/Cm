# 配列・スライスの境界検査ポリシー

Cmの境界検査は「既定は無検査（性能優先）、`--sanitize=bounds`指定時は全実行系で統一トラップ」という二段構えを取る。スライスはランタイム関数呼び出し越しのアクセスでありLLVMの静的境界検査が届かないため、MIRレベルの計装パス`instrument_bounds_checks`が添字アクセス呼び出しの直前へ`0 <= index < len`の検査を挿入し、違反時は`cm_bounds_error`が統一メッセージ・終了コード1で停止する。固定長配列はLLVMの`BoundsCheckingPass`が補完する。

## 概要

native/jitの既定ビルドでは、スライスの境界外アクセスはランタイム関数側のセンチネル返しに委ねられる。読み取り（`cm_slice_get_*`）は範囲外で0/NULLを返し（`src/internal/codegen/llvm/native/runtime/slice.c:372-373`等）、書き込みの土台である`cm_slice_get_element_ptr`は範囲外でNULLを返す（`runtime/slice.c:778-779`）ため、範囲外書き込みはNULLストアとしてSIGSEGVになる。`cm_slice_delete`の範囲外は何もしない（`runtime/slice.c:463-464`）。この既定挙動には検査コストが無い代わりに、範囲外読みが「黙って0を返す」ことを許す。

`--sanitize=bounds`を指定すると、検査は2系統で挿入される。スライスはMIR計装パス（LLVMに依存しないため全実行系で同一動作）、固定長配列はLLVMの`BoundsCheckingPass`（alloca等の静的にサイズが分かるアクセスへのトラップ挿入）である。両者は補完関係にあり、`src/cmd/cm/build.cpp:652-654`のコメントがこの分担を明記している。

## データ構造とアルゴリズム

### 検査の挿入位置（MIR計装パス）

`--sanitize=bounds`時、MIR最適化の後に`instrument_bounds_checks`（`src/internal/mir/passes/instrumentation/bounds.cpp:173-180`）が全関数へ1回適用される。適用箇所は`src/cmd/cm/build.cpp:655-658`で、sv以外の全ターゲット（native/jit/wasm/js）が対象になる。

検査対象は「第0引数=スライス、第1引数=インデックス」契約のランタイム呼び出しで、名前で判定する（`bounds.cpp:23-29`）:

```cpp
bool is_indexed_slice_access(const std::string& name) {
    if (name.rfind("cm_slice_get_", 0) == 0) {
        // subsliceは対象外（契約が異なる）
        return name != "cm_slice_get_subslice";
    }
    return name == "cm_slice_delete";
}
```

これにより添字読み（`cm_slice_get_i32`等のスカラget）、書き込みの土台（`cm_slice_get_element_ptr`）、`delete`が検査される。読み書きどちらも要素アクセスは必ずこれらの`Call`ターミネータを経由するため、MIRでの呼び出し前検査が「添字アクセスの検査挿入位置」として網羅的に機能する。

挿入されるCFGはブロック分割方式で、`bounds.cpp:1-8`のコメントどおり次の形になる。

```
B:    ...既存文...; Call cm_slice_len(slice) -> len_local, success=B1
B1:   t1 = Lt(index, 0);   SwitchInt(t1, {0 -> B2}, otherwise -> ERR)
B2:   t2 = Lt(index, len); SwitchInt(t2, {0 -> ERR}, otherwise -> CONT)
CONT: 元のスライスアクセス呼び出し
ERR:  Call cm_bounds_error(index, len) -> Unreachable
```

実装は`bounds.cpp:71-171`で、`cm_slice_len`呼び出しの挿入（:117-128）、負判定（:130-146）、範囲判定（:148-162）、エラーブロック生成（:50-67）から成る。インデックスが符号なし定数で負になり得ない場合も一律に検査する（単純さ優先、`bounds.cpp:8`）。

### トラップの実装

`cm_bounds_error`はランタイムのnoreturn関数で、`src/internal/codegen/llvm/native/runtime/print.c:261-266`:

```c
__attribute__((noreturn)) void cm_bounds_error(long long index, long long len) {
    printf("error: index out of bounds: index %lld, length %lld\n", index, len);
    fflush(stdout);
    exit(1);
}
```

wasmランタイムにも同名関数があり（`src/internal/codegen/llvm/wasm/runtime/core.c:118`）、全実行系で同一メッセージ・終了コード1に統一されている。シグナルではなくメッセージ付きの正常系exitであるため、テストハーネスが出力を検証できる。

### 固定長配列（LLVM BoundsCheckingPass）

固定長配列（alloca上の`[N x T]`）へのアクセスはGEPとして静的にサイズが見えるため、LLVMの`BoundsCheckingPass`で検査する。nativeでは`instrumentSanitizers`（`src/internal/codegen/llvm/native/codegen.cpp:945-990`）が`options.sanitizeBounds`のときパスを追加し、JITでは`JITEngine::execute`が最適化後に同じパスを適用する（`src/internal/codegen/llvm/jit/jit_engine.cpp:222-237`）。違反時は`llvm.trap`による即時停止で、ランタイム関数を必要としないためnative/jit両対応である。

スライスがこのパスの対象外である理由は、データバッファが`cm_alloc`による不透明なヒープ確保であり、アクセスが関数呼び出し越しのためLLVMから割り当てサイズが見えないことにある。この「見えない」領域をMIR計装パスが埋める、という役割分担である。

### オプションの流れとO0/O2の扱い

- CLIパース: `--sanitize=`の有効値は`{"address","thread","memory","bounds","undefined"}`（`src/cmd/cm/options.cpp:28-31`）。
- native AOT: `sanitizeBounds`フラグ設定は`src/cmd/cm/backend/llvm.cpp:117-125`。
- JIT: `cm run`は`opts.sanitizers`から`bounds`を検出して`JITEngine::execute`へ渡す（`src/cmd/cm/backend/run.cpp:112-117, :141-143`）。JITはプロセス内実行のためASan等のランタイム後付けが不可能であり、`bounds`/`undefined`のみ許可される（`src/cmd/cm/build.cpp:630-636`）。
- 最適化レベルとの関係: 検査挿入は`--sanitize=bounds`の有無だけで決まり、O0/O2いずれでも既定ビルドには検査コードが一切入らない（性能への影響ゼロ）。sanitize有効時は、MIR計装がMIR最適化後に適用され（`build.cpp:648-658`）、JITのLLVMパスも`optimizeModule`の後に適用される（`jit_engine.cpp:220-237`）ため、最適化が検査を除去してしまうことはない。

## 実装箇所

| ファイル | 役割 |
|---|---|
| src/internal/mir/passes/instrumentation/bounds.cpp | スライスアクセスへのMIRレベル境界検査挿入パス |
| src/internal/mir/passes/instrumentation/bounds.hpp | パスのインターフェイス（`instrument_bounds_checks`） |
| src/cmd/cm/build.cpp:648-658 | sanitize検証と計装パスの適用（MIR最適化後・コード生成前） |
| src/internal/codegen/llvm/native/runtime/print.c:261-266 | `cm_bounds_error`トラップ関数（native/jit） |
| src/internal/codegen/llvm/native/codegen.cpp:945-990 | 固定長配列向け`BoundsCheckingPass`の適用（native AOT） |
| src/internal/codegen/llvm/jit/jit_engine.cpp:183-237 | JITでの`BoundsCheckingPass`適用（`sanitizeBounds`引数） |
| src/cmd/cm/options.cpp:28-31 | `--sanitize=`の有効値定義 |
| src/cmd/cm/backend/llvm.cpp:117-125 / run.cpp:112-143 | AOT/JITそれぞれへのフラグ伝搬 |
| src/internal/codegen/llvm/native/runtime/slice.c:367-455, :774-783 | 既定ビルドのセンチネル挙動（OOB読み0/NULL） |

## 落とし穴とケア

- 防ぐバグのクラス: 境界外アクセスの挙動がバックエンドごとに分裂する問題（native/jitは読み0・書きSIGSEGV、他実行系は別挙動）と、「範囲外読みが黙って0を返し計算が静かに壊れる」問題。sanitize有効時は全実行系が同一メッセージのトラップに統一されるため、分裂に依存したテストの偽陽性も防げる。
- 維持すべき不変条件: 添字1つを取る新しいスライスアクセス関数をランタイムへ追加するときは、`is_indexed_slice_access`（`bounds.cpp:23-29`）の対象に含まれるか必ず確認すること。名前プレフィクス`cm_slice_get_`の規約に従えば自動で対象になるが、契約が「第0引数=スライス、第1引数=インデックス」でない関数（subslice系）は明示的に除外する必要がある。
- 検査はMIRの`Call`ターミネータ名に基づくため、スライスアクセスをランタイム呼び出しを経ない形（直接GEP等）へ最適化・変更した場合、その経路は検査から漏れる。スライス要素アクセスは必ずランタイム関数経由という設計を崩さないこと。
- 固定長配列の`BoundsCheckingPass`は`llvm.trap`によるシグナル停止であり、スライスのメッセージ形式（`error: index out of bounds: ...`）とは出力が異なる。メッセージの完全統一は性能とランタイム依存のトレードオフとして未統一のままである。
- 既定ビルド（sanitize無効）の範囲外書き込みはNULLデリファレンスのSIGSEGVであり未定義動作ではないが、範囲外読みの0センチネルはバグを隠す。境界起因が疑われる不具合の調査ではまず`--sanitize=bounds`を付けて再現すること。
- 回帰テスト: `tests/sanitize/cases/oob/`（`slice_read.cm`・`slice_write.cm`がスライス、`read.cm`・`write.cm`が固定長配列）を`tests/sanitize/run_tests.sh`が実行し、sanitize有効時のトラップメッセージと、無効時の既定挙動維持の両方を検証する。

## 関連資料

- [スライスのランタイム表現](runtime-representation.md) — 検査対象となるランタイム関数群の定義
- [境界チェック統一ポリシー設計文書（archive）](../../archive/v0.17.0/arrays-slices/bounds-checking-policy.md) — バックエンド分裂の調査記録と設計判断
