#include "no_std_checker.hpp"

#include "internal/base/i18n.hpp"

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
    i18n::MsgId category;
    if (callee == "println" || callee == "__println__" || callee == "print" ||
        callee == "__print__" || callee == "printf" || callee == "puts" ||
        callee.find("cm_print") == 0 || callee.find("cm_println") == 0) {
        category = i18n::MsgId::NostdCatOsStdout;
    } else if (callee == "malloc" || callee == "free" || callee == "calloc" ||
               callee == "realloc") {
        category = i18n::MsgId::NostdCatOsHeap;
    } else if (callee == "open" || callee == "close" || callee == "read" || callee == "write" ||
               callee == "lseek" || callee.find("cm_file_") == 0 || callee.find("cm_read_") == 0 ||
               callee.find("cm_io_") == 0) {
        category = i18n::MsgId::NostdCatFileIo;
    } else if (callee == "exit") {
        category = i18n::MsgId::NostdCatProcess;
    } else if (callee == "socket" || callee == "connect" || callee == "bind") {
        category = i18n::MsgId::NostdCatNetwork;
    } else if (callee.find("pthread_") == 0) {
        category = i18n::MsgId::NostdCatThread;
    } else {
        category = i18n::MsgId::NostdCatOsDependent;
    }

    return i18n::msgf(i18n::MsgId::NostdForbiddenCall, funcName, callee, i18n::msg(category));
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
