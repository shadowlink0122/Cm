// LLVMコード生成器のドライバ処理
// コンパイル全体の進行（compile/compileWithModuleInfo/compileModules）と初期化・IR生成・検証・IR文字列取得を担う
#include "internal/codegen/llvm/native/codegen.hpp"
#include "internal/codegen/llvm/optimizations/mir_pattern_detector.hpp"
#include "internal/codegen/llvm/optimizations/pass_limiter.hpp"
#include "internal/mir/printer.hpp"
#include "internal/mir/splitter.hpp"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>

#ifndef _WIN32
#include <dlfcn.h>
#endif
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace cm::codegen::llvm_backend {

// MIRプログラムをコンパイル
void LLVMCodeGen::compile(const mir::MirProgram& program) {
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMStart);

    // インポートの有無を記録（最適化時に使用）
    hasImports = !program.imports.empty();
    if (hasImports) {
        cm::debug::codegen::log(cm::debug::codegen::Id::LLVMInit,
                                "Program has imports - optimization will be limited");
    }

    // 0. MIRレベルでのパターン検出と最適化レベル調整
    int adjusted_level =
        MIRPatternDetector::adjustOptimizationLevel(program, options.optimizationLevel);
    if (adjusted_level != options.optimizationLevel) {
        if (cm::debug::debug_mode()) {
            std::cerr << "[MIR] 最適化レベルを O" << options.optimizationLevel << " から O"
                      << adjusted_level << " に変更しました（MIRパターン検出による）\n";
        }
        options.optimizationLevel = adjusted_level;
    }

    // 1. 初期化
    initialize(program.filename);

    // 2. MIR → LLVM IR 変換
    generateIR(program);

    // 3. 検証
    if (options.verifyIR) {
        verifyModule();
    }

    // CM_DUMP_IR=1 で最適化前のLLVM IRをstderrへダンプする（非決定性・プラットフォーム差の調査用）
    if (const char* dump_pre = std::getenv("CM_DUMP_IR")) {
        if (dump_pre[0] == '1') {
            context->dumpIRToStderr();
        }
    }

    // 3.5. 最適化前のパターン検出と調整
    if (options.optimizationLevel > 0) {
        int adjusted_level2 = OptimizationPassLimiter::adjustOptimizationLevel(
            context->getModule(), options.optimizationLevel);

        if (adjusted_level2 != options.optimizationLevel) {
            if (cm::debug::debug_mode()) {
                std::cerr << "[LLVM] 最適化レベルを O" << options.optimizationLevel << " から O"
                          << adjusted_level2 << " に変更しました\n";
            }
            options.optimizationLevel = adjusted_level2;
        }
    }

    // 4. 最適化
    optimize();

    // CM_DUMP_IR=2 で最適化後のIRもダンプする
    if (const char* dump_env = std::getenv("CM_DUMP_IR")) {
        if (dump_env[0] == '2') {
            context->dumpIRToStderr();
        }
    }

    // 4.5. サニタイザ計装（最適化後に実行することで冗長な検査の重複挿入を避ける。O0でも動作する）
    instrumentSanitizers();

    // 5. 出力
    emit();

    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMEnd);
}

// モジュール分割付きコンパイル
// 注意: 全体を単一モジュールとしてコンパイルする既定経路。
// モジュール分割コンパイル（compileModules+linkObjects）はドライバのCM_MODULE_CODEGEN=1ゲートから使用される
LLVMCodeGen::ModuleCompileInfo LLVMCodeGen::compileWithModuleInfo(
    const mir::MirProgram& program, const std::vector<std::string>& changed_modules_hint) {
    ModuleCompileInfo info;
    (void)changed_modules_hint;  // 全体コンパイル経路では未使用

    // 現時点では全体をコンパイル
    // モジュール分割情報の収集はスキップ（オーバーヘッド削減）
    compile(program);

    return info;
}

// モジュール内容のFNV-1a 64bitハッシュ（キャッシュキー用。seed違いの2値を連結して128bit相当にする）
static uint64_t fnv1a64(const std::string& s, uint64_t hash) {
    for (unsigned char c : s) {
        hash ^= c;
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

// コンパイラ自身の同一性（実行バイナリのパス+サイズ+更新時刻）
// キャッシュキーへ混ぜることで、コンパイラを更新した後に古い.oを使い続けることを防ぐ
static std::string compilerIdentity() {
    static const std::string identity = [] {
        std::string id;
#ifndef _WIN32
        Dl_info info;
        if (dladdr(reinterpret_cast<void*>(&compilerIdentity), &info) && info.dli_fname) {
            std::error_code ec;
            std::filesystem::path exe(info.dli_fname);
            auto size = std::filesystem::file_size(exe, ec);
            auto mtime = std::filesystem::last_write_time(exe, ec);
            id = exe.string() + ":" + std::to_string(static_cast<unsigned long long>(size)) + ":" +
                 std::to_string(static_cast<long long>(mtime.time_since_epoch().count()));
        }
#endif
        if (id.empty()) {
            // フォールバック: ビルド時刻（このTUの再ビルドでのみ変化する保守的な近似）
            id = std::string(__DATE__) + " " + std::string(__TIME__);
        }
        return id;
    }();
    return identity;
}

// モジュール別差分コンパイル
// 各モジュールのMIR内容（自関数+extern関数+全型レイアウト+ターゲット/最適化設定）から内容ハッシュを計算し、
// output_dir内の内容アドレス.o（<モジュール名>-<ハッシュ>.o）が既に存在すればコード生成・最適化を丸ごとスキップする。
// ミスしたモジュールは独立LLVMContextでワーカスレッド並列にコンパイルする（ワーカ数はCM_CODEGEN_JOBSで上書き可能）
std::vector<LLVMCodeGen::ModuleObjectFile> LLVMCodeGen::compileModules(
    const mir::MirProgram& program, const std::vector<std::string>& changed_modules,
    const std::map<std::string, std::filesystem::path>& cached_objects,
    const std::filesystem::path& output_dir) {
    // 出力ディレクトリを作成
    std::filesystem::create_directories(output_dir);

    // MIR分割
    auto modules = mir::MirSplitter::split_by_module(program);

    // 変更モジュールのセット（指定されたモジュールは内容ハッシュが一致してもキャッシュを使わない）
    std::set<std::string> changed_set(changed_modules.begin(), changed_modules.end());

    // ターゲット設定はモジュール間で共通なので1回だけ構築
    TargetConfig config;
    if (!options.customTriple.empty()) {
        config.triple = options.customTriple;
        config.target = BuildTarget::Native;
    } else {
        switch (options.target) {
            case BuildTarget::Baremetal:
                config = TargetConfig::getBaremetalARM();
                break;
            case BuildTarget::BaremetalX86:
                config = TargetConfig::getBaremetalX86();
                break;
            case BuildTarget::Wasm:
                config = TargetConfig::getWasm();
                break;
            case BuildTarget::BaremetalUEFI:
                config = TargetConfig::getBaremetalUEFI();
                break;
            default:
                config = TargetConfig::getNative();
        }
    }
    config.debugInfo = options.debugInfo;
    // Bug#13修正: UEFIターゲットではCodeGen最適化を無効化
    // LLVM TargetMachineのISel/SelectionDAG最適化がefi_mainのcall/ret命令を削除してフォールスルークラッシュを起こす
    if (config.target == BuildTarget::BaremetalUEFI) {
        config.optLevel = 0;
    } else {
        config.optLevel = options.optimizationLevel;
    }

    // ============================================================
    // キャッシュキー計算
    // ============================================================
    mir::MirPrinter printer;

    // 全モジュール共通部: ターゲット・最適化設定と型レイアウト（構造体・enum・グローバル・インターフェイス）。
    // 構造体レイアウトの変更はGEPオフセットとして全モジュールのコードへ波及するため、共通キーに含めて全体を無効化する
    std::string shared_key;
    shared_key += compilerIdentity() + "|";
    shared_key += config.triple + "|" + config.cpu + "|" + config.features + "|";
    shared_key += std::to_string(config.optLevel) + "|" + std::to_string(config.debugInfo) + "|";
    for (const auto& st : program.structs) {
        if (!st)
            continue;
        shared_key += "S:" + st->name + "{";
        for (const auto& field : st->fields) {
            shared_key += field.name + ":" + printer.type_to_string(field.type) + ";";
        }
        shared_key += "}";
    }
    for (const auto& en : program.enums) {
        if (!en)
            continue;
        shared_key += "E:" + en->name + "{";
        for (const auto& member : en->members) {
            shared_key += member.name + "=" + std::to_string(member.tag_value) + "(";
            for (const auto& [field_name, field_type] : member.fields) {
                shared_key += field_name + ":" + printer.type_to_string(field_type) + ",";
            }
            shared_key += ");";
        }
        shared_key += "}";
    }
    for (const auto& gv : program.global_vars) {
        if (!gv)
            continue;
        shared_key += "G:" + gv->name + ":" + printer.type_to_string(gv->type) + ";";
    }
    for (const auto& iface : program.interfaces) {
        if (!iface)
            continue;
        shared_key += "I:" + iface->name + ";";
    }

    // モジュール固有部: 自関数の全文 + extern関数の全文
    // extern関数はシグネチャ宣言に加えBug#45経路でbodyも生成されるため、全文を含める（過剰無効化側に倒す）
    auto module_key = [&](const mir::ModuleProgram& mod_program) {
        std::string text = shared_key;
        for (const auto* func : mod_program.functions) {
            text += printer.to_string(*func);
        }
        for (const auto* func : mod_program.extern_functions) {
            text += "X:" + printer.to_string(*func);
        }
        uint64_t h1 = fnv1a64(text, 0xcbf29ce484222325ULL);
        uint64_t h2 = fnv1a64(text, 0x9e3779b97f4a7c15ULL);
        char buf[33];
        std::snprintf(buf, sizeof(buf), "%016llx%016llx", static_cast<unsigned long long>(h1),
                      static_cast<unsigned long long>(h2));
        return std::string(buf);
    };

    // ============================================================
    // キャッシュ判定
    // ============================================================
    struct ModuleJob {
        const std::string* mod_name;
        const mir::ModuleProgram* mod_program;
        std::filesystem::path obj_path;
        size_t result_index;
    };
    std::vector<ModuleObjectFile> results(modules.size());
    std::vector<ModuleJob> jobs;
    size_t index = 0;

    for (const auto& [mod_name, mod_program] : modules) {
        ModuleObjectFile& result = results[index];
        result.module_name = mod_name;

        // 呼び出し元指定のキャッシュ（旧API互換）を先に確認
        auto cache_it = cached_objects.find(mod_name);
        bool caller_cached = changed_set.count(mod_name) == 0 && cache_it != cached_objects.end() &&
                             std::filesystem::exists(cache_it->second);
        if (caller_cached) {
            result.object_path = cache_it->second;
            result.from_cache = true;
            if (cm::debug::debug_mode()) {
                std::cerr << "[MODULE] " << mod_name << ": キャッシュヒット（指定） ("
                          << cache_it->second.string() << ")\n";
            }
            ++index;
            continue;
        }

        // 内容アドレスキャッシュ
        std::filesystem::path obj_path =
            output_dir / (mod_name + "-" + module_key(mod_program) + ".o");
        result.object_path = obj_path;
        if (changed_set.count(mod_name) == 0 && std::filesystem::exists(obj_path)) {
            result.from_cache = true;
            if (cm::debug::debug_mode()) {
                std::cerr << "[MODULE] " << mod_name << ": キャッシュヒット（内容一致） ("
                          << obj_path.string() << ")\n";
            }
            ++index;
            continue;
        }

        result.from_cache = false;
        jobs.push_back(ModuleJob{&results[index].module_name, &mod_program, obj_path, index});
        ++index;
    }

    // ============================================================
    // キャッシュミスのモジュールを並列コンパイル
    // ============================================================
    if (!jobs.empty()) {
        // LLVMターゲットレジストリの初期化は非スレッドセーフなため、ワーカ起動前にメインスレッドで1回行う
        {
            TargetManager init_target(config);
            init_target.initialize();
        }

        size_t worker_count = std::thread::hardware_concurrency();
        if (const char* jobs_env = std::getenv("CM_CODEGEN_JOBS")) {
            long v = std::strtol(jobs_env, nullptr, 10);
            if (v > 0)
                worker_count = static_cast<size_t>(v);
        }
        if (worker_count == 0)
            worker_count = 1;
        worker_count = std::min(worker_count, jobs.size());

        std::mutex log_mutex;
        std::atomic<size_t> next_job{0};
        std::vector<std::exception_ptr> worker_errors(worker_count);

        auto compile_one = [&](const ModuleJob& job) {
            const std::string& mod_name = *job.mod_name;
            const mir::ModuleProgram& mod_program = *job.mod_program;

            if (cm::debug::debug_mode()) {
                std::lock_guard<std::mutex> lock(log_mutex);
                std::cerr << "[MODULE] " << mod_name << ": コンパイル中 ("
                          << mod_program.functions.size() << " 関数)\n";
            }

            // 独立LLVMContextを作成
            auto mod_context = std::make_unique<LLVMContext>(mod_name + "_module", config);
            auto mod_target = std::make_unique<TargetManager>(config);
            mod_target->initialize();
            mod_target->configureModule(mod_context->getModule());

            // 組み込み関数を登録
            auto mod_intrinsics = std::make_unique<IntrinsicsManager>(
                &mod_context->getModule(), &mod_context->getContext(), config);

            // MIR→LLVM IR変換
            MIRToLLVM mod_converter(*mod_context);
            mod_converter.convert(mod_program);

            // LLVM IR検証
            if (options.verifyIR) {
                std::string errStr;
                llvm::raw_string_ostream errStream(errStr);
                if (llvm::verifyModule(mod_context->getModule(), &errStream)) {
                    throw std::runtime_error("モジュール " + mod_name + " のLLVM IR検証失敗:\n" +
                                             errStr);
                }
            }

            // 最適化
            // Bug#13修正: UEFIターゲットではLLVM最適化パスをスキップ
            // O2のインライン展開+DCEがefi_mainの制御フローを破壊し、call/ret命令が消滅してフォールスルークラッシュを引き起こす
            bool isUefiModule = config.target == BuildTarget::BaremetalUEFI;
            if (options.optimizationLevel > 0 && !isUefiModule) {
                llvm::LoopAnalysisManager LAM;
                llvm::FunctionAnalysisManager FAM;
                llvm::CGSCCAnalysisManager CGAM;
                llvm::ModuleAnalysisManager MAM;
                // 同一コード折り畳み（M10）は分割経路でも有効化する
                llvm::PipelineTuningOptions modulePipelineTuning;
                modulePipelineTuning.MergeFunctions = (options.optimizationLevel >= 2);
                llvm::PassBuilder PB(mod_target->getTargetMachine(), modulePipelineTuning);
                PB.registerModuleAnalyses(MAM);
                PB.registerCGSCCAnalyses(CGAM);
                PB.registerFunctionAnalyses(FAM);
                PB.registerLoopAnalyses(LAM);
                PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

                llvm::OptimizationLevel optLevel;
                switch (options.optimizationLevel) {
                    case 1:
                        optLevel = llvm::OptimizationLevel::O1;
                        break;
                    case 2:
                        optLevel = llvm::OptimizationLevel::O2;
                        break;
                    default:
                        optLevel = llvm::OptimizationLevel::O3;
                        break;
                }
                auto MPM = PB.buildPerModuleDefaultPipeline(optLevel);
                MPM.run(mod_context->getModule(), MAM);
            }

            // CM_DUMP_IR=1/2でモジュール別IRをstderrへダンプ（分割経路のデバッグ用。1=変換直後相当、2=最適化後）
            if (const char* dump_env = std::getenv("CM_DUMP_IR");
                dump_env && (dump_env[0] == '1' || dump_env[0] == '2')) {
                std::lock_guard<std::mutex> lock(log_mutex);
                llvm::errs() << ";; ===== module: " << mod_name << " =====\n";
                mod_context->getModule().print(llvm::errs(), nullptr);
            }

            // オブジェクトファイル出力（直接版: ワーカスレッドからのforkを避ける。空モジュールも正常な.oになる）
            // 並走する別プロセスが同一キーを書く場合に備え、一時名へ出力してからrenameで原子的に置く
            std::filesystem::path tmp_path = job.obj_path;
            tmp_path +=
                ".tmp" +
                std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()) & 0xFFFF);
            mod_target->emitObjectFileDirect(mod_context->getModule(), tmp_path.string());
            std::filesystem::rename(tmp_path, job.obj_path);
        };

        auto worker = [&](size_t worker_id) {
            try {
                while (true) {
                    size_t i = next_job.fetch_add(1);
                    if (i >= jobs.size())
                        break;
                    compile_one(jobs[i]);
                }
            } catch (...) {
                worker_errors[worker_id] = std::current_exception();
            }
        };

        if (worker_count == 1) {
            worker(0);
        } else {
            std::vector<std::thread> threads;
            threads.reserve(worker_count);
            for (size_t w = 0; w < worker_count; ++w) {
                threads.emplace_back(worker, w);
            }
            for (auto& t : threads) {
                t.join();
            }
        }

        for (const auto& err : worker_errors) {
            if (err) {
                std::rethrow_exception(err);
            }
        }
    }

    return results;
}

// LLVM IR を文字列として取得（デバッグ用）
std::string LLVMCodeGen::getIRString() const {
    std::string str;
    llvm::raw_string_ostream os(str);
    context->getModule().print(os, nullptr);
    return str;
}

// 初期化
void LLVMCodeGen::initialize(const std::string& moduleName) {
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMInit);

    // ターゲット設定
    TargetConfig config;
    if (!options.customTriple.empty()) {
        config.triple = options.customTriple;
        config.target = BuildTarget::Native;
    } else {
        switch (options.target) {
            case BuildTarget::Baremetal:
                config = TargetConfig::getBaremetalARM();
                break;
            case BuildTarget::BaremetalX86:
                config = TargetConfig::getBaremetalX86();
                break;
            case BuildTarget::Wasm:
                config = TargetConfig::getWasm();
                break;
            case BuildTarget::BaremetalUEFI:
                config = TargetConfig::getBaremetalUEFI();
                break;
            default:
                config = TargetConfig::getNative();
        }
    }
    config.debugInfo = options.debugInfo;
    // Bug#13修正: UEFIターゲットではCodeGen最適化を無効化
    // LLVM最適化パスがefi_mainのcall/ret命令を削除してフォールスルークラッシュを起こす
    if (options.target == BuildTarget::BaremetalUEFI) {
        config.optLevel = 0;
    } else {
        config.optLevel = options.optimizationLevel;
    }

    // コンテキスト作成
    context = std::make_unique<LLVMContext>(moduleName, config);

    // ターゲットマネージャ
    targetManager = std::make_unique<TargetManager>(config);
    targetManager->initialize();
    targetManager->configureModule(context->getModule());

    // 組み込み関数マネージャ（宣言は遅延実行）
    intrinsicsManager =
        std::make_unique<IntrinsicsManager>(&context->getModule(), &context->getContext(), config);

    // 変換器
    converter = std::make_unique<MIRToLLVM>(*context);

    // ベアメタルの場合、スタートアップコード生成
    if (config.target == BuildTarget::Baremetal) {
        targetManager->generateStartupCode(context->getModule());
    }
}

// IR生成
void LLVMCodeGen::generateIR(const mir::MirProgram& program) {
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMIRGen, "Generating LLVM IR from MIR");
    converter->convert(program);
    if (options.verbose) {
        llvm::errs() << "=== Generated LLVM IR ===\n";
        context->getModule().print(llvm::errs(), nullptr);
        llvm::errs() << "========================\n";
    }
}

// モジュール検証
void LLVMCodeGen::verifyModule() {
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMVerify);
    std::string errors;
    llvm::raw_string_ostream os(errors);
    if (llvm::verifyModule(context->getModule(), &os)) {
        context->getModule().print(llvm::errs(), nullptr);
        cm::debug::codegen::log(cm::debug::codegen::Id::LLVMError,
                                "Module verification failed: " + errors, cm::debug::Level::Error);
        throw std::runtime_error("LLVM module verification failed:\n" + errors);
    }
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMVerifyOK);
}

}  // namespace cm::codegen::llvm_backend
