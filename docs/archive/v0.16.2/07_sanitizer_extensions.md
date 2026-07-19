# 実装設計: サニタイザ拡張（TSan/MSan・.cmconfig.yml・MIRレベル独自チェック）

## 背景・課題

設計06（サニタイザ）で将来課題としていた3項目を実装する。
また、macOS 26.xでLLVM 17のASanランタイムが初期化に失敗する環境起因の問題（原因: 古いcompiler-rtが新しいDarwinのmallocゾーン/シャドウメモリ仕様に未対応）へ対処する。

## 設計

### サニタイザ一覧（拡張後）

| 値 | 検出内容 | native | wasm | jit | 実装 |
|---|---|---|---|---|---|
| `address` | 境界外・use-after-free・二重解放 | ○ | × | × | LLVM AddressSanitizerPass + ランタイムリンク |
| `thread` | データ競合 | ○ | × | × | LLVM ModuleThreadSanitizerPass + ThreadSanitizerPass + ランタイムリンク |
| `memory` | 未初期化読み取り | ○(Linuxのみ) | × | × | LLVM MemorySanitizerPass + ランタイムリンク |
| `bounds` | 静的サイズ確定アクセスの境界外 | ○ | ○ | ○ | LLVM BoundsCheckingPass（trap方式） |
| `undefined` | ゼロ除算・剰余、nullポインタ参照 | ○ | ○ | ○ | MIRレベル計装（Cm独自、panic方式） |

- `memory` はcompiler-rtがLinux系にしかMSanランタイムを提供しないため、macOSでは専用メッセージで拒否する
- `memory` はCmランタイム（C実装）が非計装のため誤検出があり得る（既知の制限としてチュートリアルに明記）
- `thread` のデータ競合E2Eはタイミング依存で不安定なため、計装検証（__tsan_initシンボル）と正常系プローブに留める

### MIRレベル独自チェック（undefined）

`src/internal/mir/passes/instrumentation/undefined.cpp` の `UndefinedCheckInstrumentation`（OptimizationPass準拠）で実装する。
MIR最適化パイプラインの完了後（build.cppのバックエンドディスパッチ時）に1回だけ適用し、挿入したガードが定数伝播で消されないようにする。
MIRレベルの計装のためLLVM系の全実行系（native/wasm/jit）で同一の検出動作になる。

- **ゼロ除算/剰余**: 整数型の `Div`/`Mod` を含むAssign文の直前でブロックを分割し、`SwitchInt(除数, {0 → panicブロック}, otherwise → 続き)` を挿入する。除数が非0定数と確定していれば省略する。浮動小数はIEEE 754で定義済みのため対象外
- **nullポインタ参照**: `Deref` 投影を含むPlaceへのアクセス（Assignの両辺・Rvalue内の全オペランド・Callターミネータの引数/格納先）の直前で、`%t = Eq(ポインタ, null); SwitchInt(%t, {0 → 続き}, otherwise → panic)` を挿入する。対象は型が生ポインタ（`TypeKind::Pointer`）と確定する場合のみ（参照は生成時点で非null保証のため対象外）
- panicブロックは `Call panic("runtime error: ...") → Unreachable`。メッセージは実行時出力のため英語固定（i18n対象外）
- 分割で移動した文の再計装はポインタ同一性の処理済み集合で防ぐ

### .cmconfig.yml

```yaml
compile:
  sanitize: bounds,undefined   # カンマ区切り。[bounds, undefined] のインラインリスト表記も許容
```

- 優先順位: CLI `--sanitize=` > `.cmconfig.yml` > なし（CLI指定があればconfigは完全に無視）
- 値の検証はCLIと共通の `cli::parse_sanitizer_list` で行い、不正値は警告（`CliSanitizeInCmconfigYmlIgnored`）を出して無視する（既存のlanguage:不正時と同じ方針）

### macOS ASanランタイム問題の修正

- 原因: LLVM 17.0.6のcompiler-rt（2023年）がmacOS 26（Darwin 25）で `sanitizer_malloc_mac.inc` のCHECK失敗（Homebrew版）またはシャドウメモリ確保のハング（Apple CLT版）を起こす
- 対処: サニタイザリンク用clang++の探索（`findSanitizerLinkDriver`）で**バージョン非固定のHomebrew LLVM（最新）を最優先**し、無ければllvm@17へフォールバックする。ASan ABIのバージョン記号（`__asan_version_mismatch_check_v8`）はLLVM 9以降不変のため、LLVM 17計装 + 新しいランタイムの組み合わせで動作する（LLVM 22.1.8ランタイムで実機検証済み）
- Apple CLTのclang++はABI記号が `__asan_version_mismatch_check_apple_clang_*` 形式でリンクできないため使用しない
- E2Eテストのプローブ判定は維持する（新しいLLVMも無い環境では実行時検査を明示SKIPし、計装はシンボルレベルで検証）

### テスト計画

- ユニット（`tests/unit/mir_pass_test.cpp`）: 手組みMIRで整数除算の計装（ブロック分割・SwitchInt・panic呼び出し）、非0定数除数と浮動小数の非計装、null参照のEq比較挿入を検証
- E2E（`tests/sanitize/run_tests.sh`）: undefined（native/jit/wasmでpanic）、thread（計装+プローブ）、memory（Linux計装 / macOS拒否）、cmconfig（適用・CLI優先・不正値警告）を追加
- CI: Toolingジョブを `ubuntu-latest` + `macos-15` のマトリクスへ拡大し、wasmtimeをインストールしてwasmレグもCIで実行する

### 複雑ケースの網羅（malloc/free・move・ポインタ）

実務的なメモリバグを検出できることを保証するため、E2Eへ以下を追加する（計46ケース）:

- **ヒープ系（ASan）**: `use libc { malloc/free }` によるヒープ確保に対し、境界外書き込み（heap-buffer-overflow）・use-after-free・二重解放（double-free）が検出され、正常なmalloc/freeは偽陽性を出さないことを検証する。ASanランタイムが健全な環境でのみ実行時レポートを検証し、不健全環境では計装（`__asan_init`）のシンボル検証に留める
- **ポインタ系（undefined）**: 正常なスタックポインタ読み書きの無影響、nullへのストア、null引数を関数へ渡し呼び出し先でderef、ゼロ剰余（`modulo by zero`）を検証する。native/jit/wasm/jsの各実行系で確認する
- **moveセマンティクス**: `move` を含む正常プログラム（ネストVectorの所有権移動）が、計装のガード挿入（Move→Copy複製）で挙動を変えないことをbounds/undefined/addressの各サニタイザで検証する（偽陽性・誤計算の回帰防止）
- **読み取り境界（bounds）**: 書き込みだけでなく読み取りの境界外アクセスもtrapになることを検証する
