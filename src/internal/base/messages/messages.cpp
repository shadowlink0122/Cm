// Cmコンパイラ メッセージカタログ本文テーブルの定義（table[メッセージ][言語]）。行 = MsgId の宣言順、列 = Lang の順（En, Ja）。
// テンプレートの {0} {1} ... は i18n::msgf のプレースホルダ。訳が無い言語は nullptr にすると英語へフォールバックする
// 行の過不足は配列サイズ kMessageCount との不一致としてコンパイル時に検出される

#include "messages.hpp"

#include <cstddef>

namespace cm::i18n {

// clang-format off
constexpr const char* kMessages[kMessageCount][kLangCount] = {
    // ===== cli =====
    // CliAsyncAwaitIsOnlySupported
    {"error: async/await is only supported on the JS target\n",
     "エラー: async/awaitはJSターゲット専用の機能です\n"},
    // CliAsyncFunctionDetected
    {"  async function detected: {0}\n",
     "  async関数が検出されました: {0}\n"},
    // CliAwaitExpressionDetectedFunction
    {"  await expression detected (function: {0}）\n",
     "  await式が検出されました（関数: {0}）\n"},
    // CliCannotOpenFile
    {"error: cannot open file: {0}",
     "エラー: ファイルを開けません: {0}"},
    // CliCannotWriteFile
    {"error: cannot write file: {0}\n",
     "エラー: ファイルに書き込めません: {0}\n"},
    // CliCheckComplete
    {"\n=== Check complete ===\n",
     "\n=== チェック完了 ===\n"},
    // CliClockDrivenTestsRequirePlatform
    {"hint: clock-driven tests require //! platform: sv at the top of the file\n",
     "ヒント: クロック駆動のテストはファイル先頭に //! platform: sv を指定してください\n"},
    // CliCmRunDoesNotSupport
    {"error: cm run does not support direct execution of --target=wasm\n",
     "エラー: cm run は --target=wasm の直接実行に未対応です\n"},
    // CliCmRunDoesNotSupport2
    {"error: cm run does not support direct execution of --target=sv\n",
     "エラー: cm run は --target=sv の直接実行に未対応です\n"},
    // CliCompilationCompleteMs
    {"✓ compilation complete: {0} ({1}ms)\n",
     "✓ コンパイル完了: {0} ({1}ms)\n"},
    // CliCompiling
    {"compiling: {0}\n\n",
     "コンパイル中: {0}\n\n"},
    // CliConcatenatedSubmoduleS
    {"✓ concatenated {0} submodule(s)\n",
     "✓ サブモジュール {0} 個を連結\n"},
    // CliConfigFile
    {"config file: {0}\n\n",
     "設定ファイル: {0}\n\n"},
    // CliDeclarations
    {"declarations: {0}\n\n",
     "宣言数: {0}\n\n"},
    // CliDefinedSymbols
    {"defined symbols: ",
     "定義済みシンボル: "},
    // CliException
    {"{0}: exception: {1}\n",
     "{0}: 例外: {1}\n"},
    // CliFailedFileS
    {"failed: {0} file(s)\n",
     "失敗: {0} ファイル\n"},
    // CliFile
    {"  file: {0}\n",
     "  ファイル: {0}\n"},
    // CliFiles
    {"files: {0}/{1}\n",
     "ファイル数: {0}/{1}\n"},
    // CliFilesFixed
    {"files: {0}/{1} fixed\n",
     "ファイル数: {0}/{1} 修正\n"},
    // CliFilesToCheckFileS
    {"files to check: {0} file(s)\n",
     "チェック対象: {0} ファイル\n"},
    // CliFormatCheckComplete
    {"\n=== Format check complete ===\n",
     "\n=== フォーマットチェック完了 ===\n"},
    // CliFormatComplete
    {"\n=== Format complete ===\n",
     "\n=== フォーマット完了 ===\n"},
    // CliFormattedPlaceS
    {"formatted: {0} place(s)\n",
     "整形箇所: {0} 箇所\n"},
    // CliFormattingFileS
    {"formatting: {0} file(s)\n",
     "フォーマット対象: {0} ファイル\n"},
    // CliFrontendTotalMs
    {"  frontend total: {0}ms ({1}%)\n",
     "  フロントエンド合計: {0}ms ({1}%)\n"},
    // CliFunctionS
    {"    {0}: {1} function(s)\n",
     "    {0}: {1} 関数\n"},
    // CliFunrollLoopsCountMustBe
    {"--funroll-loops count must be in the range 1-1024",
     "--funroll-loops の展開回数は1-1024の範囲で指定してください"},
    // CliGenerateJsWithCmCompile
    {"hint: generate .js with 'cm compile --target=js' and run it with any JS runtime\n",
     "ヒント: cm compile --target=js で生成した .js を任意のJS実行系で実行してください\n"},
    // CliHirDeclarations
    {"HIR declarations: {0}\n\n",
     "HIR宣言数: {0}\n\n"},
    // CliHirMirLoweringMs
    {"  HIR + MIR lowering: {0}ms\n",
     "  HIR+MIR変換: {0}ms\n"},
    // CliImportedModules
    {"imported modules:\n",
     "インポートされたモジュール:\n"},
    // CliInternalError
    {"internal error ({0}): {1}\n",
     "内部エラー（{0}）: {1}\n"},
    // CliInvalidCommandForm
    {"error: invalid command form\n",
     "エラー: 不正なコマンド形式です\n"},
    // CliInvalidFunrollLoopsValue
    {"invalid --funroll-loops value: {0}",
     "無効な--funroll-loopsの値: {0}"},
    // CliInvalidLangValueEnOr
    {"invalid --lang value (en or ja): {0}",
     "無効な--langの値（en または ja）: {0}"},
    // CliInvalidLanguageInCmconfigYml
    {"warning: invalid language in .cmconfig.yml (expected en or ja): {0}\n",
     "警告: .cmconfig.yml の言語設定が不正です（en または ja）: {0}\n"},
    // CliInvalidMaximumOutputSize
    {"invalid maximum output size: {0}",
     "無効な最大出力サイズ: {0}"},
    // CliIverilogCompilationFailed
    {"error: iverilog compilation failed\n",
     "エラー: iverilog コンパイルに失敗しました\n"},
    // CliIverilogVvpNotFoundRequired
    {"error: iverilog / vvp not found (required to run SV tests)\n",
     "エラー: iverilog / vvp が見つかりません（SVテストの実行に必要）\n"},
    // CliJavascriptCodeGenerationComplete
    {"✓ JavaScript code generation complete: {0}\n",
     "✓ JavaScript コード生成完了: {0}\n"},
    // CliJavascriptCodeGenerationError
    {"JavaScript code generation error: {0}\n",
     "JavaScript コード生成エラー: {0}\n"},
    // CliJitCompilerIsDisabledAn
    {"error: JIT compiler is disabled; an LLVM-enabled build is required\n",
     "エラー: JITコンパイラが無効です。LLVM対応ビルドが必要です。\n"},
    // CliJitExecutionComplete
    {"✓ JIT execution complete\n",
     "✓ JIT実行完了\n"},
    // CliJitExecutionError
    {"JIT execution error ({0}): {1}\n",
     "JIT実行エラー ({0}): {1}\n"},
    // CliJitExecutionError2
    {"JIT execution error: {0}\n",
     "JIT実行エラー: {0}\n"},
    // CliLlvmCodeGenerationError
    {"LLVM code generation error: {0}\n",
     "LLVM コード生成エラー: {0}\n"},
    // CliLlvmIrAfterOptimization
    {"=== LLVM IR (after optimization) ===\n",
     "=== LLVM IR (最適化後) ===\n"},
    // CliMacosBrewInstallIcarusVerilog
    {"hint: macOS: brew install icarus-verilog / Ubuntu: sudo apt-get install iverilog\n",
     "ヒント: macOS: brew install icarus-verilog / Ubuntu: sudo apt-get install iverilog\n"},
    // CliMaximumOutputSizeMustBe
    {"maximum output size must be in the range 1-1024 GB",
     "最大出力サイズは1-1024GBの範囲で指定してください"},
    // CliMirAfterOptimization
    {"=== MIR (after optimization) ===\n",
     "=== MIR (最適化後) ===\n"},
    // CliMirBeforeOptimization
    {"=== MIR (before optimization) ===\n",
     "=== MIR (最適化前) ===\n"},
    // CliMirFunctions
    {"MIR functions: {0}\n\n{1}",
     "MIR関数数: {0}\n\n{1}"},
    // CliMirOptimizationMs
    {"  MIR optimization: {0}ms\n",
     "  MIR最適化: {0}ms\n"},
    // CliModulesDetected
    {"  modules: {0} detected",
     "  モジュール: {0} 検出"},
    // CliMsg
    {"error: {0}: {1}\n",
     "エラー: {0}: {1}\n"},
    // CliMultipleInputFilesAreNot
    {"multiple input files are not allowed",
     "複数の入力ファイルは指定できません"},
    // CliNeedsFormattingFileS
    {"needs formatting: {0}/{1} file(s)\n",
     "要整形: {0}/{1} ファイル\n"},
    // CliNeedsFormattingPlaceS
    {"{0}: needs formatting ({1} place(s))\n",
     "{0}: 要整形（{1} 箇所）\n"},
    // CliNoCmFilesFoundTo
    {"error: no .cm files found to check\n",
     "エラー: チェック対象の.cmファイルが見つかりません\n"},
    // CliNoCmFilesFoundTo2
    {"error: no .cm files found to format\n",
     "エラー: フォーマット対象の.cmファイルが見つかりません\n"},
    // CliNoCommandSpecified
    {"error: no command specified\n",
     "エラー: コマンドが指定されていません\n"},
    // CliNoInputFileOrDirectory
    {"error: no input file or directory specified\n",
     "エラー: 入力ファイルまたはディレクトリが指定されていません\n"},
    // CliNoInputFileSpecified
    {"error: no input file specified\n",
     "エラー: 入力ファイルが指定されていません\n"},
    // CliNoTestFunctionsFound
    {"error: no #[test] functions found: {0}\n",
     "エラー: #[test] 関数が見つかりません: {0}\n"},
    // CliNodeNotFoundRequiredTo
    {"error: node not found (required to run --target=js)\n",
     "エラー: node が見つかりません（--target=js の実行に必要です）\n"},
    // CliNoteForWhileStatementsAre
    {"  Note: for/while statements are converted to HirLoop\n",
     "  Note: for/while文がHirLoopに変換されています\n"},
    // CliOptimizationComplete
    {"optimization complete\n\n",
     "最適化完了\n\n"},
    // CliOptimizationLevelMustBeIn
    {"optimization level must be in the range 0-3",
     "最適化レベルは0-3の範囲で指定してください"},
    // CliParseTypeCheckMs
    {"  parse + type check: {0}ms\n",
     "  パース+型チェック: {0}ms\n"},
    // CliPathDoesNotExist
    {"error: path does not exist: {0}\n",
     "エラー: パスが存在しません: {0}\n"},
    // CliPlaceSFormatted
    {"{0}: {1} place(s) formatted\n",
     "{0}: {1} 箇所の整形\n"},
    // CliPreprocessMs
    {"  preprocess: {0}ms\n",
     "  プリプロセス: {0}ms\n"},
    // CliPreprocessorError
    {"{0}: preprocessor error: {1}\n",
     "{0}: プリプロセッサエラー: {1}\n"},
    // CliPreprocessorError2
    {"preprocessor error: {0}\n",
     "プリプロセッサエラー: {0}\n"},
    // CliProgramExitCode
    {"program exit code: {0}\n",
     "プログラム終了コード: {0}\n"},
    // CliRebuildWithDcmUseLlvm
    {"rebuild with -DCM_USE_LLVM=ON in CMake\n",
     "CMakeで -DCM_USE_LLVM=ON を指定してビルドしてください。\n"},
    // CliRunCmCompileEmitLlvm
    {"hint: run 'cm compile --emit-llvm --target=wasm -o out.wasm' and execute it with wasmtime out.wasm etc.\n",
     "hint: run 'cm compile --emit-llvm --target=wasm -o out.wasm' and execute it with wasmtime out.wasm etc.\n"},
    // CliRunCmHelpForUsage
    {"run 'cm help' for usage\n",
     "'cm help' でヘルプを表示\n"},
    // CliRunning
    {"running: {0}\n\n",
     "実行中: {0}\n\n"},
    // CliRunning2
    {"running: {0}\n",
     "実行中: {0}\n"},
    // CliRunningNode
    {"running: node {0}\n",
     "実行中: node {0}\n"},
    // CliS
    {"error(s)",
     "エラー"},
    // CliS2
    {"warning(s)",
     "警告"},
    // CliSWarnings
    {"errors: {0}, warnings: {1}\n",
     "エラー: {0}, 警告: {1}\n"},
    // CliSanitizeInCmconfigYmlIgnored
    {"warning: invalid compile.sanitize in .cmconfig.yml (ignored): {0}",
     "警告: .cmconfig.yml の compile.sanitize が不正なため無視します: {0}"},
    // CliSanitizeMemoryLinuxOnly
    {"error: sanitizer 'memory' is only supported on Linux (no MemorySanitizer runtime for this OS)\n",
     "エラー: サニタイザ 'memory' はLinux専用です（このOS向けのMemorySanitizerランタイムが存在しません）\n"},
    // CliSanitizeNotSupportedOnTarget
    {"error: sanitizer '{0}' is not supported on target '{1}'\n",
     "エラー: サニタイザ '{0}' はターゲット '{1}' では使用できません\n"},
    // CliSanitizeUnknownValue
    {"error: unknown sanitizer '{0}'\n",
     "エラー: 不明なサニタイザ '{0}'\n"},
    // CliSanitizeValidValues
    {"valid sanitizers: address/thread (native), memory (native Linux), bounds (native/wasm/jit), undefined (native/wasm/jit/js)\n",
     "有効なサニタイザ: address/thread（native）, memory（native Linux）, bounds（native/wasm/jit）, undefined（native/wasm/jit/js）\n"},
    // CliSpecifyTheJsTargetWith
    {"hint: specify the JS target with --target=js\n",
     "ヒント: --target=js オプションでJSターゲットを指定してください\n"},
    // CliStepIsOnlyAvailableIn
    {"error: step() is only available in //! platform: sv tests (test function: {0}）\n",
     "エラー: step() は //! platform: sv のテストでのみ使用できます（テスト関数: {0}）\n"},
    // CliSvHierarchyError
    {"SV hierarchy error: {0}\n",
     "sv階層化エラー: {0}\n"},
    // CliSvHierarchySubmoduleSDetected
    {"SV hierarchy: {0} submodule(s) detected\n",
     "sv階層化: {0} 個のサブモジュールを検出\n"},
    // CliSvTestPassed
    {"✓ SV test passed\n",
     "✓ SVテスト成功\n"},
    // CliSvTestSimulationIsNot
    {"error: SV test simulation is not supported on Windows\n",
     "エラー: SVテストのシミュレーション実行はWindowsでは未対応です\n"},
    // CliSyntaxErrorsOccurred
    {"syntax errors occurred\n",
     "構文エラーが発生しました\n"},
    // CliSystemverilogCodeGenerationError
    {"SystemVerilog code generation error: {0}\n",
     "SystemVerilog コード生成エラー: {0}\n"},
    // CliSystemverilogGenerationCompleteMs
    {"✓ SystemVerilog generation complete: {0} ({1}ms)\n",
     "✓ SystemVerilog 生成完了: {0} ({1}ms)\n"},
    // CliTestExecutionForPlatformIs
    {"error: test execution for platform '{0}' is not supported (only sv or native/JIT)\n",
     "エラー: platform '{0}' のテスト実行は未対応です（sv または native/JIT のみ）\n"},
    // CliTestSPassed
    {"✓ {0} test(s) passed\n",
     "✓ {0} 件のテストが成功\n"},
    // CliTestbenchHasNotBeenGenerated
    {"error: testbench has not been generated: {0}\n",
     "エラー: テストベンチが生成されていません: {0}\n"},
    // CliTheLlvmBackendIsNot
    {"error: the LLVM backend is not enabled\n",
     "エラー: LLVM バックエンドが有効になっていません。\n"},
    // CliTheOOptionRequiresAn
    {"the -o option requires an output file name",
     "-o オプションには出力ファイル名が必要です"},
    // CliThisFileTargetsPlatformCurrent
    {"warning: this file targets platform '{0}' (current: {1}）\n",
     "警告: このファイルはプラットフォーム '{0}' 向けです（現在: {1}）\n"},
    // CliToRunAFileUse
    {"to run a file, use: cm run {0}\n\n",
     "ファイルを実行するには次を使用してください: cm run {0}\n\n"},
    // CliTokens
    {"tokens: {0}\n\n",
     "トークン数: {0}\n\n"},
    // CliTypeCheckOk
    {"type check: OK\n\n",
     "型チェック: OK\n\n"},
    // CliUnknownCommandRunCmHelp
    {"unknown command: {0}\nrun 'cm help' for usage",
     "不明なコマンド: {0}\n'cm help' でヘルプを表示"},
    // CliUnknownOptionRunCmHelp
    {"unknown option: {0}\nrun 'cm help' for usage",
     "不明なオプション: {0}\n'cm help' でヘルプを表示"},
    // CliUnknownTarget
    {"error: unknown target '{0}'\n",
     "エラー: 不明なターゲット '{0}'\n"},
    // CliUseCmTestPlatformSv
    {"hint: use 'cm test' (//! platform: sv) for simulation\n",
     "ヒント: シミュレーション実行は cm test（//! platform: sv）を使用してください\n"},
    // CliUseTargetToSpecifyThe
    {"hint: use --target={0} to specify the target platform\n\n",
     "ヒント: --target={0} オプションで対象プラットフォームを指定してください\n\n"},
    // CliValidTargetsNativeWasmJs
    {"valid targets: native, wasm, js, ts, web, bm, bm-x86, baremetal-arm, baremetal-x86, uefi\n",
     "valid targets: native, wasm, js, ts, web, bm, bm-x86, baremetal-arm, baremetal-x86, uefi\n"},
    // CliWarningIsNotAvailableIn
    {"{0}:{1}: warning: '{2}' is not available in bare-metal environments [B001]\n",
     "{0}:{1}: warning: ベアメタル環境では '{2}' は使用できません [B001]\n"},
    // ===== codegen =====
    // CodegenCodeGenerationTimeExceededThe
    {"code generation time exceeded the limit",
     "コード生成時間が制限を超過しました"},
    // CodegenCodegenError
    {"[CODEGEN] error: {0}\n",
     "[CODEGEN] エラー: {0}\n"},
    // CodegenCodegenSectionWasNotCommitted
    {"[CODEGEN] section '{0}' was not committed\n",
     "[CODEGEN] セクション '{0}' はコミットされませんでした\n"},
    // CodegenCodegenWarningGeneratedCodeExceeds
    {"[CODEGEN] warning: generated code exceeds {0} MB\n",
     "[CODEGEN] 警告: 生成コードが{0}MBに達しています\n"},
    // CodegenGeneratedCodeLineCountExceeded
    {"generated code line count exceeded the limit",
     "生成コード行数が制限を超過しました"},
    // CodegenGeneratedCodeSizeExceededThe
    {"generated code size exceeded the limit",
     "生成コードサイズが制限を超過しました"},
    // CodegenRequiredBlockCannotBeAdded
    {"required block '{0}' cannot be added (size exceeded)",
     "必須ブロック '{0}' を追加できません（サイズ超過）"},
    // CodegenRequiredBlockGenerationFailed
    {"required block '{0}' generation failed",
     "必須ブロック '{0}' の生成に失敗"},
    // ===== diag =====
    // DiagE001
    {"unexpected token '{0}'",
     "予期しないトークン '{0}'"},
    // DiagE002
    {"a semicolon is required after the statement",
     "文の後にセミコロンが必要です"},
    // DiagE003
    {"unmatched closing brace: '{0}'",
     "対応する閉じ括弧がありません: '{0}'"},
    // DiagE100
    {"type mismatch: '{0}' and '{1}'",
     "型が一致しません: '{0}' と '{1}'"},
    // DiagE101
    {"variable '{0}' is not defined",
     "変数 '{0}' は定義されていません"},
    // DiagE102
    {"function '{0}' is not defined",
     "関数 '{0}' は定義されていません"},
    // DiagE103
    {"type '{0}' is not defined",
     "型 '{0}' は定義されていません"},
    // DiagE200
    {"variable '{0}' is used after move",
     "move後の変数 '{0}' を使用しています"},
    // DiagE201
    {"cannot modify '{0}' while it is borrowed",
     "借用中の '{0}' を変更できません"},
    // DiagE300
    {"null pointer dereference",
     "nullポインタの参照解除です"},
    // DiagE301
    {"invalid pointer arithmetic: '{0}'",
     "無効なポインタ演算: '{0}'"},
    // DiagE302
    {"pointer type mismatch: '{0}' and '{1}'",
     "ポインタ型が一致しません: '{0}' と '{1}'"},
    // DiagE303
    {"cannot modify through a const pointer",
     "constポインタ経由で変更できません"},
    // DiagE304
    {"use '->' for field access on pointer types",
     "ポインタ型に対するフィールドアクセスには '->' を使用してください"},
    // DiagE400
    {"type parameter count mismatch: '{0}' requires {1} type parameter(s)",
     "型パラメータの数が一致しません: '{0}' は {1} 個の型パラメータを必要とします"},
    // DiagE401
    {"type constraint not satisfied: '{0}' does not implement '{1}'",
     "型制約を満たしていません: '{0}' は '{1}' を実装していません"},
    // DiagE402
    {"recursive type instantiation is not supported: '{0}'",
     "再帰的な型のインスタンス化はサポートされていません: '{0}'"},
    // DiagE403
    {"invalid type argument: '{0}' cannot be used for type parameter '{1}'",
     "無効な型引数: '{0}' は型パラメータ '{1}' に使用できません"},
    // DiagE404
    {"failed to instantiate generic type '{0}'",
     "ジェネリック型 '{0}' のインスタンス化に失敗しました"},
    // DiagE500
    {"match expression is not exhaustive: '{0}' is not covered",
     "match式が網羅的ではありません: '{0}' がカバーされていません"},
    // DiagE501
    {"duplicate match arm: '{0}'",
     "重複するmatchアーム: '{0}'"},
    // DiagE502
    {"'{0}' is not a variant of enum '{1}'",
     "'{0}' は enum '{1}' のバリアントではありません"},
    // DiagE503
    {"match guards must be of type bool",
     "matchガードはbool型である必要があります"},
    // DiagE504
    {"this match arm is unreachable",
     "このmatchアームには到達できません"},
    // DiagE600
    {"invalid literal: '{0}'",
     "無効なリテラル: '{0}'"},
    // DiagE601
    {"literal '{0}' is out of range for type '{1}'",
     "リテラル '{0}' は型 '{1}' の範囲を超えています"},
    // DiagE602
    {"a constant expression is required",
     "定数式が必要です"},
    // DiagL001
    {"name '{0}' does not follow the {1} naming convention",
     "名前 '{0}' は {1} 命名規則に従っていません"},
    // DiagL002
    {"variable '{0}' can be const",
     "変数 '{0}' は const にできます"},
    // DiagL003
    {"line is too long ({0} > {1} characters)",
     "行が長すぎます（{0} > {1}文字）"},
    // DiagL004
    {"trailing whitespace at end of line",
     "行末に不要な空白があります"},
    // DiagL005
    {"missing newline at end of file",
     "ファイル末尾に改行がありません"},
    // DiagL006
    {"inconsistent indentation",
     "インデントが一貫していません"},
    // DiagL007
    {"inconsistent brace style (K&R style recommended)",
     "波括弧のスタイルが一貫していません（K&Rスタイル推奨）"},
    // DiagL100
    {"an unnecessary copy of '{0}' can be avoided",
     "'{0}' の不要なコピーを避けられます"},
    // DiagL101
    {"the loop can be optimized with for-in",
     "ループを for-in で最適化できます"},
    // DiagL102
    {"loop-invariant computation '{0}' inside the loop",
     "ループ内で不変な計算 '{0}' があります"},
    // DiagL103
    {"consider using an iterator instead of an index-based loop",
     "インデックスベースのループよりイテレータを使用することを検討してください"},
    // DiagL200
    {"consider specifying type arguments explicitly",
     "型引数を明示的に指定することを検討してください"},
    // DiagL201
    {"type '{0}' can be written more simply as '{1}'",
     "型 '{0}' はより簡潔に '{1}' と書けます"},
    // DiagL202
    {"consider adding an interface constraint to type parameter '{0}'",
     "型パラメータ '{0}' にインターフェース制約を追加することを検討してください"},
    // DiagL300
    {"consider using a reference instead of a raw pointer",
     "生ポインタよりも参照を使用することを検討してください"},
    // DiagL301
    {"raw pointers in structs may cause ownership issues",
     "構造体内の生ポインタは所有権の問題を引き起こす可能性があります"},
    // DiagL302
    {"use '->' for access through pointers",
     "ポインタ経由のアクセスには '->' を使用してください"},
    // DiagL400
    {"consider using a match expression instead of multiple if-else",
     "複数のif-elseよりもmatch式を使用することを検討してください"},
    // DiagL401
    {"a single-arm match expression can be replaced with if-let",
     "単一アームのmatch式はif-letに置き換えられます"},
    // DiagL402
    {"consider using explicit patterns instead of a wildcard",
     "ワイルドカードの代わりに明示的なパターンを使用することを検討してください"},
    // DiagW001
    {"variable '{0}' is unused",
     "変数 '{0}' は使用されていません"},
    // DiagW002
    {"code after a return statement is unreachable",
     "return文の後のコードは到達不能です"},
    // DiagW003
    {"implicit conversion from '{0}' to '{1}'",
     "'{0}' から '{1}' への暗黙的な型変換"},
    // DiagW004
    {"import '{0}' is unused",
     "import '{0}' は使用されていません"},
    // DiagW005
    {"parameter '{0}' is unused",
     "パラメータ '{0}' は使用されていません"},
    // DiagW006
    {"variable '{0}' shadows a variable in an outer scope",
     "変数 '{0}' は外側のスコープの変数を隠蔽しています"},
    // DiagW100
    {"pointer '{0}' may be null",
     "ポインタ '{0}' がnullである可能性があります"},
    // DiagW101
    {"returning a raw pointer; ownership may become unclear",
     "生ポインタを返しています。所有権が不明確になる可能性があります"},
    // DiagW102
    {"returning a pointer to local variable '{0}'",
     "ローカル変数 '{0}' へのポインタを返しています"},
    // DiagW200
    {"type parameter '{0}' is unused",
     "型パラメータ '{0}' は使用されていません"},
    // DiagW201
    {"type annotation '{0}' can be inferred",
     "型注釈 '{0}' は推論可能です"},
    // DiagW202
    {"many instantiations of generic type '{0}' may increase binary size",
     "many instantiations of generic type '{0}' may increase binary size"},
    // DiagW300
    {"wildcard pattern '_' captures all cases",
     "ワイルドカードパターン '_' がすべてのケースを捕捉しています"},
    // DiagW301
    {"enum variant '{0}' is unused",
     "enumバリアント '{0}' は使用されていません"},
    // DiagW302
    {"the result of the match expression is unused",
     "match式の結果が使用されていません"},
    // DiagW400
    {"consider replacing magic number '{0}' with a named constant",
     "マジックナンバー '{0}' を定数に置き換えることを検討してください"},
    // DiagW401
    {"converting literal '{0}' to '{1}' may lose precision",
     "リテラル '{0}' を '{1}' に変換すると精度が失われる可能性があります"},
    // ===== fmt =====
    // FmtCannotReadFile
    {"error: cannot read file: {0}\n",
     "エラー: ファイルを読み込めません: {0}\n"},
    // FmtCannotWriteFile
    {"error: cannot write file: {0}\n",
     "エラー: ファイルに書き込めません: {0}\n"},
    // FmtFormattingFixEs
    {"✓ {0} formatting fix(es)\n",
     "✓ {0} 箇所のフォーマット修正\n"},
    // ===== js =====
    // JsCallocReallocVoidAreNot
    {"hint: calloc/realloc/void* are not available on the JS target. malloc/free are replaced by GC. \n",
     "ヒント: calloc/realloc/void* はJSターゲットでは利用できません。malloc/freeはGCベースで代替されます。\n"},
    // JsCastingToVoidIsNot
    {"casting to void* is not available on the JS target",
     "JSターゲットでは void* へのキャストは使用できません"},
    // JsFunction
    {" (function: {0})",
     " (関数: {0})"},
    // JsJs
    {"error[JS]: {0}",
     "エラー[JS]: {0}"},
    // JsOnTheJsTargetIs
    {"on the JS target, {0}() is not available. JavaScript has no heap memory management",
     "JSターゲットでは {0}() は使用できません。JavaScriptにはヒープメモリ管理機能がありません"},
    // JsPointerCastsFromVoidAre
    {"pointer casts from void* are not available on the JS target",
     "JSターゲットでは void* からのポインタキャストは使用できません"},
    // JsTheVoidTypeIsNot
    {"the void* type is not available on the JS target (variable: {0}）",
     "JSターゲットでは void* 型は使用できません（変数: {0}）"},
    // ===== lint =====
    // LintCasePascal
    {"PascalCase",
     "PascalCase"},
    // LintCasePascalOrUpper
    {"PascalCase or UPPER_SNAKE_CASE",
     "PascalCase または UPPER_SNAKE_CASE"},
    // LintCaseSnake
    {"snake_case",
     "snake_case"},
    // LintCaseSnakeOrUpper
    {"snake_case or UPPER_SNAKE_CASE",
     "snake_case または UPPER_SNAKE_CASE"},
    // LintCaseUpperSnake
    {"UPPER_SNAKE_CASE",
     "UPPER_SNAKE_CASE"},
    // LintLabelConstantName
    {"constant name",
     "定数名"},
    // LintLabelEnumName
    {"enum name",
     "enum名"},
    // LintLabelEnumVariantName
    {"enum variant name",
     "enumバリアント名"},
    // LintLabelFieldName
    {"field name",
     "フィールド名"},
    // LintLabelFunctionName
    {"function name",
     "関数名"},
    // LintLabelGlobalConstantName
    {"global constant name",
     "グローバル定数名"},
    // LintLabelGlobalVariableName
    {"global variable name",
     "グローバル変数名"},
    // LintLabelInterfaceName
    {"interface name",
     "インターフェース名"},
    // LintLabelMethodName
    {"method name",
     "メソッド名"},
    // LintLabelModuleName
    {"module name",
     "モジュール名"},
    // LintLabelParameterName
    {"parameter name",
     "パラメータ名"},
    // LintLabelStructName
    {"struct name",
     "構造体名"},
    // LintLabelTypeAliasName
    {"type alias name",
     "型エイリアス名"},
    // LintLabelTypeParameterName
    {"type parameter name",
     "型パラメータ名"},
    // LintLabelVariableName
    {"variable name",
     "変数名"},
    // LintNamingViolation
    {"{0} '{1}' does not follow the {2} naming convention [L001]",
     "{0} '{1}' は {2} 命名規則に従っていません [L001]"},
    // ===== module =====
    // ModuleModuleFailedToParse
    {"error: module '{0}' failed to parse\n",
     "エラー: モジュール '{0}' のパースに失敗しました\n"},
    // ModuleModuleNotFound
    {"error: module '{0}' not found\n",
     "エラー: モジュール '{0}' が見つかりません\n"},
    // ===== nostd =====
    // NostdCatFileIo
    {"file I/O",
     "ファイルI/O"},
    // NostdCatNetwork
    {"networking",
     "ネットワーク"},
    // NostdCatOsDependent
    {"OS-dependent functionality",
     "OS依存機能"},
    // NostdCatOsHeap
    {"OS heap memory management",
     "OSヒープメモリ管理"},
    // NostdCatOsStdout
    {"OS standard output",
     "OS標準出力"},
    // NostdCatProcess
    {"process control",
     "プロセス制御"},
    // NostdCatThread
    {"threading",
     "スレッド"},
    // NostdForbiddenCall
    {"error: function '{0}' uses '{1}'; {2} is not available in bare-metal environments",
     "エラー: 関数 '{0}' 内で '{1}' を使用しています。{2} はベアメタル環境では使用できません"},
    // ===== parse =====
    // ParseASwitchStatementRequiresCase
    {"a switch statement requires case or else",
     "switch文内にはcaseまたはelseが必要です"},
    // ParseDuplicateElseClause
    {"duplicate else clause",
     "重複するelse節"},
    // ParseEnumValueIsAlreadyUsed
    {"enum value {0} is already used",
     "enum値 {0} は既に使用されています"},
    // ParseEnumValuesRequireAnInteger
    {"enum values require an integer or character literal",
     "enum値には整数リテラルまたは文字リテラルが必要です"},
    // ParseIsRequiredAfterMatchMatch
    {"'{' is required after match; 'match' is a reserved word in Cm and cannot be used as a variable name",
     "'{' is required after match; 'match' is a reserved word in Cm and cannot be used as a variable name"},
    // ParseLiteralTypesRequireAString
    {"literal types require a string, integer, or floating-point literal",
     "リテラル型には文字列、整数、または浮動小数点リテラルが必要です"},
    // ParseParserStalledWhileParsingMatch
    {"parser stalled while parsing match patterns",
     "match式のパターン解析でパーサが停滞しました"},
    // ParseRecursionDepthExceededTheLimit
    {"recursion depth exceeded the limit (500)",
     "再帰深度が制限(500)を超えました"},
    // ParseTheMainFunctionCannotBe
    {"the main function cannot be exported",
     "main関数はエクスポートできません"},
    // ParseTooManyErrorsAbortingCompilation
    {"too many errors; aborting compilation",
     "エラーが多すぎるためコンパイルを中断します"},
    // ParseTypedefRequiresAValidType
    {"typedef requires a valid type",
     "typedefには有効な型が必要です"},
    // ===== sv =====
    // SvCannotOpenFile
    {"error: cannot open file '{0}'\n",
     "エラー: ファイル '{0}'\n"},
    // SvCannotWriteMemfile
    {"warning: cannot write memfile: {0}\n",
     "警告: memfileを書き出せません: {0}\n"},
    // SvFunction
    {"function",
     "関数"},
    // SvIdentifiersConflictWithSystemverilogReserved
    {"identifiers conflict with SystemVerilog reserved words: ",
     "SystemVerilogの予約語と衝突する識別子があります: "},
    // SvLines
    {"  lines: {0}\n",
     "  行数: {0}\n"},
    // SvNonSynthesizableTypesDetectedOn
    {"non-synthesizable types detected on the SV target",
     "SVターゲットで非合成型が検出されました"},
    // SvPinConstraintsGenerated
    {"✓ pin constraints generated: {0}\n",
     "✓ ピン制約生成: {0}\n"},
    // SvPortHasNoSvPin
    {"warning: port '{0}' has no #[sv::pin] attribute (it will not be included in the .cst)\n",
     "警告: ポート '{0}' に #[sv::pin] が指定されていません（.cstに含まれません）\n"},
    // SvProjectScriptGenerated
    {"✓ project script generated: {0}\n",
     "✓ プロジェクトスクリプト生成: {0}\n"},
    // SvRenameThem
    {"; rename them",
     "。別の名前に変更してください"},
    // SvSizeBytes
    {"  size: {0} bytes\n",
     "  サイズ: {0} bytes\n"},
    // SvSv004FloatingPointTypesAre
    {"error[SV004]: floating-point types are not supported on the SV target: {0} (use fixed-point arithmetic or a vendor IP via extern struct)\n",
     "error[SV004]: 浮動小数点型はSVターゲット非対応です: {0}（固定小数点で記述するか、ベンダーIPを extern struct で利用してください）\n"},
    // SvSv004FloatingPointTypesAre2
    {"error[SV004]: floating-point types are not supported on the SV target: {0}::{1}\n",
     "error[SV004]: 浮動小数点型はSVターゲット非対応です: {0}::{1}\n"},
    // SvSv006DynamicArraysSlicesAre
    {"error[SV006]: dynamic arrays (slices) are not supported on the SV target: {0}(use a fixed-size array T[N])\n",
     "error[SV006]: 動的配列（スライス）はSVターゲット非対応です: {0}（固定長配列 T[N] を使用してください）\n"},
    // SvSv006DynamicArraysSlicesAre2
    {"error[SV006]: dynamic arrays (slices) are not supported on the SV target: {0}::{1}\n",
     "error[SV006]: 動的配列（スライス）はSVターゲット非対応です: {0}::{1}\n"},
    // SvSv007InlineAssemblyAsmIs
    {"error[SV007]: inline assembly (__asm__) is not available on the SV target",
     "エラー[SV007]: インラインアセンブリ（__asm__）はSVターゲットで使用できません"},
    // SvSv007UnsupportedExpressionOnThe
    {"error[SV007]: unsupported expression on the SV target (MirRvalue::{0}); references, aggregate construction, and format conversion are not synthesizable",
     "エラー[SV007]: SVターゲットで非対応の式です（MirRvalue::{0}）。参照・集約構築・フォーマット変換は合成できません"},
    // SvSv007UnsupportedStatementOnThe
    {"error[SV007]: unsupported statement on the SV target (MirStatement kind={0}）",
     "エラー[SV007]: SVターゲットで非対応の文です（MirStatement kind={0}）"},
    // SvSvTargetFunctionAssignsState
    {"warning: SV target: function '{0}' assigns state variable '{1}' and reads it afterwards; assignments in posedge functions take effect next cycle (non-blocking), so this read sees the previous-cycle value\n",
     "警告: SVターゲット: 関数 '{0}' 内で代入した状態変数 '{1}' and reads it afterwards; assignments in posedge functions take effect next cycle (non-blocking), so this read sees the previous-cycle value\n"},
    // SvSystemverilogGenerationComplete
    {"✓ SystemVerilog generation complete: {0}\n",
     "✓ SystemVerilog 生成完了: {0}\n"},
    // SvVariable
    {"variable",
     "変数"},
    // ===== type =====
    // TypeBitSliceRangesMustBe
    {"bit-slice ranges must be integer literals (v0.16.0 limitation; e.g. x[7:4])",
     "ビットスライスの範囲は整数リテラルで指定してください（v0.16.0時点の制限。例: x[7:4]）"},
    // TypeBitSlicesOnAScalar
    {"bit slices on a scalar bit (width 1) must be [0:0]",
     "スカラーbit（幅1）へのビットスライスは [0:0] のみ有効です"},
    // TypeCanOnlyBeUsedInside
    {"'?' can only be used inside functions returning {0} (current function return type: {1})",
     "'?' は次を返す関数の中でのみ使用できます: {0}（現在の関数の戻り値型: {1})"},
    // TypeCanOnlyBeUsedOn
    {"'?' can only be used on Result/Option values (target type: {0})",
     "'?' はResult/Option型の値にのみ使用できます（対象の型: {0})"},
    // TypeExitMustBeUsedAs
    {"exit must be used as exit(exit_code)",
     "exit は exit(終了コード) の形式で使用します"},
    // TypeFunctionIsAlreadyDefinedWith
    {"function '{0}' is already defined with a different signature (free-function overloading is not supported; use a different name)",
     "関数 '{0}' is already defined with a different signature (free-function overloading is not supported; use a different name)"},
    // TypeInvalidBitSliceRangeHi
    {"invalid bit-slice range (hi >= lo >= 0, width <= 64)",
     "ビットスライス範囲が不正です（hi >= lo >= 0、幅は64以下）"},
    // TypeIsCanOnlyBeUsed
    {"'is' can only be used on union-typed values (left-hand type: {0})",
     "'is' はユニオン型の値にのみ使用できます（左辺の型: {0})"},
    // TypeLabelNone
    {"none",
     "なし"},
    // TypeLabelUnknown
    {"unknown",
     "不明"},
    // TypePartSelectWidthMustBe
    {"part-select width must be an integer literal from 1 to 64 (v0.16.0 limitation)",
     "パートセレクトの幅は1〜64の整数リテラルで指定してください（v0.16.0時点の制限）"},
    // TypePartSelectWidthOnA
    {"part-select width on a scalar bit (width 1) must be 1",
     "スカラーbit（幅1）へのパートセレクト幅は1のみ有効です"},
    // TypeStepMustBeUsedAs
    {"step must be used as step(cycle_count)",
     "step は step(クロック数) の形式で使用します"},
    // TypeTestFunctionCannotTakeParameters
    {"#[test] function '{0}' cannot take parameters",
     "#[test] function '{0}' は引数を取れません"},
    // TypeTestFunctionMustReturnVoid
    {"#[test] function '{0}' must return void",
     "#[test] function '{0}' の戻り値は void である必要があります"},
    // TypeTheArgumentToStepMust
    {"the argument to step must be an integer type (cycle count)",
     "step の引数は整数型（クロック数）である必要があります"},
    // TypeTheBaseOfAPart
    {"the base of a part-select must be an integer type",
     "パートセレクトの基点は整数型である必要があります"},
    // TypeTheExitCodeForExit
    {"the exit code for exit must be an integer type",
     "exit の終了コードは整数型である必要があります"},
    // TypeTheTargetTypeIsNot
    {"the target type '{0}' is not a variant of the union",
     "'is' の対象型 '{0}' はユニオンの変種に含まれていません"},
    // TypeTheUpperBitOfThe
    {"the upper bit of the bit slice exceeds the type width",
     "ビットスライスの上位ビットが型の幅を超えています"},
    // TypeTypePatternIsNotA
    {"type pattern '{0}' is not a variant of the union",
     "型パターン '{0}' はユニオンの変種に含まれていません"},
    // TypeTypePatternsCanOnlyBe
    {"type patterns can only be used in match on union types (target type: {0})",
     "型パターンはユニオン型のmatchでのみ使用できます（対象の型: {0})"},
    // TypeUnusedResultValueTheError
    {"unused Result value; the error is ignored. Handle it with match, is_ok(), unwrap(), etc., or assign it to a variable explicitly [must_use]",
     "unused Result value; the error is ignored. Handle it with match, is_ok(), unwrap(), etc., or assign it to a variable explicitly [must_use]"},
    // TypeCannotCastNumericToString
    {"cannot cast numeric type '{0}' to string with 'as'; use string interpolation or a to-string conversion instead",
     "数値型 '{0}' を 'as' で string にキャストすることはできません（文字列補間または文字列化関数を使用してください）"},
    // TypeIntegerLiteralNarrowingTruncates
    {"integer literal {0} does not fit in target type '{1}' and will be truncated",
     "整数リテラル {0} は変換先の型 '{1}' に収まらず切り捨てられます"},
    // TypeGenericArgumentCountMismatch
    {"generic type '{0}' expects {1} type argument(s) but {2} were provided",
     "ジェネリック型 '{0}' は型引数を {1} 個必要としますが {2} 個指定されています"},
    // TypeMangledSymbolCollision
    {"symbol '{0}' is defined more than once: {1} conflicts with {2} (both lower to the same linkage name)",
     "シンボル '{0}' が重複しています: {1} と {2} が同一のリンケージ名に縮退します"},
    // TypeGenericTypeRequiresArguments
    {"generic type '{0}' requires {1} type argument(s) (e.g. {0}<...>)",
     "ジェネリック型 '{0}' には型引数が {1} 個必要です（例: {0}<...>）"},
    // TypeUnknownTypeArgument
    {"unknown type '{0}' in type argument of '{1}'",
     "'{1}' の型引数に未定義の型 '{0}' が指定されています"},
    // TypeGenericFunctionArgumentCountMismatch
    {"generic function '{0}' expects {1} type argument(s) but {2} were provided",
     "ジェネリック関数 '{0}' は型引数を {1} 個必要としますが {2} 個指定されています"},
    // TypeGenericBoundMissing
    {"type parameter '{0}' uses operator '{1}' but no interface bound is declared (declare <{0}: {2}>)",
     "型パラメータ '{0}' は演算子 '{1}' を使用していますが、インターフェース境界が宣言されていません（<{0}: {2}> と宣言してください）"},
    // TypeUseAfterMove
    {"variable '{0}' used after move",
     "move後の変数 '{0}' を使用しています"},
    // TypeNotAllPathsReturn
    {"non-void function '{0}' has a path that falls off the end without returning a value",
     "非void関数 '{0}' に値を返さずに終了する経路があります"},
    // TypeAssignToConstAggregate
    {"assignment to a field or element of const value '{0}' (const is currently shallow for aggregates; this will become an error in a future version)",
     "const値 '{0}' のフィールド/要素へ代入しています（集約のconstは現在浅い扱いですが、将来のバージョンでエラーになります）"},
    // TypeAddrOfConst
    {"taking a mutable pointer to const value '{0}' (writes through the pointer bypass const; this will become an error in a future version)",
     "const値 '{0}' へのミュータブルなポインタを取得しています（ポインタ経由の書き込みはconstを迂回します。将来のバージョンでエラーになります）"},
    // ImportDuplicateSymbol
    {"symbol '{0}' is imported from both '{1}' and '{2}' and is ambiguous; use an alias (import ... as ...) or a qualified name",
     "シンボル '{0}' は '{1}' と '{2}' の両方からimportされ曖昧です。エイリアス（import ... as ...）か修飾名を使ってください"},
    // ImportNonExportedSymbol
    {"function '{0}' selected by import is not exported from '{1}' (add 'export' to the definition or an export list)",
     "importで指定された関数 '{0}' は '{1}' でexportされていません（定義にexportを付けるかexportリストへ追加してください）"},
    // MirSliceReceiverUnresolved
    {"could not resolve the slice receiver of a builtin call (the statement would be silently dropped)",
     "スライス組み込み呼び出しのレシーバの場所を解決できませんでした（この文は黙って欠落します）"},
    // MirErrorSymbol
    {"internal error: unresolved type artifact '{0}' reached MIR in function '{1}' (a type checker recovery leaked downstream)",
     "内部エラー: 未解決型の成果物 '{0}' が関数 '{1}' のMIRに到達しました（型検査のエラー回復が下流へ漏れています）"},
    // PsExpectedBindingVariableNamePattern
    {"Expected binding variable name in pattern",
     "パターン内に束縛変数名が必要です"},
    // PsExpectedMatchPattern
    {"Expected match pattern",
     "matchパターンが必要です"},
    // PsExpectedFieldNameStructLiteral
    {"Expected field name in struct literal (named initialization required)",
     "構造体リテラルにはフィールド名が必要です（名前付き初期化が必須です）"},
    // PsExpectedFieldNameStructLiteral2
    {"Expected ':' after field name '{0}' in struct literal",
     "構造体リテラルのフィールド名 '{0}' の後には ':' が必要です"},
    // PsEmptyParenthesesWithoutLambdaBody
    {"Empty parentheses without lambda body",
     "ラムダ本体のない空の括弧です"},
    // PsExpectedExpression
    {"Expected expression",
     "式が必要です"},
    // PsExpectedAttributeStart
    {"Expected attribute start '@' or '#'",
     "属性の開始には '@' または '#' が必要です"},
    // PsExpected
    {"Expected '/' after '..'",
     "'..' の後には '/' が必要です"},
    // PsExpected2
    {"Expected '/' after '.'",
     "'.' の後には '/' が必要です"},
    // PsExpectedFromExport
    {"Expected 'from' after 'export *'",
     "'export *' の後には 'from' が必要です"},
    // PsExpectedGlobalVariableInitializer
    {"Expected '=' for global variable initializer",
     "グローバル変数の初期化子には '=' が必要です"},
    // PsDeriveNotSupportedEnumsYet
    {"#[derive] is not supported on enums yet",
     "#[derive] はenumではまだサポートされていません"},
    // PsParserStuckNoProgressMade
    {"Parser stuck - no progress made",
     "パーサが停止しました - 解析が進行していません"},
    // PsAttributesNotSupportedExportLists
    {"Attributes are not supported on export lists",
     "属性はexportリストではサポートされていません"},
    // PsDirectiveNotYetImplemented
    {"Directive '#{0}' is not yet implemented",
     "ディレクティブ '#{0}' は未実装です"},
    // PsUnknownInvalidDirective
    {"Unknown or invalid directive after '#'",
     "'#' の後のディレクティブが不明または不正です"},
    // PsDefaultArgumentRequiredParameterDefault
    {"Default argument required after parameter with default value",
     "デフォルト値付きパラメータの後のパラメータにはデフォルト引数が必要です"},
    // PsDeriveRequiresAtLeastOne
    {"#[derive] requires at least one interface name",
     "#[derive] には少なくとも1つのインターフェース名が必要です"},
    // PsOnlyOneDefaultMemberAllowed
    {"Only one default member allowed per struct",
     "defaultメンバは構造体ごとに1つだけ許可されます"},
    // PsExpectedOperatorSymbolOperator
    {"Expected operator symbol after 'operator'",
     "'operator' の後には演算子記号が必要です"},
    // PsOnlyOneDestructorAllowedPer
    {"Only one destructor allowed per impl block",
     "デストラクタはimplブロックごとに1つだけ許可されます"},
    // PsExpectedSelf
    {"Expected 'self' after '~'",
     "'~' の後には 'self' が必要です"},
    // PsParserStuckBlockNoProgress
    {"Parser stuck in block - no progress made",
     "パーサがブロック内で停止しました - 解析が進行していません"},
    // PsParserStuckSynchronizationTooMany
    {"Parser stuck in synchronization - too many tokens skipped",
     "パーサが同期処理で停止しました - スキップしたトークンが多すぎます"},
    // PsExpectedIdentifier
    {"Expected identifier after '::'",
     "'::' の後には識別子が必要です"},
    // PsExpectedType
    {"Expected type",
     "型が必要です"},
    // PsSvMsbLsbBit3
    {"型の幅・要素数は個数で指定します（SVの範囲表記 [msb:lsb] は型宣言では使えません）。例: bit[3:0] ではなく bit[4] と書きます（生成されるSVでは logic [3:0] になります）",
     "型の幅・要素数は個数で指定します（SVの範囲表記 [msb:lsb] は型宣言では使えません）。例: bit[3:0] ではなく bit[4] と書きます（生成されるSVでは logic [3:0] になります）"},
    // PsExpected3
    {"Expected '>'",
     "'>' が必要です"},
    // PsExpectedIdentifierReservedWord
    {"Expected identifier, got reserved word '{0}'",
     "識別子が必要ですが、予約語 '{0}' が指定されました"},
    // PsExpectedIdentifier2
    {"Expected identifier, got '{0}'",
     "識別子が必要ですが、'{0}' が指定されました"},
    // TcUnknownInterfaceDerive
    {"Unknown interface '{0}' in 'with' / #[derive]",
     "'with' / #[derive] に不明なインターフェース '{0}' が指定されています"},
    // TcInterfaceNotDerivableImpl
    {"Interface '{0}' is not derivable; use 'impl {1} for {2}' instead",
     "インターフェース '{0}' はderive不可です。代わりに 'impl {1} for {2}' を使用してください"},
    // TcCannotDeriveStructField
    {"Cannot derive '{0}' for struct '{1}': field '{2}' ({3})",
     "構造体 '{1}' に '{0}' をderiveできません: フィールド '{2}' ({3})"},
    // TcRequiresExactly1ArgumentAssembly
    {"{0} requires exactly 1 argument (assembly code)",
     "{0} にはちょうど1つの引数（アセンブリコード）が必要です"},
    // TcArgumentMustStringLiteral
    {"{0} argument must be a string literal",
     "{0} の引数は文字列リテラルでなければなりません"},
    // TcRequiresAtLeast1Argument
    {"'{0}' requires at least 1 argument",
     "'{0}' には少なくとも1つの引数が必要です"},
    // TcTakesOnly1Argument
    {"'{0}' takes only 1 argument, got {1}",
     "'{0}' の引数は1つだけですが、{1}個指定されました"},
    // TcMethodTypeNotStaticMethod
    {"Method '{0}' of type '{1}' is not a static method",
     "型 '{1}' のメソッド '{0}' はstaticメソッドではありません"},
    // TcStaticMethodExpectsArguments
    {"Static method '{0}' expects {1} arguments, got {2}",
     "staticメソッド '{0}' は{1}個の引数を期待しますが、{2}個指定されました"},
    // TcArgumentTypeMismatchCallExpected
    {"Argument type mismatch in call to '{0}': expected {1}, got {2}",
     "'{0}' の呼び出しで引数型が一致しません: 期待 {1}、実際 {2}"},
    // TcArgumentTypeMismatchEnumConstructor
    {"Argument type mismatch in enum constructor '{0}': expected {1}, got {2}",
     "enumコンストラクタ '{0}' で引数型が一致しません: 期待 {1}、実際 {2}"},
    // TcArgumentTypeMismatchExpected
    {"Argument type mismatch in '{0}': expected {1}, got {2}",
     "'{0}' で引数型が一致しません: 期待 {1}、実際 {2}"},
    // TcNotFunction
    {"'{0}' is not a function",
     "'{0}' は関数ではありません"},
    // TcFunctionPointerExpectsArguments
    {"Function pointer '{0}' expects {1} arguments, got {2}",
     "関数ポインタ '{0}' は{1}個の引数を期待しますが、{2}個指定されました"},
    // TcArgumentTypeMismatchCallFunction
    {"Argument type mismatch in call to function pointer '{0}': expected {1}, got {2}",
     "関数ポインタ '{0}' の呼び出しで引数型が一致しません: 期待 {1}、実際 {2}"},
    // TcVariadicFunctionRequiresAtLeast
    {"Variadic function '{0}' requires at least {1} arguments, got {2}",
     "可変長引数関数 '{0}' には少なくとも{1}個の引数が必要ですが、{2}個指定されました"},
    // TcCannotPassCapturingClosureFunction
    {"Cannot pass a capturing closure to function parameter {0} of '{1}': closures lose their captured environment when passed as values (bind to a local variable and call it directly, or use builtin higher-order methods like map/filter)",
     "キャプチャ付きクロージャを '{1}' の関数パラメータ {0} へ渡せません: クロージャは値として渡すとキャプチャ環境を失います（ローカル変数へ束縛して直接呼び出すか、map/filter等の組み込み高階メソッドを使用してください）"},
    // TcFunctionExpectsArguments
    {"Function '{0}' expects {1} arguments, got {2}",
     "関数 '{0}' は{1}個の引数を期待しますが、{2}個指定されました"},
    // TcFunctionExpectsArguments2
    {"Function '{0}' expects {1} to {2} arguments, got {3}",
     "関数 '{0}' は{1}〜{2}個の引数を期待しますが、{3}個指定されました"},
    // TcPointerTypeDoesNotSupport
    {"Pointer type does not support method calls. Use (*ptr).method() instead.",
     "ポインタ型はメソッド呼び出しをサポートしません。代わりに (*ptr).method() を使用してください。"},
    // TcCannotCallPrivateMethodFrom
    {"Cannot call private method '{0}' from outside impl block of '{1}'",
     "privateメソッド '{0}' は '{1}' のimplブロック外から呼び出せません"},
    // TcArgumentTypeMismatchMethodCall
    {"Argument type mismatch in method call '{0}': expected {1}, got {2}",
     "メソッド呼び出し '{0}' で引数型が一致しません: 期待 {1}、実際 {2}"},
    // TcFunctionFieldExpectsArguments
    {"Function field '{0}' expects {1} arguments, got {2}",
     "関数フィールド '{0}' は{1}個の引数を期待しますが、{2}個指定されました"},
    // TcArgumentTypeMismatchFunctionField
    {"Argument type mismatch in function field call '{0}': expected {1}, got {2}",
     "関数フィールド呼び出し '{0}' で引数型が一致しません: 期待 {1}、実際 {2}"},
    // TcUnknownMethodType
    {"Unknown method '{0}' for type '{1}'",
     "型 '{1}' に不明なメソッド '{0}' が指定されました"},
    // TcCannotAccessPrivateFieldFrom
    {"Cannot access private field '{0}' from outside impl block of '{1}'",
     "privateフィールド '{0}' は '{1}' のimplブロック外からアクセスできません"},
    // TcUnknownFieldStruct
    {"Unknown field '{0}' in struct '{1}'",
     "構造体 '{1}' に不明なフィールド '{0}' が指定されました"},
    // TcUnknownStructType
    {"Unknown struct type '{0}'",
     "不明な構造体型 '{0}' です"},
    // TcCannotPointerTypeFieldAccess
    {"Cannot use '.' on pointer type '{0}'. Use '->' for field access through pointers.",
     "ポインタ型 '{0}' に '.' は使用できません。ポインタ経由のフィールドアクセスには '->' を使用してください。"},
    // TcFieldAccessNonStructType
    {"Field access on non-struct type '{0}'",
     "構造体でない型 '{0}' へのフィールドアクセスです"},
    // TcArrayTakesNoArguments
    {"Array {0}() takes no arguments",
     "配列の {0}() は引数を取りません"},
    // TcSliceTakesNoArguments
    {"Slice {0}() takes no arguments",
     "スライスの {0}() は引数を取りません"},
    // TcSlicePushTakes1Argument
    {"Slice push() takes 1 argument",
     "スライスの push() は1つの引数を取ります"},
    // TcCannotPushCapturingClosureInto
    {"Cannot push a capturing closure into a function-type slice: closures lose their captured environment when stored as values (bind to a local variable and call it directly)",
     "キャプチャ付きクロージャを関数型スライスへpushできません: クロージャは値として格納するとキャプチャ環境を失います（ローカル変数へ束縛して直接呼び出してください）"},
    // TcSlicePopTakesNoArguments
    {"Slice pop() takes no arguments",
     "スライスの pop() は引数を取りません"},
    // TcSliceTakes1IndexArgument
    {"Slice {0}() takes 1 index argument",
     "スライスの {0}() は1つの添字引数を取ります"},
    // TcSliceClearTakesNoArguments
    {"Slice clear() takes no arguments",
     "スライスの clear() は引数を取りません"},
    // TcArrayIndexofTakes1Argument
    {"Array indexOf() takes 1 argument",
     "配列の indexOf() は1つの引数を取ります"},
    // TcArraySearchUnsupportedElem
    {"Array {0}() does not support element type '{1}'",
     "配列の {0}() は要素型 '{1}' に対応していません"},
    // TcArrayTakes1Argument
    {"Array {0}() takes 1 argument",
     "配列の {0}() は1つの引数を取ります"},
    // TcArraySomeTakes1Predicate
    {"Array some() takes 1 predicate function",
     "配列の some() は1つの述語関数を取ります"},
    // TcArrayEveryTakes1Predicate
    {"Array every() takes 1 predicate function",
     "配列の every() は1つの述語関数を取ります"},
    // TcArrayFindindexTakes1Predicate
    {"Array findIndex() takes 1 predicate function",
     "配列の findIndex() は1つの述語関数を取ります"},
    // TcArrayReduceTakes12
    {"Array reduce() takes 1-2 arguments (callback, [initial])",
     "配列の reduce() は1〜2個の引数（コールバック、[初期値]）を取ります"},
    // TcArrayForeachTakes1Callback
    {"Array forEach() takes 1 callback function",
     "配列の forEach() は1つのコールバック関数を取ります"},
    // TcArrayMapTakes1Callback
    {"Array map() takes 1 callback function",
     "配列の map() は1つのコールバック関数を取ります"},
    // TcArrayFilterTakes1Predicate
    {"Array filter() takes 1 predicate function",
     "配列の filter() は1つの述語関数を取ります"},
    // TcArrayReverseTakesNoArguments
    {"Array reverse() takes no arguments",
     "配列の reverse() は引数を取りません"},
    // TcArraySortTakesNoArguments
    {"Array sort() takes no arguments (use sortBy for custom comparator)",
     "配列の sort() は引数を取りません（カスタム比較子にはsortByを使用してください）"},
    // TcArraySortbyTakes1Comparator
    {"Array sortBy() takes 1 comparator function",
     "配列の sortBy() は1つの比較関数を取ります"},
    // TcArrayGetTakes1Index
    {"Array get() takes 1 index argument",
     "配列の get() は1つの添字引数を取ります"},
    // TcArrayGetIndexMustInteger
    {"Array get() index must be an integer",
     "配列の get() の添字は整数でなければなりません"},
    // TcArrayFirstTakesNoArguments
    {"Array first() takes no arguments",
     "配列の first() は引数を取りません"},
    // TcArrayLastTakesNoArguments
    {"Array last() takes no arguments",
     "配列の last() は引数を取りません"},
    // TcArrayFindTakes1Predicate
    {"Array find() takes 1 predicate function",
     "配列の find() は1つの述語関数を取ります"},
    // TcArrayDimTakesNoArguments
    {"Array dim() takes no arguments",
     "配列の dim() は引数を取りません"},
    // TcUnknownArrayMethod
    {"Unknown array method '{0}'",
     "不明な配列メソッド '{0}' です"},
    // TcStringTakesNoArguments
    {"String {0}() takes no arguments",
     "文字列の {0}() は引数を取りません"},
    // TcStringCharsTakesNoArguments
    {"String chars() takes no arguments",
     "文字列の chars() は引数を取りません"},
    // TcStringCodepointAtTakes1
    {"String codepoint_at() takes 1 argument",
     "文字列の codepoint_at() は1つの引数を取ります"},
    // TcStringCodepointAtIndexMust
    {"String codepoint_at() index must be integer",
     "文字列の codepoint_at() の添字は整数でなければなりません"},
    // TcStringTakes1Argument
    {"String {0}() takes 1 argument",
     "文字列の {0}() は1つの引数を取ります"},
    // TcStringIndexMustInteger
    {"String {0}() index must be integer",
     "文字列の {0}() の添字は整数でなければなりません"},
    // TcStringTakes12Arguments
    {"String {0}() takes 1-2 arguments",
     "文字列の {0}() は1〜2個の引数を取ります"},
    // TcStringArgumentsMustIntegers
    {"String {0}() arguments must be integers",
     "文字列の {0}() の引数は整数でなければなりません"},
    // TcStringIndexofTakes1Argument
    {"String indexOf() takes 1 argument",
     "文字列の indexOf() は1つの引数を取ります"},
    // TcStringIndexofArgumentMustString
    {"String indexOf() argument must be string",
     "文字列の indexOf() の引数は文字列でなければなりません"},
    // TcStringArgumentMustString
    {"String {0}() argument must be string",
     "文字列の {0}() の引数は文字列でなければなりません"},
    // TcStringRepeatTakes1Argument
    {"String repeat() takes 1 argument",
     "文字列の repeat() は1つの引数を取ります"},
    // TcStringRepeatCountMustInteger
    {"String repeat() count must be integer",
     "文字列の repeat() の回数は整数でなければなりません"},
    // TcStringReplaceTakes2Arguments
    {"String replace() takes 2 arguments",
     "文字列の replace() は2つの引数を取ります"},
    // TcStringReplaceArgumentsMustStrings
    {"String replace() arguments must be strings",
     "文字列の replace() の引数は文字列でなければなりません"},
    // TcStringFirstTakesNoArguments
    {"String first() takes no arguments",
     "文字列の first() は引数を取りません"},
    // TcStringLastTakesNoArguments
    {"String last() takes no arguments",
     "文字列の last() は引数を取りません"},
    // TcUnknownStringMethod
    {"Unknown string method '{0}'",
     "不明な文字列メソッド '{0}' です"},
    // TcInterfaceSpecifiedMoreThanOnce
    {"Interface '{0}' is specified more than once in 'with' / #[derive] for struct '{1}'",
     "インターフェース '{0}' が構造体 '{1}' の 'with' / #[derive] で複数回指定されています"},
    // TcUndefinedTypeGlobalVariable
    {"Undefined type: '{0}' for global variable '{1}'",
     "未定義の型: グローバル変数 '{1}' の '{0}'"},
    // TcUndefinedTypeMacro
    {"Undefined type: '{0}' for macro '{1}'",
     "未定義の型: マクロ '{1}' の '{0}'"},
    // TcUndefinedTypeFieldStruct
    {"Undefined type: '{0}' for field '{1}' in struct '{2}'",
     "未定義の型: 構造体 '{2}' のフィールド '{1}' の '{0}'"},
    // TcNestedCssFieldRequiresType
    {"Nested css field '{0}' requires type '{1}' to implement Css",
     "ネストしたcssフィールド '{0}' には型 '{1}' のCss実装が必要です"},
    // TcEnumVariantHasMultiplePayload
    {"Enum variant '{0}::{1}' has multiple payload values; enum payloads are limited to a single value, wrap them in a struct (e.g. {2}({3}Data))",
     "enumバリアント '{0}::{1}' に複数のペイロード値があります。enumペイロードは単一値に限定されるため、構造体で包んでください（例: {2}({3}Data)）"},
    // TcUndefinedTypeFieldEnumVariant
    {"Undefined type: '{0}' for field '{1}' in enum variant '{2}::{3}'",
     "未定義の型: enumバリアント '{2}::{3}' のフィールド '{1}' の '{0}'"},
    // TcUndefinedTypeTypedef
    {"Undefined type: '{0}' in typedef '{1}'",
     "未定義の型: typedef '{1}' の '{0}'"},
    // TcUndefinedReturnTypeInterfaceMethod
    {"Undefined return type: '{0}' in interface method '{1}::{2}'",
     "未定義の戻り値型: インターフェースメソッド '{1}::{2}' の '{0}'"},
    // TcUndefinedParameterTypeParameterInterface
    {"Undefined parameter type: '{0}' for parameter '{1}' in interface method '{2}::{3}'",
     "未定義のパラメータ型: インターフェースメソッド '{2}::{3}' のパラメータ '{1}' の '{0}'"},
    // TcUndefinedReturnTypeFunction
    {"Undefined return type: '{0}' in function '{1}'",
     "未定義の戻り値型: 関数 '{1}' の '{0}'"},
    // TcUndefinedParameterTypeParameterFunction
    {"Undefined parameter type: '{0}' for parameter '{1}' in function '{2}'",
     "未定義のパラメータ型: 関数 '{2}' のパラメータ '{1}' の '{0}'"},
    // TcLambdaParameterMustHaveExplicit
    {"Lambda parameter '{0}' must have an explicit type. Use: (Type param_name) => { ... }",
     "ラムダパラメータ '{0}' には明示的な型が必要です。次の形式を使用してください: (Type param_name) => { ... }"},
    // TcCannotInferTypeMatchScrutinee
    {"Cannot infer type of match scrutinee",
     "matchの対象式の型を推論できません"},
    // TcMatchGuardMustBooleanExpression
    {"Match guard must be a boolean expression",
     "matchガードはbool式でなければなりません"},
    // TcMatchArmHasIncompatibleType
    {"Match arm {0} has incompatible type (expected '{1}', got '{2}')",
     "matchアーム {0} の型に互換性がありません（期待 '{1}'、実際 '{2}'）"},
    // TcMatchStatementHasNoArms
    {"Match statement has no arms",
     "match文にアームがありません"},
    // TcNonExhaustiveMatchMissingTrue
    {"Non-exhaustive match: missing 'true' or 'false' pattern (or add '_' wildcard)",
     "網羅的でないmatch: 'true' または 'false' パターンがありません（または '_' ワイルドカードを追加してください）"},
    // TcNonExhaustiveMatchMissingPattern
    {"Non-exhaustive match: missing pattern for '{0}' (or add '_' wildcard)",
     "網羅的でないmatch: '{0}' のパターンがありません（または '_' ワイルドカードを追加してください）"},
    // TcNonExhaustiveMatchIntegerPatterns
    {"Non-exhaustive match: integer patterns require a '_' wildcard pattern",
     "網羅的でないmatch: 整数パターンには '_' ワイルドカードパターンが必要です"},
    // TcPatternTypeDoesNotMatch
    {"Pattern type does not match scrutinee type",
     "パターンの型が対象式の型と一致しません"},
    // TcEnumPatternTypeDoesNot
    {"Enum pattern type does not match scrutinee type",
     "enumパターンの型が対象式の型と一致しません"},
    // TcRangeStartTypeDoesNot
    {"Range start type does not match scrutinee type",
     "範囲の開始型が対象式の型と一致しません"},
    // TcRangeEndTypeDoesNot
    {"Range end type does not match scrutinee type",
     "範囲の終了型が対象式の型と一致しません"},
    // TcCannotAssignMovedVariableVariable
    {"Cannot assign to moved variable '{0}': variable no longer exists after move",
     "move済み変数 '{0}' へ代入できません: move後の変数は存在しません"},
    // TcLogicalOperatorsRequireBoolOperands
    {"Logical operators require bool operands",
     "論理演算子にはboolオペランドが必要です"},
    // TcCannotAssignConstVariable
    {"Cannot assign to const variable '{0}'",
     "const変数 '{0}' へ代入できません"},
    // TcCannotAssignWhileItBorrowed
    {"Cannot assign to '{0}' while it is borrowed",
     "借用中の '{0}' へ代入できません"},
    // TcCannotStoreReferenceMayDropped
    {"Cannot store reference to '{0}' in '{1}': '{2}' may be dropped while '{3}' is still alive",
     "'{0}' への参照を '{1}' へ格納できません: '{3}' の生存中に '{2}' が破棄される可能性があります"},
    // TcCannotAssignThroughConstPointer
    {"Cannot assign through const pointer",
     "constポインタ経由で代入できません"},
    // TcCannotAssignThroughPointerConst
    {"Cannot assign through pointer to const",
     "const対象へのポインタ経由で代入できません"},
    // TcTypeDoesNotImplementOperator
    {"Type '{0}' does not implement {1} operator for compound assignment",
     "型 '{0}' は複合代入のための {1} 演算子を実装していません"},
    // TcAssignmentTypeMismatch
    {"Assignment type mismatch",
     "代入の型が一致しません"},
    // TcAddOperatorRequiresNumericOperands
    {"Add operator requires numeric operands or string concatenation",
     "加算演算子には数値オペランドまたは文字列連結が必要です"},
    // TcSubOperatorRequiresNumericOperands
    {"Sub operator requires numeric operands",
     "減算演算子には数値オペランドが必要です"},
    // TcArithmeticOperatorsRequireNumericOperands
    {"Arithmetic operators require numeric operands",
     "算術演算子には数値オペランドが必要です"},
    // TcNegationRequiresNumericOperand
    {"Negation requires numeric operand",
     "符号反転には数値オペランドが必要です"},
    // TcLogicalNotRequiresBoolOperand
    {"Logical not requires bool operand",
     "論理否定にはboolオペランドが必要です"},
    // TcBitwiseNotRequiresIntegerOperand
    {"Bitwise not requires integer operand",
     "ビット否定には整数オペランドが必要です"},
    // TcCannotDereferenceNonPointer
    {"Cannot dereference non-pointer",
     "ポインタでない値をデリファレンスできません"},
    // TcCannotModifyConstVariable
    {"Cannot modify const variable '{0}'",
     "const変数 '{0}' を変更できません"},
    // TcCannotModifyWhileItBorrowed
    {"Cannot modify '{0}' while it is borrowed",
     "借用中の '{0}' を変更できません"},
    // TcIncrementDecrementRequiresNumericOperand
    {"Increment/decrement requires numeric operand",
     "インクリメント/デクリメントには数値オペランドが必要です"},
    // TcTernaryConditionMustBoolInt
    {"Ternary condition must be bool or int",
     "三項演算子の条件はboolまたはintでなければなりません"},
    // TcTernaryBranchesHaveIncompatibleTypes
    {"Ternary branches have incompatible types",
     "三項演算子の分岐の型に互換性がありません"},
    // TcArrayIndexMustIntegerType
    {"Array index must be an integer type",
     "配列の添字は整数型でなければなりません"},
    // TcIndexAccessNonArrayType
    {"Index access on non-array type",
     "配列でない型への添字アクセスです"},
    // TcSliceStartIndexMustInteger
    {"Slice start index must be an integer type",
     "スライスの開始添字は整数型でなければなりません"},
    // TcSliceEndIndexMustInteger
    {"Slice end index must be an integer type",
     "スライスの終了添字は整数型でなければなりません"},
    // TcSliceStepMustIntegerType
    {"Slice step must be an integer type",
     "スライスのステップは整数型でなければなりません"},
    // TcSliceAccessNonArrayString
    {"Slice access on non-array/string type",
     "配列・文字列でない型へのスライスアクセスです"},
    // TcCannotMoveWhileItBorrowed
    {"Cannot move '{0}' while it is borrowed",
     "借用中の '{0}' をmoveできません"},
    // TcUnknownStructType2
    {"Unknown struct type: {0}",
     "不明な構造体型: {0}"},
    // TcCannotStoreCapturingClosureStruct
    {"Cannot store a capturing closure in struct field '{0}' of '{1}': closures lose their captured environment when stored as values (bind to a local variable and call it directly)",
     "キャプチャ付きクロージャを '{1}' の構造体フィールド '{0}' へ格納できません: クロージャは値として格納するとキャプチャ環境を失います（ローカル変数へ束縛して直接呼び出してください）"},
    // TcUndefinedVariable
    {"Undefined variable '{0}'",
     "未定義の変数 '{0}' です"},
    // TcGenericFunctionExpectsArguments
    {"Generic function '{0}' expects {1} arguments, got {2}",
     "ジェネリック関数 '{0}' は{1}個の引数を期待しますが、{2}個指定されました"},
    // TcGenericFunctionExpectsArguments2
    {"Generic function '{0}' expects {1} to {2} arguments, got {3}",
     "ジェネリック関数 '{0}' は{1}〜{2}個の引数を期待しますが、{3}個指定されました"},
    // TcTypeDoesNotSatisfyConstraint
    {"Type '{0}' does not satisfy constraint '{1}' for type parameter '{2}' in function '{3}'",
     "型 '{0}' は関数 '{3}' の型パラメータ '{2}' の制約 '{1}' を満たしません"},
    // TcCannotInferTypeAutoVariable
    {"Cannot infer type for 'auto' variable '{0}' without initializer",
     "初期化子のない 'auto' 変数 '{0}' の型を推論できません"},
    // TcTypeMismatchVariableDeclarationExpected
    {"Type mismatch in variable declaration '{0}': expected '{1}', got '{2}'",
     "変数宣言 '{0}' の型が一致しません: 期待 '{1}'、実際 '{2}'"},
    // TcCannotInferType
    {"Cannot infer type for '{0}'",
     "'{0}' の型を推論できません"},
    // TcReturnTypeMismatchExpected
    {"Return type mismatch: expected '{0}', got '{1}'",
     "戻り値型が一致しません: 期待 '{0}'、実際 '{1}'"},
    // TcCannotReturnReferenceLocalVariable
    {"Cannot return reference to local variable '{0}': variable will be dropped when function returns",
     "ローカル変数 '{0}' への参照を返せません: 関数のreturn時に変数は破棄されます"},
    // TcMissingReturnValueExpected
    {"Missing return value: expected '{0}'",
     "戻り値がありません: '{0}' が必要です"},
    // TcIfConditionMustBool
    {"If condition must be bool, got '{0}'",
     "if条件はboolでなければなりませんが、'{0}' が指定されました"},
    // TcWhileConditionMustBool
    {"While condition must be bool, got '{0}'",
     "while条件はboolでなければなりませんが、'{0}' が指定されました"},
    // TcConditionMustBool
    {"For condition must be bool, got '{0}'",
     "for条件はboolでなければなりませんが、'{0}' が指定されました"},
    // TcCannotInferTypeIterableExpression
    {"Cannot infer type of iterable expression",
     "反復対象式の型を推論できません"},
    // TcRequiresIterableTypeArrayType
    {"For-in requires an iterable type (array or type with iter() method), got '{0}'",
     "for-inには反復可能な型（配列またはiter()メソッドを持つ型）が必要ですが、'{0}' が指定されました"},
    // TcRequiresIterableTypeArray
    {"For-in requires an iterable type (array), got '{0}'",
     "for-inには反復可能な型（配列）が必要ですが、'{0}' が指定されました"},
    // TcVariableTypeMismatchExpected
    {"For-in variable type mismatch: expected '{0}', got '{1}'",
     "for-in変数の型が一致しません: 期待 '{0}'、実際 '{1}'"},
    // TcInvalidLiteralValueLiteralType
    {"Invalid literal value {0} for literal type. Allowed values: {1}",
     "リテラル型に不正なリテラル値 {0} が指定されました。許可される値: {1}"},
    // TcVariableMayUsedBeforeInitialization
    {"Variable '{0}' may be used before initialization",
     "変数 '{0}' は初期化前に使用される可能性があります"},
    // TcVariableNeverModifiedConsiderUsing
    {"Variable '{0}' is never modified, consider using 'const'",
     "変数 '{0}' は変更されていません。'const' の使用を検討してください"},
    // TcVariableNeverUsedW001
    {"Variable '{0}' is never used [W001]",
     "変数 '{0}' は使用されていません [W001]"},
    // TcArraySizeMustPositiveInteger
    {"Array size must be a positive integer, got {0} for '{1}'",
     "配列サイズは正の整数でなければなりませんが、'{1}' に {0} が指定されました"},
    // TcArraySizeMustConstVariable
    {"Array size must be a const variable, but '{0}' is not const",
     "配列サイズはconst変数でなければなりませんが、'{0}' はconstではありません"},
    // TcUndefinedVariableUsedArraySize
    {"Undefined variable '{0}' used as array size",
     "配列サイズに未定義の変数 '{0}' が使用されています"},
    // TcConstVariableDoesNotHave
    {"Const variable '{0}' does not have a compile-time integer value",
     "const変数 '{0}' はコンパイル時整数値を持ちません"},
    // SvSv008UnsynthesizableCallSkipped
    {"warning[SV008]: '{0}' in a synthesis module is not synthesizable and will be skipped (output such as println is only available inside #[test] functions)\n",
     "警告[SV008]: 合成モジュール内の '{0}' は合成不能のためスキップします（println等の出力は #[test] 関数内でのみ使用できます）\n"},
    // SvSv002PointerTypesNotSupported
    {"error[SV002]: Pointer types are not supported in SV target: {0}\n",
     "エラー[SV002]: ポインタ型はSVターゲットではサポートされません: {0}\n"},
    // SvSv005NonConstStringTooLong
    {"error[SV005]: Non-const string longer than 3 characters is not synthesizable (would be truncated to logic [23:0]): {0} = \"{1}\"\n",
     "エラー[SV005]: 3文字を超える非const文字列は合成不能です（logic [23:0] へ切り詰められます）: {0} = \"{1}\"\n"},
    // SvSv002PointerTypesNotSupportedLocal
    {"error[SV002]: Pointer types are not supported in SV target: {0}::{1}\n",
     "エラー[SV002]: ポインタ型はSVターゲットではサポートされません: {0}::{1}\n"},
    // CliEntryPointMainNotFound
    {"error: entry point 'main' not found (the file is a module; import it from a program with main)\n",
     "エラー: エントリポイント 'main' が見つかりません（このファイルはモジュールです。mainを持つプログラムからimportしてください）\n"},
};
// clang-format on

namespace {

// 全メッセージに英語（原文）が定義されていることをコンパイル時に検証する
constexpr bool all_messages_have_english() {
    for (size_t i = 0; i < kMessageCount; ++i) {
        if (kMessages[i][static_cast<size_t>(Lang::En)] == nullptr) {
            return false;
        }
    }
    return true;
}

}  // namespace

static_assert(all_messages_have_english(),
              "messages.cpp: 英語本文が未定義のメッセージがあります（行の過不足を確認）");

}  // namespace cm::i18n
