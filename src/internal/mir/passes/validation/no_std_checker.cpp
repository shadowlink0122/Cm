#include "no_std_checker.hpp"

#include "internal/base/i18n.hpp"

#include <set>
#include <string>

namespace cm::mir::opt {

NoStdChecker::CheckResult NoStdChecker::check(const MirProgram& program, bool forbid_float) {
    CheckResult result;

    for (const auto& func : program.functions) {
        if (!func)
            continue;
        checkFunction(*func, forbid_float, result);
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

        // ヒープ確保を伴うランタイムヘルパ（個別名）
        "cm_array_to_slice",
    };

    if (forbidden.count(name) > 0)
        return true;

    if (name.find("cm_print") == 0 || name.find("cm_println") == 0 || name.find("cm_file_") == 0 ||
        name.find("cm_read_") == 0 || name.find("cm_io_") == 0) {
        return true;
    }

    // ヒープ確保を伴うランタイムヘルパ群（R18: 従来は漏れており、文字列連結・整形・スライス操作が
    // 無診断でコンパイルを通り実行時クラッシュの温床だった）
    // cm_string_*: 連結・解放等の文字列ランタイム / cm_str_*: cm_str_alloc等の確保ヘルパ
    // cm_format_*: 補間・書式整形（結果文字列を確保する） / cm_slice_*: 動的配列（ヒープ格納）
    // cm_mem_*: std::memのアロケータ経路 / __builtin_array_*: 配列HOF（結果スライスを確保する）
    if (name.find("cm_string_") == 0 || name.find("cm_str_") == 0 || name.find("cm_format_") == 0 ||
        name.find("cm_slice_") == 0 || name.find("cm_mem_") == 0 ||
        name.find("__builtin_array_") == 0) {
        return true;
    }

    // cm_int_to_string / cm_long_to_string 等の数値→文字列変換（結果を確保する）
    if (name.find("cm_") == 0 && name.size() > 10 &&
        name.compare(name.size() - 10, 10, "_to_string") == 0) {
        return true;
    }

    return false;
}

namespace {

// 禁止機能のカテゴリ判定（呼び出し診断とアドレス取得診断で共有）
i18n::MsgId forbidden_category(const std::string& callee) {
    if (callee == "println" || callee == "__println__" || callee == "print" ||
        callee == "__print__" || callee == "printf" || callee == "sprintf" || callee == "puts" ||
        callee == "putchar" || callee.find("cm_print") == 0 || callee.find("cm_println") == 0) {
        return i18n::MsgId::NostdCatOsStdout;
    }
    if (callee == "malloc" || callee == "free" || callee == "calloc" || callee == "realloc" ||
        callee.find("cm_string_") == 0 || callee.find("cm_str_") == 0 ||
        callee.find("cm_format_") == 0 || callee.find("cm_slice_") == 0 ||
        callee.find("cm_mem_") == 0 || callee.find("__builtin_array_") == 0 ||
        callee == "cm_array_to_slice" ||
        (callee.find("cm_") == 0 && callee.size() > 10 &&
         callee.compare(callee.size() - 10, 10, "_to_string") == 0)) {
        return i18n::MsgId::NostdCatOsHeap;
    }
    if (callee == "open" || callee == "close" || callee == "read" || callee == "write" ||
        callee == "lseek" || callee.find("cm_file_") == 0 || callee.find("cm_read_") == 0 ||
        callee.find("cm_io_") == 0) {
        return i18n::MsgId::NostdCatFileIo;
    }
    if (callee == "exit") {
        return i18n::MsgId::NostdCatProcess;
    }
    if (callee == "socket" || callee == "connect" || callee == "bind") {
        return i18n::MsgId::NostdCatNetwork;
    }
    if (callee.find("pthread_") == 0) {
        return i18n::MsgId::NostdCatThread;
    }
    return i18n::MsgId::NostdCatOsDependent;
}

}  // namespace

std::string NoStdChecker::getErrorMessage(const std::string& funcName, const std::string& callee) {
    return i18n::msgf(i18n::MsgId::NostdForbiddenCall, funcName, callee,
                      i18n::msg(forbidden_category(callee)));
}

std::string NoStdChecker::getAddressOfErrorMessage(const std::string& funcName,
                                                   const std::string& callee) {
    return i18n::msgf(i18n::MsgId::NostdForbiddenAddressOf, funcName, callee,
                      i18n::msg(forbidden_category(callee)));
}

void NoStdChecker::checkOperand(const MirFunction& func, const MirOperand* op,
                                CheckResult& result) {
    if (!op || op->kind != MirOperand::FunctionRef) {
        return;
    }
    const auto* name = std::get_if<std::string>(&op->data);
    if (name && isForbiddenFunction(*name)) {
        result.has_errors = true;
        result.errors.push_back(getAddressOfErrorMessage(func.name, *name));
    }
}

void NoStdChecker::checkRvalue(const MirFunction& func, const MirRvalue* rv, CheckResult& result) {
    if (!rv) {
        return;
    }
    switch (rv->kind) {
        case MirRvalue::Use:
            checkOperand(func, std::get<MirRvalue::UseData>(rv->data).operand.get(), result);
            break;
        case MirRvalue::BinaryOp: {
            const auto& d = std::get<MirRvalue::BinaryOpData>(rv->data);
            checkOperand(func, d.lhs.get(), result);
            checkOperand(func, d.rhs.get(), result);
            break;
        }
        case MirRvalue::UnaryOp:
            checkOperand(func, std::get<MirRvalue::UnaryOpData>(rv->data).operand.get(), result);
            break;
        case MirRvalue::Aggregate:
            for (const auto& op : std::get<MirRvalue::AggregateData>(rv->data).operands) {
                checkOperand(func, op.get(), result);
            }
            break;
        case MirRvalue::Cast:
            checkOperand(func, std::get<MirRvalue::CastData>(rv->data).operand.get(), result);
            break;
        case MirRvalue::FormatConvert:
            checkOperand(func, std::get<MirRvalue::FormatConvertData>(rv->data).operand.get(),
                         result);
            break;
        case MirRvalue::Ref:
            break;
    }
}

void NoStdChecker::checkFunction(const MirFunction& func, bool forbid_float, CheckResult& result) {
    // SSE無効ターゲット（baremetal-x86）では浮動小数点型を専用診断で拒否する（R18）。
    // 一時変数もMIRローカルに現れるため、ローカル型の走査で式中のfloat使用まで検出できる
    if (forbid_float) {
        for (const auto& local : func.locals) {
            if (local.type && (local.type->kind == hir::TypeKind::Float ||
                               local.type->kind == hir::TypeKind::Double ||
                               local.type->kind == hir::TypeKind::UFloat ||
                               local.type->kind == hir::TypeKind::UDouble)) {
                result.has_errors = true;
                result.errors.push_back(i18n::msgf(i18n::MsgId::NostdFloatNotAvailable, func.name));
                break;  // 関数につき1件で十分
            }
        }
    }

    for (const auto& block : func.basic_blocks) {
        if (!block)
            continue;

        // 禁止関数のアドレス取得（&putchar等）を文のオペランド走査で検出する（R18: 関数ポインタ
        // 経由の間接呼び出しはcallee名を追えないため、アドレス取得の時点で封じる）
        for (const auto& stmt : block->statements) {
            if (stmt && stmt->kind == MirStatement::Assign) {
                const auto& assign = std::get<MirStatement::AssignData>(stmt->data);
                checkRvalue(func, assign.rvalue.get(), result);
            }
        }

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

            // 引数として禁止関数のアドレスを渡すケース（f(&putchar)）も検出する
            for (const auto& arg : call_data.args) {
                checkOperand(func, arg.get(), result);
            }
        }
    }
}

}  // namespace cm::mir::opt
