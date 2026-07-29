# オブジェクトファイル出力

ネイティブバックエンドはLLVM TargetMachineの `addPassesToEmitFile` でアセンブラを経由せず直接オブジェクトファイル（.o）を出力する。TargetMachineの構成（triple・CPU・機能フラグ・PIC・CodeModel）はTargetManagerが一元管理し、コード生成そのものはfork隔離＋タイムアウト監視のSafeCodeGeneratorで実行することで、LLVM内部のクラッシュや無限ループからコンパイラ本体のプロセスを守る。

## 概要

ターゲット定義は `src/internal/codegen/llvm/core/context.hpp:14-91` の `BuildTarget` enum（Baremetal/BaremetalX86/Native/Wasm/BaremetalUEFI）と `TargetConfig` 構造体（triple・cpu・features・dataLayout・noStd/noMain・optLevel）にある。ネイティブ構成の決定は `src/internal/codegen/llvm/native/target.cpp:346` の `TargetConfig::getNative()`、TargetMachineの生成は同ファイルの `TargetManager::initialize()`（`target.cpp:33-153`）が担う。

出力経路は `LLVMCodeGen::emit()`（`src/internal/codegen/llvm/native/codegen.cpp:1005-1027`）がObjectFile/Assembly/LLVMIR/Bitcode/Executableへ分岐し、オブジェクト出力は `TargetManager::emitObjectFile`（`target.cpp:162-178`）→ `SafeCodeGenerator::emitObjectFileSafe`（`src/internal/codegen/llvm/native/safe_codegen.hpp:273-318`）と流れる。

## データ構造とアルゴリズム

### TargetMachine構成（target.cpp）

`getNative()` はホストtripleとホストCPU名から開始し、ビルド時マクロ `CM_DEFAULT_TARGET_ARCH` が指すアーキテクチャとホストが食い違う場合はtriple文字列を書き換えてクロス構成にする（arm64ビルド設定でx86_64ホストなら `x86_64`→`aarch64`、逆方向は `arm64`/`aarch64`→`x86_64`、いずれもcpuは `generic` に落とす。`target.cpp:350-377`）。

CPU機能フラグは有効機能の加算だけでなく無効機能の明示否定も渡す。

```cpp
// target.cpp:387-396 — clangの-march=nativeと同様に+/-両方を渡す
for (const auto& feature : features) {
    if (!featureStr.empty())
        featureStr += ",";
    featureStr += (feature.second ? "+" : "-") + feature.first().str();
}
```

CPU名（znver4等）はそのCPUの既定機能（AVX-512等）を含意するため、加算のみだとハイパーバイザが機能をマスクしたVMでも既定機能の命令が生成され、実行時SIGILLになる。この+/-両方式が実ホストの機能集合へ正確に一致させる。環境変数 `CM_TARGET_CPU`/`CM_TARGET_FEATURES` で上書きでき、`CM_DUMP_TARGET=1` で実際に使うtriple/cpu/featuresをstderrへ出力する（`target.cpp:399-414`）。

`TargetManager::initialize()` はLLVM fatal error handlerの設置、ネイティブ・WebAssembly・X86・ARM各ターゲットのレジストリ初期化を行い、最適化レベルをLLVMの `CodeGenOptLevel` へ写像した上でTargetMachineを生成する。

```cpp
// target.cpp:144-146 — PIC + smallコードモデルで生成
targetMachine =
    target->createTargetMachine(config.triple, config.cpu, config.features, options,
                                llvm::Reloc::PIC_, llvm::CodeModel::Small, optLevel);
```

`configureModule()`（`target.cpp:156-159`）がモジュールへtripleとTargetMachine由来のDataLayoutを設定し、MIR→LLVM変換側のサイズ計算（構造体値渡し判定等）と最終コード生成のレイアウトを一致させる。

### 直接オブジェクト出力

外部アセンブラを起動せず、legacy PassManagerに `addPassesToEmitFile` でコード生成パスを積んで実行する。モジュール並列コンパイル用の同期版 `emitObjectFileDirect` が最小形を示す。

```cpp
// target.cpp:195-200
llvm::legacy::PassManager pass;
if (targetMachine->addPassesToEmitFile(pass, dest, nullptr, fileType)) {
    throw std::runtime_error("TargetMachine cannot emit object file");
}
pass.run(module);
dest.flush();
```

`fileType` は `CodeGenFileType::ObjectFile`（アセンブリ出力時は `AssemblyFile`）で、LLVMバージョン差は `LLVM_VERSION_MAJOR` の条件コンパイルで吸収する（`target.cpp:183-187`）。

### fork隔離コード生成（safe_codegen.hpp）

通常経路の `emitObjectFileSafe` は、コード生成を子プロセスへ隔離する `generateToFileForked`（`safe_codegen.hpp:69-167`）をPOSIXで優先使用する。

- 子プロセス: `fork()` 後にPassManagerを構築・実行してファイルへ書き出し、`_exit` で即終了する（atexit等を実行しない）。出力先オープン失敗は4、emit不可は2、成功は0の終了コードプロトコル（`safe_codegen.hpp:89-106`）
- 親プロセス: `waitpid(WNOHANG)` をポーリングし、デッドライン超過で `SIGKILL`→ゾンビ回収してタイムアウトエラーにする（`safe_codegen.hpp:108-127`）。書きかけファイルのサイズも監視し、100MB（`MAX_OUTPUT_SIZE`）超過でも子をkillする（`safe_codegen.hpp:128-137`）
- 子がシグナルで死んだ場合は「Code generation subprocess crashed (signal N)」としてエラー化する（`safe_codegen.hpp:158-161`）。LLVMのSegfault/assert失敗がcmプロセス自体を巻き込まない

fork隔離が選ばれる理由はヘッダのコメントに明記されている: 従来のスレッド方式はタイムアウト時にdetachするしかなく、(1)破棄済みスタックへの書き込み（use-after-free）、(2)LLVMの内部ループがGB級メモリを保持したまま残留する、という2つの問題があった。子プロセスならSIGKILLで計算資源ごと確実に回収できる（`safe_codegen.hpp:62-68`）。前提条件として、コード生成が単一スレッドで走ること（他スレッド保有ロックを子が引き継ぐとデッドロックしうる）が不変条件である。

`fork` 自体が失敗した場合と非POSIX環境では、スレッド+メモリバッファ方式の `generateToMemory`（`safe_codegen.hpp:171-270`）へフォールバックする。この経路は共有状態を `shared_ptr` でヒープに置き値キャプチャでスレッドへ渡すため、タイムアウトdetach後もuse-after-freeにならない（`safe_codegen.hpp:189-194`・`249-257`）。

タイムアウトは既定30秒で、環境変数 `CM_CODEGEN_TIMEOUT` により上書きできる（`safe_codegen.hpp:50-59`）。大規模プロジェクトの正当な長時間生成と、テストでのタイムアウト経路検証の両方に対応する。

### 生成前バリデーションと複雑度チェック

`emitObjectFileSafe` はコード生成前に `PreCodeGenValidator::validate`（`src/internal/codegen/llvm/native/loop_detector.hpp:88-91`）で無限ループ・過剰複雑度を検査する。ただしUEFI/ベアメタルtriple（`windows`/`none` を含む）では意図的な無限ループ（halt/hang）が正常なためスキップする（`safe_codegen.hpp:276-284`）。`TargetManager::emitObjectFile` は併せて `checkComplexity`（関数1万・命令100万の警告閾値、`safe_codegen.hpp:373-400`）を通し、タイムアウト時のエラーメッセージには最適化レベルを下げるヒントを付与する（`target.cpp:167-177`）。

### ベアメタル向け付帯生成

Baremetalターゲットではオブジェクト出力に加えて、FLASH/RAM配置のリンカスクリプト生成（`target.cpp:223-269`）と、MSP設定・.data/.bss初期化・main呼び出しを行う `_start` スタートアップコードのIR合成（`target.cpp:272-343`）を行う。

## 実装箇所

| ファイル | 役割 |
|---|---|
| `src/internal/codegen/llvm/core/context.hpp` | BuildTarget enumとTargetConfig（ターゲット別triple/cpu/features既定値） |
| `src/internal/codegen/llvm/core/context.cpp` | LLVMContext初期化（triple/DataLayout設定・std/no_stdランタイム宣言） |
| `src/internal/codegen/llvm/native/target.cpp` / `target.hpp` | TargetManager（ターゲット初期化・TargetMachine生成・emit入口・ベアメタル付帯生成）と `getNative()` |
| `src/internal/codegen/llvm/native/safe_codegen.hpp` | SafeCodeGenerator（fork隔離・タイムアウト・SIGKILL回収・スレッドフォールバック・複雑度チェック） |
| `src/internal/codegen/llvm/native/loop_detector.hpp` / `loop_detector.cpp` | PreCodeGenValidator（生成前の無限ループ/複雑度検査） |
| `src/internal/codegen/llvm/native/pass_debugger.hpp` | 最適化パスのデバッグ支援 |
| `src/internal/codegen/llvm/native/codegen.cpp` | emit()分岐・emitObjectFile/emitAssembly/emitLLVMIR/emitBitcode/emitExecutable |

## 落とし穴とケア

- fork隔離はコード生成が単一スレッドで走ることを前提とする。並列モジュールコンパイルのワーカ内では隔離なしの `emitObjectFileDirect`（`target.cpp:182-201`）を使う設計になっており、fork経路を並列コンテキストへ持ち込んではならない。
- CPU機能は+/-両方を渡す不変条件を守ること（`target.cpp:387-396`）。加算のみに戻すと、機能マスクされたVM上で既定機能命令が生成されて実行時SIGILLになるバグのクラスが再発する。調査時は `CM_DUMP_TARGET=1` と `CM_TARGET_CPU`/`CM_TARGET_FEATURES` で切り分けられる。
- 子プロセスは必ず `_exit` で終了する（`safe_codegen.hpp:90-105`）。`exit` に変えると親から引き継いだatexitハンドラやLLVMのグローバルデストラクタが二重実行される。
- タイムアウト後の子プロセスは `SIGKILL`→`waitpid` の順で必ずゾンビ回収する（`safe_codegen.hpp:121-127`）。
- モジュールのDataLayoutはTargetMachineから設定する（`target.cpp:156-159`）。これを怠るとMIR→LLVM変換時のサイズ判定（構造体値渡し/sret閾値）と実レイアウトがずれ、ABI不一致を起こす。
- PreCodeGenValidatorのベアメタル/UEFIスキップ条件（triple文字列判定、`safe_codegen.hpp:278-281`）を変更する際は、意図的無限ループを持つカーネル/UEFIサンプルが誤検知されないことを確認する。
- 回帰テスト: バックエンドスイート（`make test-llvm`）が生成オブジェクトの実行まで検証し、タイムアウト経路は `CM_CODEGEN_TIMEOUT` を小さく設定して検証できる（`safe_codegen.hpp:47-49`）。
- デバッグ情報オプション（`--debug`/`-d` 由来の `config.debugInfo`）はオプション配管とキャッシュキーへの反映のみで実体を持たない: ドライバが `opts.debug` を転写し（`src/cmd/cm/backend/llvm.cpp:86`）、`TargetConfig` への反映とキャッシュキーへの算入までは行われる（`src/internal/codegen/llvm/native/codegen.cpp:196, 215`）が、`LLVMContext` の `debugBuilder`（DIBuilder、`core/context.hpp:100`）は宣言のみで使用箇所がなく、DWARF等のデバッグ情報は生成されない。

## 関連資料

- [MIR→LLVM IR変換の構造](mir-to-llvm.md)
- [リンクとランタイム解決](linking-and-runtime.md)
- 差分ビルド・モジュール並列コード生成の設計: [../../archive/v0.17.0/incremental-build-and-parallel-codegen.md](../../archive/v0.17.0/incremental-build-and-parallel-codegen.md)
