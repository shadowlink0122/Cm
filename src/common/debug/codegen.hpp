#pragma once

#include "../debug.hpp"

#include <string>

namespace cm::debug::codegen {

/// CodeGen メッセージID
enum class Id {
    // 基本フロー
    Start,
    End,
    BackendSelect,

    // Rust生成
    RustStart,
    RustEnd,
    RustFunction,
    RustStruct,
    RustEnum,
    RustImpl,
    RustExpr,
    RustStmt,
    RustModule,

    // TypeScript生成
    TsStart,
    TsEnd,
    TsFunction,
    TsClass,
    TsInterface,
    TsEnum,
    TsExpr,
    TsStmt,
    TsModule,

    // C++生成
    CppStart,
    CppEnd,
    CppFunction,
    CppClass,
    CppStruct,
    CppEnum,
    CppTemplate,
    CppExpr,
    CppStmt,
    CppHeader,

    // LLVM生成
    LLVMStart,
    LLVMEnd,
    LLVMInit,
    LLVMInitOK,
    LLVMTarget,
    LLVMIRGen,
    LLVMConvert,
    LLVMConvertEnd,
    LLVMFunction,
    LLVMBlock,
    LLVMInst,
    LLVMGlobal,
    LLVMLocal,
    LLVMType,
    LLVMIntrinsics,
    LLVMRuntime,
    LLVMOptimize,
    LLVMOptimizeEnd,
    LLVMVerify,
    LLVMVerifyOK,
    LLVMEmit,
    LLVMEmitEnd,
    LLVMLink,
    LLVMLinkerScript,
    LLVMDump,
    LLVMError,

    // 共通処理
    TypeMap,
    NameMangle,
    SymbolResolve,
    ImportGenerate,
    ModuleResolve,

    // MIR変換
    MirToTarget,
    StateMachine,
    BlockTranslate,
    InstructionTranslate,
    PhiEliminate,

    // 最適化
    TargetOptimize,
    InlineExpand,
    TailCallOptimize,

    // ファイル生成
    FileWrite,
    FormatCode,
    HeaderGenerate,
    SourceGenerate,

    // エラー処理
    Error,
    Warning,
    UnsupportedFeature,

    // デバッグ出力
    CodeDump,
    AstDump,
    SymbolDump
};

/// メッセージテーブル [en, ja]
inline const char* messages[][2] = {
    // 基本フロー
    {"Starting code generation", "コード生成を開始"},
    {"Completed code generation", "コード生成を完了"},
    {"Selected backend", "バックエンドを選択"},

    // Rust生成
    {"Starting Rust generation", "Rust生成を開始"},
    {"Completed Rust generation", "Rust生成を完了"},
    {"Generating Rust function", "Rust関数を生成"},
    {"Generating Rust struct", "Rust構造体を生成"},
    {"Generating Rust enum", "Rust列挙型を生成"},
    {"Generating Rust impl", "Rust impl を生成"},
    {"Generating Rust expression", "Rust式を生成"},
    {"Generating Rust statement", "Rust文を生成"},
    {"Generating Rust module", "Rustモジュールを生成"},

    // TypeScript生成
    {"Starting TypeScript generation", "TypeScript生成を開始"},
    {"Completed TypeScript generation", "TypeScript生成を完了"},
    {"Generating TypeScript function", "TypeScript関数を生成"},
    {"Generating TypeScript class", "TypeScriptクラスを生成"},
    {"Generating TypeScript interface", "TypeScriptインターフェースを生成"},
    {"Generating TypeScript enum", "TypeScript列挙型を生成"},
    {"Generating TypeScript expression", "TypeScript式を生成"},
    {"Generating TypeScript statement", "TypeScript文を生成"},
    {"Generating TypeScript module", "TypeScriptモジュールを生成"},

    // C++生成
    {"Starting C++ generation", "C++生成を開始"},
    {"Completed C++ generation", "C++生成を完了"},
    {"Generating C++ function", "C++関数を生成"},
    {"Generating C++ class", "C++クラスを生成"},
    {"Generating C++ struct", "C++構造体を生成"},
    {"Generating C++ enum", "C++列挙型を生成"},
    {"Generating C++ template", "C++テンプレートを生成"},
    {"Generating C++ expression", "C++式を生成"},
    {"Generating C++ statement", "C++文を生成"},
    {"Generating C++ header", "C++ヘッダーを生成"},

    // LLVM生成
    {"Starting LLVM generation", "LLVM生成を開始"},
    {"Completed LLVM generation", "LLVM生成を完了"},
    {"Initializing LLVM", "LLVMを初期化"},
    {"LLVM initialized", "LLVM初期化完了"},
    {"Setting target", "ターゲットを設定"},
    {"Generating LLVM IR", "LLVM IR生成"},
    {"Converting MIR to LLVM", "MIRをLLVMに変換"},
    {"Conversion complete", "変換完了"},
    {"Processing function", "関数を処理"},
    {"Processing block", "ブロックを処理"},
    {"Processing instruction", "命令を処理"},
    {"Processing global", "グローバル変数を処理"},
    {"Processing local", "ローカル変数を処理"},
    {"Processing type", "型を処理"},
    {"Declaring intrinsics", "組み込み関数を宣言"},
    {"Setting up runtime", "ランタイムを設定"},
    {"Optimizing", "最適化"},
    {"Optimization complete", "最適化完了"},
    {"Verifying module", "モジュールを検証"},
    {"Verification passed", "検証合格"},
    {"Emitting code", "コードを出力"},
    {"Emission complete", "出力完了"},
    {"Linking", "リンク"},
    {"Generating linker script", "リンカスクリプト生成"},
    {"Dumping IR", "IRをダンプ"},
    {"LLVM error", "LLVMエラー"},

    // 共通処理
    {"Mapping types", "型をマッピング"},
    {"Mangling names", "名前をマングル"},
    {"Resolving symbols", "シンボルを解決"},
    {"Generating imports", "インポートを生成"},
    {"Resolving modules", "モジュールを解決"},

    // MIR変換
    {"Converting MIR to target", "MIRをターゲットに変換"},
    {"Generating state machine", "ステートマシンを生成"},
    {"Translating basic block", "基本ブロックを変換"},
    {"Translating instruction", "命令を変換"},
    {"Eliminating PHI nodes", "PHIノードを除去"},

    // 最適化
    {"Optimizing for target", "ターゲット向けに最適化"},
    {"Expanding inline functions", "インライン関数を展開"},
    {"Optimizing tail calls", "末尾呼び出しを最適化"},

    // ファイル生成
    {"Writing file", "ファイルを書き込み"},
    {"Formatting code", "コードをフォーマット"},
    {"Generating header file", "ヘッダーファイルを生成"},
    {"Generating source file", "ソースファイルを生成"},

    // エラー処理
    {"Code generation error", "コード生成エラー"},
    {"Code generation warning", "コード生成警告"},
    {"Unsupported feature", "未対応の機能"},

    // デバッグ出力
    {"Dumping generated code", "生成コードをダンプ"},
    {"Dumping AST", "ASTをダンプ"},
    {"Dumping symbol table", "シンボルテーブルをダンプ"}};

inline const char* get(Id id) {
    return messages[static_cast<int>(id)][::cm::debug::g_lang];
}

inline void log(Id id, ::cm::debug::Level level = ::cm::debug::Level::Debug) {
    if (!::cm::debug::g_debug_mode || level < ::cm::debug::g_debug_level)
        return;

    // バックエンドに応じてステージを選択
    ::cm::debug::Stage stage = ::cm::debug::Stage::CodegenCpp;
    int id_val = static_cast<int>(id);
    if (id_val >= static_cast<int>(Id::RustStart) && id_val <= static_cast<int>(Id::RustModule)) {
        stage = ::cm::debug::Stage::CodegenRust;
    } else if (id_val >= static_cast<int>(Id::TsStart) &&
               id_val <= static_cast<int>(Id::TsModule)) {
        stage = ::cm::debug::Stage::CodegenTs;
    }

    ::cm::debug::log(stage, level, get(id));
}

inline void log(Id id, const std::string& detail,
                ::cm::debug::Level level = ::cm::debug::Level::Debug) {
    if (!::cm::debug::g_debug_mode || level < ::cm::debug::g_debug_level)
        return;

    // バックエンドに応じてステージを選択
    ::cm::debug::Stage stage = ::cm::debug::Stage::CodegenCpp;
    int id_val = static_cast<int>(id);
    if (id_val >= static_cast<int>(Id::RustStart) && id_val <= static_cast<int>(Id::RustModule)) {
        stage = ::cm::debug::Stage::CodegenRust;
    } else if (id_val >= static_cast<int>(Id::TsStart) &&
               id_val <= static_cast<int>(Id::TsModule)) {
        stage = ::cm::debug::Stage::CodegenTs;
    }

    ::cm::debug::log(stage, level, std::string(get(id)) + ": " + detail);
}

/// 生成コードをダンプ（Traceレベル）
inline void dump_code(const std::string& context, const std::string& code) {
    if (!::cm::debug::g_debug_mode || ::cm::debug::Level::Trace < ::cm::debug::g_debug_level)
        return;
    ::cm::debug::log(::cm::debug::Stage::CodegenCpp, ::cm::debug::Level::Trace,
                     "Generated " + context + ":\n" + code);
}

}  // namespace cm::debug::codegen