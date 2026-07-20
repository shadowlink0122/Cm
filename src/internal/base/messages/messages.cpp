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
