#include "codegen.hpp"

#include "builtins.hpp"
#include "runtime.hpp"
#include "types.hpp"

#include <algorithm>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>

namespace cm::codegen::js {

using ast::TypeKind;

JSCodeGen::JSCodeGen(const JSCodeGenOptions& options)
    : options_(options), emitter_(options.indentSpaces) {}

void JSCodeGen::compile(const mir::MirProgram& program) {
    // 出力クリア
    emitter_.clear();
    generated_functions_.clear();
    static_vars_.clear();
    function_map_.clear();
    used_runtime_helpers_.clear();

    // ポインタ使用バリデーション（malloc/free/void*検出）
    if (!validatePointerUsage(program)) {
        throw std::runtime_error("JSターゲットで禁止されたポインタ操作が検出されました");
    }

    // 構造体マップ構築
    for (const auto& st : program.structs) {
        if (st) {
            struct_map_[st->name] = st.get();
        }
    }

    // インターフェース名を収集
    for (const auto& iface : program.interfaces) {
        if (iface) {
            interface_names_.insert(iface->name);
        }
    }

    // 関数マップ構築（クロージャキャプチャ用）
    for (const auto& func : program.functions) {
        if (func) {
            function_map_[func->name] = func.get();
        }
    }

    // static変数を収集
    collectStaticVars(program);

    // プリアンブル
    emitPreamble();

    // static変数をグローバルスコープで宣言
    emitStaticVars();

    // 構造体コンストラクタ
    for (const auto& st : program.structs) {
        if (st) {
            emitStruct(*st);
        }
    }

    // vtable（インターフェースディスパッチ用）
    emitVTables(program);

    // 関数
    for (const auto& func : program.functions) {
        if (func && !func->is_extern) {
            emitFunction(*func, program);
        }
    }

    // 生成コードから必要なランタイムヘルパーを抽出
    used_runtime_helpers_ = collectUsedRuntimeHelpers(emitter_.getCode());
    bool needs_web_runtime = emitter_.getCode().find("cm.web.") != std::string::npos;
    expandRuntimeHelperDependencies(used_runtime_helpers_);
    emitRuntime(emitter_, used_runtime_helpers_);
    if (needs_web_runtime) {
        emitWebRuntime(emitter_);
    }

    // ポストアンブル（main呼び出し等）
    emitPostamble(program);

    // ファイル出力
    if (!options_.outputFile.empty()) {
        std::ofstream file(options_.outputFile);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open output file: " + options_.outputFile);
        }
        file << emitter_.getCode();
        file.close();

        if (options_.verbose) {
            std::cout << "Generated: " << options_.outputFile << std::endl;
        }
    }

    // HTMLラッパー生成
    if (options_.generateHTML) {
        std::string htmlFile = options_.outputFile;
        size_t pos = htmlFile.rfind('.');
        if (pos != std::string::npos) {
            htmlFile = htmlFile.substr(0, pos) + ".html";
        } else {
            htmlFile += ".html";
        }

        std::string scriptSrc = options_.outputFile.empty() ? "output.js" : options_.outputFile;
        size_t slash = scriptSrc.find_last_of("/\\");
        if (slash != std::string::npos) {
            scriptSrc = scriptSrc.substr(slash + 1);
        }

        std::ofstream html(htmlFile);
        if (html.is_open()) {
            html << "<!DOCTYPE html>\n";
            html << "<html>\n<head>\n";
            html << "    <meta charset=\"UTF-8\">\n";
            html << "    <title>Cm Application</title>\n";
            html << "</head>\n<body>\n";
            html << "    <script src=\"" << scriptSrc << "\"></script>\n";
            html << "</body>\n</html>\n";
            html.close();

            if (options_.verbose) {
                std::cout << "Generated: " << htmlFile << std::endl;
            }
        }
    }
}

void JSCodeGen::emitPreamble() {
    if (options_.useStrictMode) {
        emitter_.emitLine("\"use strict\";");
        emitter_.emitLine();
    }
}

void JSCodeGen::emitPostamble(const mir::MirProgram& program) {
    emitter_.emitLine();

    // main関数を探して呼び出し
    bool hasMain = false;
    for (const auto& func : program.functions) {
        if (func && func->name == "main") {
            hasMain = true;
            break;
        }
    }

    if (hasMain) {
        emitter_.emitLine("// Entry point");
        if (options_.esModule) {
            emitter_.emitLine("export { main };");
        }
        // main()の戻り値で非ゼロ終了コードに対応
        emitter_.emitLine(
            "const __exit_code = main(); "
            "if (__exit_code && typeof process !== 'undefined') process.exit(__exit_code);");
    }
}

void JSCodeGen::emitVTables(const mir::MirProgram& program) {
    if (program.vtables.empty()) {
        return;
    }

    emitter_.emitLine("// VTables for interface dispatch");

    for (const auto& vtable : program.vtables) {
        if (!vtable)
            continue;

        // すべてのメソッド実装が存在するか確認（ジェネリックテンプレートの除外）
        bool allMethodsExist = true;
        for (const auto& entry : vtable->entries) {
            if (function_map_.find(entry.impl_function_name) == function_map_.end()) {
                allMethodsExist = false;
                break;
            }
        }
        if (!allMethodsExist) {
            continue;
        }

        std::string vtableName = sanitizeIdentifier(vtable->type_name) + "_" +
                                 sanitizeIdentifier(vtable->interface_name) + "_vtable";

        emitter_.emitLine("const " + vtableName + " = {");
        emitter_.increaseIndent();

        for (size_t i = 0; i < vtable->entries.size(); ++i) {
            const auto& entry = vtable->entries[i];
            std::string line = sanitizeIdentifier(entry.method_name) + ": " +
                               sanitizeIdentifier(entry.impl_function_name);
            if (i < vtable->entries.size() - 1) {
                line += ",";
            }
            emitter_.emitLine(line);
        }

        emitter_.decreaseIndent();
        emitter_.emitLine("};");
    }
    emitter_.emitLine();
}

void JSCodeGen::emitStruct(const mir::MirStruct& st) {
    // デフォルト構造体コンストラクタは出力しない
    (void)st;
    return;
}

void JSCodeGen::collectStaticVars(const mir::MirProgram& program) {
    for (const auto& func : program.functions) {
        if (!func || func->is_extern)
            continue;

        for (const auto& local : func->locals) {
            if (local.is_static) {
                std::string globalName = "__static_" + sanitizeIdentifier(func->name) + "_" +
                                         sanitizeIdentifier(local.name);
                std::string defaultVal = local.type ? jsDefaultValue(*local.type) : "null";
                static_vars_[globalName] = defaultVal;
            }
        }
    }
}

void JSCodeGen::emitStaticVars() {
    if (static_vars_.empty())
        return;

    emitter_.emitLine("// Static variables");
    for (const auto& [name, defaultVal] : static_vars_) {
        emitter_.emitLine("let " + name + " = " + defaultVal + ";");
    }
    emitter_.emitLine();
}

}  // namespace cm::codegen::js
