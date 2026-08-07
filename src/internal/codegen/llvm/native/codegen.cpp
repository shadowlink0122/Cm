// LLVMコード生成器の実装
#include "codegen.hpp"

#include "internal/codegen/llvm/optimizations/mir_pattern_detector.hpp"
#include "internal/codegen/llvm/optimizations/optimization_manager.hpp"
#include "internal/codegen/llvm/optimizations/pass_limiter.hpp"
#include "internal/codegen/llvm/optimizations/recursion_limiter.hpp"
#include "internal/mir/mir_splitter.hpp"
#include "internal/mir/printer.hpp"
#include "pass_debugger.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>

#ifndef _WIN32
#include <dlfcn.h>
#endif
#include <llvm/Transforms/Instrumentation/AddressSanitizer.h>
#include <llvm/Transforms/Instrumentation/BoundsChecking.h>
#include <llvm/Transforms/Instrumentation/MemorySanitizer.h>
#include <llvm/Transforms/Instrumentation/ThreadSanitizer.h>
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

// 複数オブジェクトファイルからリンク
void LLVMCodeGen::linkObjects(const std::vector<std::filesystem::path>& objects,
                              const std::string& output_file, const mir::MirProgram* program) {
    // 初期化が必要（ターゲット情報取得のため）
    if (!context) {
        initialize("link_module");
    }

    // 使用ライブラリの検出
    // MIRが渡された場合は呼び出し関数名の接頭辞から判定する（モジュール分割経路ではcontextが空でLLVM宣言ベースの判定が効かない）
    bool needsGPU = false;
    bool needsNet = false;
    bool needsSync = false;
    bool needsThread = false;
    bool needsHTTP = false;
    if (program) {
        auto has_prefix = [](const std::string& name, const char* prefix) {
            return name.rfind(prefix, 0) == 0;
        };
        for (const auto& func : program->functions) {
            if (!func)
                continue;
            for (const auto& called : mir::MirSplitter::collect_called_functions(*func)) {
                needsGPU = needsGPU || has_prefix(called, "gpu_");
                needsNet = needsNet || has_prefix(called, "cm_tcp_") ||
                           has_prefix(called, "cm_udp_") || has_prefix(called, "cm_dns_") ||
                           has_prefix(called, "cm_socket_");
                needsSync =
                    needsSync || has_prefix(called, "cm_mutex_") ||
                    has_prefix(called, "cm_rwlock_") || has_prefix(called, "cm_atomic_") ||
                    has_prefix(called, "cm_channel_") || has_prefix(called, "cm_once_") ||
                    has_prefix(called, "atomic_store_") || has_prefix(called, "atomic_load_") ||
                    has_prefix(called, "atomic_fetch_") || has_prefix(called, "atomic_compare_");
                needsThread = needsThread || has_prefix(called, "cm_thread_");
                needsHTTP = needsHTTP || has_prefix(called, "cm_http_");
            }
        }
    } else {
        needsGPU = checkForGPUUsage();
        needsNet = checkForNetUsage();
        needsSync = checkForSyncUsage();
        needsThread = checkForThreadUsage();
        needsHTTP = checkForHTTPUsage();
    }
    bool needsPthread = needsSync || needsThread;
    bool needsCppRuntime = needsGPU || needsNet || needsSync || needsThread || needsHTTP;

    // オブジェクトファイルリストを構築
    std::string obj_list;
    for (const auto& obj : objects) {
        obj_list += obj.string() + " ";
    }

    std::string linkCmd;
    auto target = context->getTargetConfig().target;

    if (target == BuildTarget::Baremetal) {
        linkCmd = "arm-none-eabi-ld -T link.ld " + obj_list + "-o " + output_file;
    } else if (target == BuildTarget::BaremetalUEFI) {
        linkCmd = "lld-link /subsystem:efi_application /entry:efi_main /out:" + output_file + " " +
                  obj_list;
    } else if (target == BuildTarget::Wasm) {
        std::string runtimePath = findRuntimeLibrary();
        linkCmd = "wasm-ld --entry=_start -z stack-size=1048576 " + obj_list + runtimePath +
                  " -o " + output_file;
    } else {
        // ネイティブリンク
        std::string runtimePath = findRuntimeLibrary();

#ifdef __APPLE__
        linkCmd = "/usr/bin/clang++ -mmacosx-version-min=15.0 -Wl,-dead_strip ";
#ifdef CM_DEFAULT_TARGET_ARCH
        linkCmd += "-arch " + std::string(CM_DEFAULT_TARGET_ARCH) + " ";
#endif
        if (context->getTargetConfig().noStd) {
            linkCmd += "-nostdlib ";
        }
        linkCmd += obj_list + runtimePath;

        if (needsGPU) {
            std::string gpuRuntimePath = findGPURuntimeLibrary();
            if (!gpuRuntimePath.empty()) {
                linkCmd += " " + gpuRuntimePath;
                linkCmd += " -framework Metal -framework Foundation";
            }
        }
        if (needsNet) {
            std::string path = findStdRuntimeLibrary("net");
            if (!path.empty())
                linkCmd += " " + path;
        }
        if (needsSync) {
            std::string path = findStdRuntimeLibrary("sync");
            if (!path.empty())
                linkCmd += " " + path;
        }
        if (needsThread) {
            std::string path = findStdRuntimeLibrary("thread");
            if (!path.empty())
                linkCmd += " " + path;
        }
        if (needsHTTP) {
            std::string path = findStdRuntimeLibrary("http");
            if (!path.empty())
                linkCmd += " " + path;
            // HTTPランタイムはOpenSSLへ依存する（emit側と同じ探索順: Homebrewの既知プレフィックス→brew --prefix）
            std::string opensslPrefix;
#ifdef CM_DEFAULT_TARGET_ARCH
            std::string targetArch = CM_DEFAULT_TARGET_ARCH;
#else
            std::string targetArch = "arm64";
#endif
            if (targetArch == "arm64") {
                if (std::filesystem::exists("/opt/homebrew/opt/openssl@3/lib")) {
                    opensslPrefix = "/opt/homebrew/opt/openssl@3";
                }
            } else {
                if (std::filesystem::exists("/usr/local/opt/openssl@3/lib")) {
                    opensslPrefix = "/usr/local/opt/openssl@3";
                }
            }
            if (opensslPrefix.empty()) {
                FILE* pipe = popen("brew --prefix openssl@3 2>/dev/null", "r");
                if (pipe) {
                    char buffer[256];
                    if (fgets(buffer, sizeof(buffer), pipe)) {
                        opensslPrefix = buffer;
                        while (!opensslPrefix.empty() && opensslPrefix.back() == '\n')
                            opensslPrefix.pop_back();
                    }
                    pclose(pipe);
                }
            }
            if (!opensslPrefix.empty()) {
                linkCmd += " -L" + opensslPrefix + "/lib";
            }
            linkCmd += " -lssl -lcrypto";
        }
        if (needsCppRuntime)
            linkCmd += " -lc++";
        if (needsPthread)
            linkCmd += " -lpthread";
        linkCmd += " -o " + output_file;
#else
        linkCmd = "clang -Wl,--gc-sections ";
        if (context->getTargetConfig().noStd) {
            linkCmd += "-nostdlib ";
        }
        linkCmd += obj_list + runtimePath;
        if (needsGPU) {
            std::string gpuRuntimePath = findGPURuntimeLibrary();
            if (!gpuRuntimePath.empty())
                linkCmd += " " + gpuRuntimePath;
        }
        if (needsNet) {
            std::string path = findStdRuntimeLibrary("net");
            if (!path.empty())
                linkCmd += " " + path;
        }
        if (needsSync) {
            std::string path = findStdRuntimeLibrary("sync");
            if (!path.empty())
                linkCmd += " " + path;
        }
        if (needsThread) {
            std::string path = findStdRuntimeLibrary("thread");
            if (!path.empty())
                linkCmd += " " + path;
        }
        if (needsHTTP) {
            std::string path = findStdRuntimeLibrary("http");
            if (!path.empty())
                linkCmd += " " + path;
            linkCmd += " -lssl -lcrypto";
        }
        linkCmd += " -o " + output_file;
        if (needsCppRuntime)
            linkCmd += " -lstdc++";
        if (needsPthread)
            linkCmd += " -lpthread";
#endif
    }

    if (cm::debug::debug_mode()) {
        std::cerr << "[LINK] " << linkCmd << "\n";
    }

    auto link_start = std::chrono::steady_clock::now();
    int ret = std::system(linkCmd.c_str());
    auto link_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - link_start)
                       .count();
    // emitExecutableと同じ切り分け警告（システムリンカ待ちの長時間化はcm外の要因）
    if (link_ms > 10000) {
        std::cerr << "warning: linker took " << (link_ms / 1000)
                  << "s (cm waits for the system linker; security software scanning new "
                     "binaries can cause this)\n";
    }
    if (ret != 0) {
        // R23: wasmのundefined symbolは、native専用FFIモジュール（std::env/fs・native::net/gpu/sync/thread等）を
        // wasmターゲットでコンパイルした場合に発生する（従来は--allow-undefinedで黙って通り、実行時のunknown importで破綻していた）
        if (target == BuildTarget::Wasm) {
            throw std::runtime_error(
                "リンクコマンド失敗: " + linkCmd +
                "\nhint: undefined "
                "symbolはネイティブ専用FFI（std::env/std::fs・native::net/gpu/sync/thread等）を"
                "wasmターゲットで使用した場合に発生します。これらのモジュールはwasmでは未対応です");
        }
        throw std::runtime_error("リンクコマンド失敗: " + linkCmd);
    }
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

// 最適化
void LLVMCodeGen::optimize() {
    // Bug#13修正: UEFIターゲットでは全最適化をスキップ
    // O2のインライン展開+DCEがefi_mainの制御フローを破壊する
    if (context->getTargetConfig().target == BuildTarget::BaremetalUEFI) {
        return;
    }

    if (options.optimizationLevel == 0) {
        return;
    }

    // 再帰とループの事前検証
    RecursionLimiter::preprocessModule(context->getModule(), options.optimizationLevel);

    // パターンベースの最適化レベル調整
    int adjustedLevel = OptimizationPassLimiter::adjustOptimizationLevel(context->getModule(),
                                                                         options.optimizationLevel);
    if (adjustedLevel != options.optimizationLevel) {
        cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimize,
                                "Optimization level adjusted from O" +
                                    std::to_string(options.optimizationLevel) + " to O" +
                                    std::to_string(adjustedLevel));
        options.optimizationLevel = adjustedLevel;

        if (adjustedLevel == 0) {
            cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimize,
                                    "Skipping optimization due to complexity patterns");
            return;
        }
    }

    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimize,
                            "Level " + std::to_string(options.optimizationLevel));

    // カスタム最適化を使用する場合
    if (options.useCustomOptimizations) {
        using namespace cm::codegen::llvm_backend::optimizations;

        OptimizationManager::OptLevel customLevel;
        switch (options.optimizationLevel) {
            case 1:
                customLevel = OptimizationManager::OptLevel::O1;
                break;
            case 2:
                customLevel = OptimizationManager::OptLevel::O2;
                break;
            case 3:
                customLevel = OptimizationManager::OptLevel::O3;
                break;
            case -1:
                customLevel = OptimizationManager::OptLevel::Oz;
                break;
            default:
                customLevel = OptimizationManager::OptLevel::O2;
        }

        auto config = createConfigFromLevel(customLevel);

        if (context->getTargetConfig().target == BuildTarget::Wasm) {
            config = createConfigForTarget("wasm32");
        } else if (context->getTargetConfig().target == BuildTarget::Baremetal) {
            config.level = OptimizationManager::OptLevel::Os;
            config.enableVectorization = false;
        }

        config.printStatistics = options.verbose;

        OptimizationManager optManager(config);
        optManager.optimizeModule(context->getModule());

        if (options.verbose) {
            llvm::errs() << "\n[Custom Optimizations Complete]\n";
        }
    }

    // PassBuilder設定。
    // O2以上ではMergeFunctions（同一コード折り畳み・ICF）を有効化する（M10）。
    // モノモーフ化はレイアウト同一の特殊化（pick__int/pick__uint等）を別関数として複製するため、証明可能に同一なIR本体をLLVMパスで正準実装へのサンクに折り畳み、生成物サイズと後段コンパイル時間の乗算的膨張を抑える。
    // 注: 設計文書の「int/uint特殊化のフロント側エイリアス化」案は、符号性が比較・除算・拡張の意味論を変えるため不採用（同一レイアウト≠同一意味論）。
    // MergeFunctionsは本体が構造的に完全同一の場合のみ折り畳むため安全
    llvm::TargetMachine* TM = targetManager ? targetManager->getTargetMachine() : nullptr;
    llvm::PipelineTuningOptions pipelineTuning;
    pipelineTuning.MergeFunctions = (options.optimizationLevel >= 2);
    llvm::PassBuilder passBuilder(TM, pipelineTuning);

    // 解析マネージャ
    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;

    passBuilder.registerModuleAnalyses(MAM);
    passBuilder.registerCGSCCAnalyses(CGAM);
    passBuilder.registerFunctionAnalyses(FAM);
    passBuilder.registerLoopAnalyses(LAM);
    passBuilder.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    // 最適化レベル
    llvm::OptimizationLevel optLevel;
    switch (options.optimizationLevel) {
        case 1:
            optLevel = llvm::OptimizationLevel::O1;
            break;
        case 2:
            optLevel = llvm::OptimizationLevel::O2;
            break;
        case 3:
            optLevel = llvm::OptimizationLevel::O3;
            break;
        case -1:
            optLevel = llvm::OptimizationLevel::Oz;
            break;
        default:
            optLevel = llvm::OptimizationLevel::O2;
    }

    // verboseモードでのパステスト
    if (options.verbose && (options.optimizationLevel >= 2)) {
        llvm::errs() << "[PASS_DEBUG] Running individual pass debugging for O"
                     << options.optimizationLevel << "\n";
        auto results =
            PassDebugger::runPassesWithTimeout(context->getModule(), passBuilder, optLevel, 5000);
        cm::codegen::llvm_backend::PassDebugger::printResults(results);

        bool hasTimeout = false;
        for (const auto& result : results) {
            if (result.timeout) {
                hasTimeout = true;
                llvm::errs() << "[PASS_DEBUG] Detected timeout in pass: " << result.passName
                             << "\n";
                llvm::errs() << "[PASS_DEBUG] Falling back to O1 optimization\n";
                break;
            }
        }

        if (hasTimeout) {
            optLevel = llvm::OptimizationLevel::O1;
        }
    }

    // モジュールパスマネージャ
    llvm::ModulePassManager MPM;

    // ターゲット別の最適化
    if (context->getTargetConfig().target == BuildTarget::Wasm) {
        MPM = passBuilder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::Oz);
    } else if (context->getTargetConfig().target == BuildTarget::Baremetal) {
        MPM = passBuilder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::Os);
    } else if (context->getTargetConfig().target == BuildTarget::BaremetalUEFI) {
        MPM = passBuilder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
    } else {
        MPM = passBuilder.buildPerModuleDefaultPipeline(optLevel);
    }

    MPM.run(context->getModule(), MAM);

    if (options.verbose) {
        llvm::errs() << "=== Optimized LLVM IR ===\n";
        context->getModule().print(llvm::errs(), nullptr);
        llvm::errs() << "========================\n";
    }

    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimizeEnd);
}

// サニタイザリンク用のclang++ドライバ探索
// LLVM計装が参照するランタイム記号（__asan_version_mismatch_check_v8等）を持つHomebrew LLVMのclang++を新しい順に探索する。
// 古いcompiler-rt（例: LLVM 17）は新しいmacOS（26.x）で初期化に失敗するため、バージョン非固定のllvm（最新）を最優先する
std::string LLVMCodeGen::findSanitizerLinkDriver() {
    std::vector<std::string> candidates = {
        "/opt/homebrew/opt/llvm/bin/clang++",
        "/usr/local/opt/llvm/bin/clang++",
        "/opt/homebrew/opt/llvm@17/bin/clang++",
        "/usr/local/opt/llvm@17/bin/clang++",
    };
    for (const auto& path : candidates) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    return "/usr/bin/clang++";
}

// サニタイザ計装
// address/thread/memory: 本体を持つ関数へ対応する sanitize_* 属性を付与してから各LLVM計装パスを実行する（ランタイムはリンク時に -fsanitize= で解決）
// bounds: BoundsCheckingPass（clang -fsanitize=local-bounds 相当）で静的にサイズが分かるメモリアクセスへ境界チェックを挿入し、違反時は llvm.trap で即停止する（ランタイム不要のためnative/wasm両対応）
void LLVMCodeGen::instrumentSanitizers() {
    const bool needsRuntimeSanitizer =
        options.sanitizeAddress || options.sanitizeThread || options.sanitizeMemory;
    if (!needsRuntimeSanitizer && !options.sanitizeBounds) {
        return;
    }

    cm::debug::codegen::log(
        cm::debug::codegen::Id::LLVMOptimize,
        std::string("Sanitizer instrumentation:") + (options.sanitizeAddress ? " address" : "") +
            (options.sanitizeThread ? " thread" : "") + (options.sanitizeMemory ? " memory" : "") +
            (options.sanitizeBounds ? " bounds" : ""));

    if (needsRuntimeSanitizer) {
        for (auto& func : context->getModule()) {
            // 宣言のみの外部関数と純ASMのNaked関数（prologue/epilogueが無くredzone操作が壊れる）は計装しない
            if (func.isDeclaration() || func.hasFnAttribute(llvm::Attribute::Naked)) {
                continue;
            }
            if (options.sanitizeAddress) {
                func.addFnAttr(llvm::Attribute::SanitizeAddress);
            }
            if (options.sanitizeThread) {
                func.addFnAttr(llvm::Attribute::SanitizeThread);
            }
            if (options.sanitizeMemory) {
                func.addFnAttr(llvm::Attribute::SanitizeMemory);
            }
        }
    }

    llvm::PassBuilder passBuilder(targetManager ? targetManager->getTargetMachine() : nullptr);
    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;
    passBuilder.registerModuleAnalyses(MAM);
    passBuilder.registerCGSCCAnalyses(CGAM);
    passBuilder.registerFunctionAnalyses(FAM);
    passBuilder.registerLoopAnalyses(LAM);
    passBuilder.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    llvm::ModulePassManager MPM;
    if (options.sanitizeBounds) {
        MPM.addPass(llvm::createModuleToFunctionPassAdaptor(llvm::BoundsCheckingPass()));
    }
    if (options.sanitizeAddress) {
        MPM.addPass(llvm::AddressSanitizerPass(llvm::AddressSanitizerOptions()));
    }
    if (options.sanitizeThread) {
        MPM.addPass(llvm::ModuleThreadSanitizerPass());
        MPM.addPass(llvm::createModuleToFunctionPassAdaptor(llvm::ThreadSanitizerPass()));
    }
    if (options.sanitizeMemory) {
        MPM.addPass(llvm::MemorySanitizerPass(llvm::MemorySanitizerOptions()));
    }
    MPM.run(context->getModule(), MAM);
}

// 出力
void LLVMCodeGen::emit() {
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMEmit, options.outputFile);

    switch (options.format) {
        case OutputFormat::ObjectFile:
            emitObjectFile();
            break;
        case OutputFormat::Assembly:
            emitAssembly();
            break;
        case OutputFormat::LLVMIR:
            emitLLVMIR();
            break;
        case OutputFormat::Bitcode:
            emitBitcode();
            break;
        case OutputFormat::Executable:
            emitExecutable();
            break;
    }

    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMEmitEnd, "Output: " + options.outputFile);
}

// オブジェクトファイル出力
void LLVMCodeGen::emitObjectFile() {
    targetManager->emitObjectFile(context->getModule(), options.outputFile);

    if (context->getTargetConfig().target == BuildTarget::Baremetal) {
        std::string ldScript = options.linkerScript.empty() ? "link.ld" : options.linkerScript;
        targetManager->generateLinkerScript(ldScript);
        cm::debug::codegen::log(cm::debug::codegen::Id::LLVMLinkerScript, ldScript);
    }
}

// アセンブリ出力
void LLVMCodeGen::emitAssembly() {
    targetManager->emitAssembly(context->getModule(), options.outputFile);
}

// LLVM IR出力（テキスト）
void LLVMCodeGen::emitLLVMIR() {
    std::error_code EC;
    llvm::raw_fd_ostream out(options.outputFile, EC);
    if (EC) {
        throw std::runtime_error("Cannot open file: " + options.outputFile);
    }
    context->getModule().print(out, nullptr);
}

// ビットコード出力
void LLVMCodeGen::emitBitcode() {
    std::error_code EC;
    llvm::raw_fd_ostream out(options.outputFile, EC);
    if (EC) {
        throw std::runtime_error("Cannot open file: " + options.outputFile);
    }
    llvm::WriteBitcodeToFile(context->getModule(), out);
}

// 実行可能ファイル生成（リンク）
void LLVMCodeGen::emitExecutable() {
    // まずオブジェクトファイル生成
    std::string objFile = options.outputFile + ".o";
    targetManager->emitObjectFile(context->getModule(), objFile);

    // 使用ライブラリの検出
    bool needsGPU = checkForGPUUsage();
    bool needsNet = checkForNetUsage();
    bool needsSync = checkForSyncUsage();
    bool needsThread = checkForThreadUsage();
    bool needsHTTP = checkForHTTPUsage();
    bool needsPthread = needsSync || needsThread;
    bool needsCppRuntime = needsGPU || needsNet || needsSync || needsThread || needsHTTP;

    // リンカ呼び出し
    std::string linkCmd;

    if (context->getTargetConfig().target == BuildTarget::Baremetal) {
        linkCmd = "arm-none-eabi-ld -T link.ld " + objFile + " -o " + options.outputFile;
    } else if (context->getTargetConfig().target == BuildTarget::BaremetalUEFI) {
        linkCmd = "lld-link /subsystem:efi_application /entry:efi_main /out:" + options.outputFile +
                  " " + objFile;
    } else if (context->getTargetConfig().target == BuildTarget::Wasm) {
        std::string runtimePath = findRuntimeLibrary();
        linkCmd = "wasm-ld --entry=_start -z stack-size=1048576 " + objFile + " " + runtimePath +
                  " -o " + options.outputFile;
    } else {
        // ネイティブ：システムリンカ使用
        std::string runtimePath = findRuntimeLibrary();

#ifdef __APPLE__
        const bool needsSanitizerRuntime =
            options.sanitizeAddress || options.sanitizeThread || options.sanitizeMemory;
        std::string linkerDriver = "/usr/bin/clang++";
        if (needsSanitizerRuntime) {
            // サニタイザランタイムはHomebrew LLVMのclang++でリンクする（新しい順に探索）。
            // Apple CLTのランタイムはLLVM計装のバージョン記号（__asan_version_mismatch_check_v8等）を持たずリンクできない。
            // また古いcompiler-rt（LLVM 17）は新しいmacOSで初期化に失敗するため、より新しいLLVMを優先する
            linkerDriver = findSanitizerLinkDriver();
        }
        linkCmd = linkerDriver + " -mmacosx-version-min=15.0 -Wl,-dead_strip ";
#ifdef CM_DEFAULT_TARGET_ARCH
        linkCmd += "-arch " + std::string(CM_DEFAULT_TARGET_ARCH) + " ";
#endif
        if (options.sanitizeAddress) {
            linkCmd += "-fsanitize=address ";
        }
        if (options.sanitizeThread) {
            linkCmd += "-fsanitize=thread ";
        }
        if (options.sanitizeMemory) {
            linkCmd += "-fsanitize=memory ";
        }
        if (context->getTargetConfig().noStd) {
            linkCmd += "-nostdlib ";
        }
        linkCmd += objFile + " " + runtimePath;

        if (needsGPU) {
            std::string gpuRuntimePath = findGPURuntimeLibrary();
            if (!gpuRuntimePath.empty()) {
                linkCmd += " " + gpuRuntimePath;
                linkCmd += " -framework Metal -framework Foundation";
            }
        }

        if (needsNet) {
            std::string netRuntimePath = findStdRuntimeLibrary("net");
            if (!netRuntimePath.empty()) {
                linkCmd += " " + netRuntimePath;
            }
        }

        if (needsSync) {
            std::string syncRuntimePath = findStdRuntimeLibrary("sync");
            if (!syncRuntimePath.empty()) {
                linkCmd += " " + syncRuntimePath;
            }
        }

        if (needsThread) {
            std::string threadRuntimePath = findStdRuntimeLibrary("thread");
            if (!threadRuntimePath.empty()) {
                linkCmd += " " + threadRuntimePath;
            }
        }

        if (needsHTTP) {
            std::string httpRuntimePath = findStdRuntimeLibrary("http");
            if (!httpRuntimePath.empty()) {
                linkCmd += " " + httpRuntimePath;
            }
            std::string opensslPrefix;
#ifdef CM_DEFAULT_TARGET_ARCH
            std::string targetArch = CM_DEFAULT_TARGET_ARCH;
#else
            std::string targetArch = "arm64";
#endif
            if (targetArch == "arm64") {
                if (std::filesystem::exists("/opt/homebrew/opt/openssl@3/lib")) {
                    opensslPrefix = "/opt/homebrew/opt/openssl@3";
                }
            } else {
                if (std::filesystem::exists("/usr/local/opt/openssl@3/lib")) {
                    opensslPrefix = "/usr/local/opt/openssl@3";
                }
            }
            if (opensslPrefix.empty()) {
                FILE* pipe = popen("brew --prefix openssl@3 2>/dev/null", "r");
                if (pipe) {
                    char buffer[256];
                    if (fgets(buffer, sizeof(buffer), pipe)) {
                        opensslPrefix = buffer;
                        while (!opensslPrefix.empty() && opensslPrefix.back() == '\n')
                            opensslPrefix.pop_back();
                    }
                    pclose(pipe);
                }
            }
            if (!opensslPrefix.empty()) {
                linkCmd += " -L" + opensslPrefix + "/lib";
            }
            linkCmd += " -lssl -lcrypto";
        }

        if (needsCppRuntime) {
            linkCmd += " -lc++";
        }

        if (needsPthread) {
            linkCmd += " -lpthread";
        }

        linkCmd += " -o " + options.outputFile;
#else
        linkCmd = "clang -Wl,--gc-sections ";
        if (options.sanitizeAddress) {
            linkCmd += "-fsanitize=address ";
        }
        if (options.sanitizeThread) {
            linkCmd += "-fsanitize=thread ";
        }
        if (options.sanitizeMemory) {
            linkCmd += "-fsanitize=memory ";
        }
        if (context->getTargetConfig().noStd) {
            linkCmd += "-nostdlib ";
        }
        linkCmd += objFile + " " + runtimePath;

        if (needsNet) {
            std::string path = findStdRuntimeLibrary("net");
            if (!path.empty())
                linkCmd += " " + path;
        }
        if (needsSync) {
            std::string path = findStdRuntimeLibrary("sync");
            if (!path.empty())
                linkCmd += " " + path;
        }
        if (needsThread) {
            std::string path = findStdRuntimeLibrary("thread");
            if (!path.empty())
                linkCmd += " " + path;
        }
        if (needsHTTP) {
            std::string path = findStdRuntimeLibrary("http");
            if (!path.empty())
                linkCmd += " " + path;
            linkCmd += " -lssl -lcrypto";
        }
        if (needsCppRuntime)
            linkCmd += " -lstdc++";
        if (needsPthread)
            linkCmd += " -lpthread";

        // LLVMが浮動小数の%をfmod等のlibmコールへ落とすためlibmをリンクする（O0では畳み込まれず残る。macOSはlibSystem同梱のため不要）
        if (!context->getTargetConfig().noStd)
            linkCmd += " -lm";

        linkCmd += " -o " + options.outputFile;
#endif
    }

    // リンカ実行
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMLink, linkCmd);
    auto link_start = std::chrono::steady_clock::now();
    int result = std::system(linkCmd.c_str());
    auto link_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - link_start)
                       .count();
    // リンクはシステムリンカ（clang++/ld）の子プロセス待ちであり、cm自身は計算していない。
    // セキュリティソフトの実行ファイルスキャン等で子プロセスが長時間ブロックされると
    // cmがハングしたように見えるため、閾値超過時に切り分け用の警告を出す
    if (link_ms > 10000) {
        std::cerr << "warning: linker took " << (link_ms / 1000)
                  << "s (cm waits for the system linker; security software scanning new "
                     "binaries can cause this)\n";
    }
    if (result != 0) {
        // R23: wasmのundefined symbolは、native専用FFIモジュール（std::env/fs・native::net/gpu/sync/thread等）を
        // wasmターゲットでコンパイルした場合に発生する（従来は--allow-undefinedで黙って通り、実行時のunknown importで破綻していた）
        if (options.target == BuildTarget::Wasm) {
            throw std::runtime_error(
                "Linking failed\nhint: undefined symbols occur when native-only FFI modules "
                "(std::env/std::fs, native::net/gpu/sync/thread, etc.) are compiled for the wasm "
                "target; these modules are not available on wasm");
        }
        throw std::runtime_error("Linking failed");
    }

    // 一時ファイル削除
    std::remove(objFile.c_str());
}

// ランタイムライブラリのパスを検索
std::string LLVMCodeGen::findRuntimeLibrary() {
    if (context->getTargetConfig().target == BuildTarget::Wasm) {
#ifdef CM_RUNTIME_WASM_PATH
        if (std::filesystem::exists(CM_RUNTIME_WASM_PATH)) {
            return CM_RUNTIME_WASM_PATH;
        }
#endif
        if (std::filesystem::exists("build/lib/cm_runtime_wasm.o")) {
            return "build/lib/cm_runtime_wasm.o";
        }
        return compileWasmRuntimeOnDemand();
    }

#ifdef CM_RUNTIME_PATH
    if (std::filesystem::exists(CM_RUNTIME_PATH)) {
        return CM_RUNTIME_PATH;
    }
#endif

    // ホームディレクトリの~/.cm/lib/も検索（make install対応）
    std::string homeLib;
    if (const char* home = std::getenv("HOME")) {
        homeLib = std::string(home) + "/.cm/lib/cm_runtime.o";
    }
    std::vector<std::string> searchPaths = {
        "build/lib/cm_runtime.o",
        "./build/lib/cm_runtime.o",
        "../build/lib/cm_runtime.o",
        ".tmp/cm_runtime.o",
    };
    if (!homeLib.empty())
        searchPaths.push_back(homeLib);

    for (const auto& path : searchPaths) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }

    return compileRuntimeOnDemand();
}

// ランタイムをオンデマンドでコンパイル
std::string LLVMCodeGen::compileRuntimeOnDemand() {
    std::vector<std::string> sourcePaths = {
        "src/internal/codegen/llvm/native/runtime.c",
        "./src/internal/codegen/llvm/native/runtime.c",
        "../src/internal/codegen/llvm/native/runtime.c",
    };

    std::string runtimeSource;
    for (const auto& path : sourcePaths) {
        if (std::filesystem::exists(path)) {
            runtimeSource = path;
            break;
        }
    }

    if (runtimeSource.empty()) {
        throw std::runtime_error(
            "Cannot find Cm runtime library. "
            "Please rebuild the compiler with 'cmake --build build'");
    }

    std::filesystem::create_directories("build/lib");
    std::string outputPath = "build/lib/cm_runtime.o";

    std::string compileCmd = "clang -c " + runtimeSource + " -o " + outputPath + " -O2";
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMInit, "Compiling runtime: " + compileCmd);

    int result = std::system(compileCmd.c_str());
    if (result != 0) {
        throw std::runtime_error("Failed to compile Cm runtime library");
    }

    return outputPath;
}

// WASMランタイムをオンデマンドでコンパイル
std::string LLVMCodeGen::compileWasmRuntimeOnDemand() {
    std::vector<std::string> sourcePaths = {
        "src/internal/codegen/llvm/wasm/runtime_wasm.c",
        "./src/internal/codegen/llvm/wasm/runtime_wasm.c",
        "../src/internal/codegen/llvm/wasm/runtime_wasm.c",
    };

    std::string runtimeSource;
    for (const auto& path : sourcePaths) {
        if (std::filesystem::exists(path)) {
            runtimeSource = path;
            break;
        }
    }

    if (runtimeSource.empty()) {
        throw std::runtime_error(
            "Cannot find Cm WASM runtime source. "
            "Please rebuild the compiler with 'cmake --build build'");
    }

    std::vector<std::string> clangPaths = {
        "/opt/homebrew/opt/llvm@17/bin/clang",
        "/opt/homebrew/opt/llvm/bin/clang",
        "/usr/local/opt/llvm@17/bin/clang",
        "/usr/local/opt/llvm/bin/clang",
    };

    std::string wasmClang;
    for (const auto& path : clangPaths) {
        if (std::filesystem::exists(path)) {
            wasmClang = path;
            break;
        }
    }

    if (wasmClang.empty()) {
        throw std::runtime_error(
            "Cannot find WASM-capable clang. "
            "Please install LLVM with Homebrew: brew install llvm@17");
    }

    std::string sourceDir = std::filesystem::path(runtimeSource).parent_path().string();

    std::filesystem::create_directories("build/lib");
    std::string outputPath = "build/lib/cm_runtime_wasm.o";

    std::string compileCmd = wasmClang + " -c " + runtimeSource + " -o " + outputPath +
                             " --target=wasm32-wasi -O2 -ffunction-sections -fdata-sections"
                             " -nostdlib -D__wasi__ -I" +
                             sourceDir;
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMInit,
                            "Compiling WASM runtime: " + compileCmd);

    int result = std::system(compileCmd.c_str());
    if (result != 0) {
        throw std::runtime_error("Failed to compile Cm WASM runtime library");
    }

    return outputPath;
}

// GPU関数の使用を検出
bool LLVMCodeGen::checkForGPUUsage() const {
    for (const auto& func : context->getModule()) {
        if (func.isDeclaration()) {
            std::string name = func.getName().str();
            if (name.find("gpu_") == 0) {
                cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimize,
                                        "GPU function detected: " + name);
                return true;
            }
        }
    }
    return false;
}

// Net関数の使用を検出
bool LLVMCodeGen::checkForNetUsage() const {
    for (const auto& func : context->getModule()) {
        if (func.isDeclaration()) {
            std::string name = func.getName().str();
            if (name.find("cm_tcp_") == 0 || name.find("cm_udp_") == 0 ||
                name.find("cm_dns_") == 0 || name.find("cm_socket_") == 0) {
                cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimize,
                                        "Net function detected: " + name);
                return true;
            }
        }
    }
    return false;
}

// Sync関数の使用を検出
bool LLVMCodeGen::checkForSyncUsage() const {
    for (const auto& func : context->getModule()) {
        if (func.isDeclaration()) {
            std::string name = func.getName().str();
            if (name.find("cm_mutex_") == 0 || name.find("cm_rwlock_") == 0 ||
                name.find("cm_atomic_") == 0 || name.find("cm_channel_") == 0 ||
                name.find("cm_once_") == 0 || name.find("atomic_store_") == 0 ||
                name.find("atomic_load_") == 0 || name.find("atomic_fetch_") == 0 ||
                name.find("atomic_compare_") == 0) {
                cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimize,
                                        "Sync function detected: " + name);
                return true;
            }
        }
    }
    return false;
}

// Thread関数の使用を検出
bool LLVMCodeGen::checkForThreadUsage() const {
    for (const auto& func : context->getModule()) {
        if (func.isDeclaration()) {
            std::string name = func.getName().str();
            if (name.find("cm_thread_") == 0) {
                cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimize,
                                        "Thread function detected: " + name);
                return true;
            }
        }
    }
    return false;
}

// HTTP関数の使用を検出
bool LLVMCodeGen::checkForHTTPUsage() const {
    for (const auto& func : context->getModule()) {
        if (func.isDeclaration()) {
            std::string name = func.getName().str();
            if (name.find("cm_http_") == 0) {
                cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimize,
                                        "HTTP function detected: " + name);
                return true;
            }
        }
    }
    return false;
}

// GPUランタイムライブラリのパスを検索
std::string LLVMCodeGen::findGPURuntimeLibrary() {
#ifdef CM_GPU_RUNTIME_PATH
    if (std::filesystem::exists(CM_GPU_RUNTIME_PATH)) {
        return CM_GPU_RUNTIME_PATH;
    }
#endif
    // ホームディレクトリの~/.cm/lib/も検索（make install対応）
    std::string homeLib;
    if (const char* home = std::getenv("HOME")) {
        homeLib = std::string(home) + "/.cm/lib/cm_gpu_runtime.o";
    }
    std::vector<std::string> searchPaths = {
        "build/lib/cm_gpu_runtime.o",
        "./build/lib/cm_gpu_runtime.o",
        "../build/lib/cm_gpu_runtime.o",
    };
    if (!homeLib.empty())
        searchPaths.push_back(homeLib);
    for (const auto& path : searchPaths) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMError, "GPU runtime library not found");
    return "";
}

// stdランタイムライブラリの汎用検索
std::string LLVMCodeGen::findStdRuntimeLibrary(const std::string& name) {
#ifdef CM_NET_RUNTIME_PATH
    if (name == "net" && std::filesystem::exists(CM_NET_RUNTIME_PATH)) {
        return CM_NET_RUNTIME_PATH;
    }
#endif
#ifdef CM_SYNC_RUNTIME_PATH
    if (name == "sync" && std::filesystem::exists(CM_SYNC_RUNTIME_PATH)) {
        return CM_SYNC_RUNTIME_PATH;
    }
#endif
#ifdef CM_THREAD_RUNTIME_PATH
    if (name == "thread" && std::filesystem::exists(CM_THREAD_RUNTIME_PATH)) {
        return CM_THREAD_RUNTIME_PATH;
    }
#endif
#ifdef CM_HTTP_RUNTIME_PATH
    if (name == "http" && std::filesystem::exists(CM_HTTP_RUNTIME_PATH)) {
        return CM_HTTP_RUNTIME_PATH;
    }
#endif

    std::string ext = (name == "sync") ? ".a" : ".o";
    std::string filename = "cm_" + name + "_runtime" + ext;

    // ホームディレクトリの~/.cm/lib/も検索（make install対応）
    std::string homeLib;
    if (const char* home = std::getenv("HOME")) {
        homeLib = std::string(home) + "/.cm/lib/" + filename;
    }
    std::vector<std::string> searchPaths = {
        "build/lib/" + filename,
        "./build/lib/" + filename,
        "../build/lib/" + filename,
    };
    if (!homeLib.empty())
        searchPaths.push_back(homeLib);
    for (const auto& path : searchPaths) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }

    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMError, name + " runtime library not found");
    return "";
}

// インポートされた外部関数があるかチェック
bool LLVMCodeGen::checkForImports() const {
    for (const auto& func : context->getModule()) {
        if (func.isDeclaration()) {
            std::string name = func.getName().str();
            if (name != "printf" && name != "puts" && name != "malloc" && name != "free" &&
                name != "memcpy" && name != "memset" && name != "__cm_panic" &&
                name != "__cm_alloc" && name != "__cm_dealloc" && name.find("llvm.") != 0) {
                cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimize,
                                        "Found imported function: " + name);
                return true;
            }
        }
    }
    return false;
}

}  // namespace cm::codegen::llvm_backend