#include "no_std_checker.hpp"

#include <set>

namespace cm::mir::opt {

NoStdChecker::CheckResult NoStdChecker::check(const MirProgram& program) {
    CheckResult result;

    for (const auto& func : program.functions) {
        if (!func)
            continue;
        checkFunction(*func, result);
    }

    return result;
}

bool NoStdChecker::isForbiddenFunction(const std::string& name) {
    static const std::set<std::string> forbidden = {
        // 標準出力
        "println",
        "__println__",
        "print",
        "__print__",
        "printf",
        "sprintf",
        "puts",
        "putchar",

        // メモリ管理（OS heap依存）
        "malloc",
        "free",
        "calloc",
        "realloc",

        // プロセス制御
        "exit",

        // ファイルI/O
        "open",
        "close",
        "read",
        "write",
        "lseek",
        "fsync",
        "fopen",
        "fclose",
        "fread",
        "fwrite",

        // ネットワーク
        "socket",
        "connect",
        "bind",
        "listen",
        "accept",
        "send",
        "recv",

        // スレッド
        "pthread_create",
        "pthread_join",
    };

    if (forbidden.count(name) > 0)
        return true;

    if (name.find("cm_print") == 0 || name.find("cm_println") == 0 || name.find("cm_file_") == 0 ||
        name.find("cm_read_") == 0 || name.find("cm_io_") == 0) {
        return true;
    }

    return false;
}

std::string NoStdChecker::getErrorMessage(const std::string& funcName, const std::string& callee) {
    std::string category;
    if (callee == "println" || callee == "__println__" || callee == "print" ||
        callee == "__print__" || callee == "printf" || callee == "puts" ||
        callee.find("cm_print") == 0 || callee.find("cm_println") == 0) {
        category = "OS標準出力";
    } else if (callee == "malloc" || callee == "free" || callee == "calloc" ||
               callee == "realloc") {
        category = "OSヒープメモリ管理";
    } else if (callee == "open" || callee == "close" || callee == "read" || callee == "write" ||
               callee == "lseek" || callee.find("cm_file_") == 0 || callee.find("cm_read_") == 0 ||
               callee.find("cm_io_") == 0) {
        category = "ファイルI/O";
    } else if (callee == "exit") {
        category = "プロセス制御";
    } else if (callee == "socket" || callee == "connect" || callee == "bind") {
        category = "ネットワーク";
    } else if (callee.find("pthread_") == 0) {
        category = "スレッド";
    } else {
        category = "OS依存機能";
    }

    return "エラー: 関数 '" + funcName + "' 内で '" + callee + "' を使用しています。" + category +
           " はベアメタル環境では使用できません";
}

void NoStdChecker::checkFunction(const MirFunction& func, CheckResult& result) {
    for (const auto& block : func.basic_blocks) {
        if (!block)
            continue;

        if (block->terminator && block->terminator->kind == MirTerminator::Call) {
            const auto& call_data = std::get<MirTerminator::CallData>(block->terminator->data);

            std::string callee;
            if (call_data.func) {
                if (call_data.func->kind == MirOperand::FunctionRef) {
                    if (const auto* name = std::get_if<std::string>(&call_data.func->data)) {
                        callee = *name;
                    }
                } else if (call_data.func->kind == MirOperand::Constant) {
                    if (const auto* c = std::get_if<MirConstant>(&call_data.func->data)) {
                        if (const auto* s = std::get_if<std::string>(&c->value)) {
                            callee = *s;
                        }
                    }
                }
            }

            if (!callee.empty() && isForbiddenFunction(callee)) {
                result.has_errors = true;
                result.errors.push_back(getErrorMessage(func.name, callee));
            }
        }
    }
}

}  // namespace cm::mir::opt
