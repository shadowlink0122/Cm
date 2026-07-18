#pragma once
#include <string>

namespace cm {

enum class Target { Native, Wasm, JS, Web, Baremetal, UEFI, SV };

inline Target string_to_target(const std::string& s) {
    if (s == "wasm")
        return Target::Wasm;
    if (s == "js")
        return Target::JS;
    if (s == "web")
        return Target::Web;
    if (s == "baremetal-arm" || s == "baremetal-x86" || s == "bm")
        return Target::Baremetal;
    if (s == "uefi")
        return Target::UEFI;
    if (s == "sv" || s == "verilog" || s == "systemverilog")
        return Target::SV;
    return Target::Native;
}

inline std::string target_to_string(Target t) {
    switch (t) {
        case Target::Wasm:
            return "wasm";
        case Target::JS:
            return "js";
        case Target::Web:
            return "web";
        case Target::Baremetal:
            return "baremetal";
        case Target::UEFI:
            return "uefi";
        case Target::SV:
            return "sv";
        default:
            return "native";
    }
}

// ターゲットのポインタ幅（バイト）。HIR/MIRの型サイズ計算（__sizeof__の定数畳み込み・スライスblob要素サイズ等）が参照する。
// wasm32等の32bitターゲットでは4になり、codegenのDataLayoutと一致させる
inline int& target_pointer_size_ref() {
    static int size = 8;
    return size;
}

inline int target_pointer_size() {
    return target_pointer_size_ref();
}

// コンパイルターゲット決定時に一度だけ呼び出す
inline void set_target_pointer_size(const std::string& target_name) {
    // wasm32とbaremetal-arm（Cortex-M）は32bitポインタ
    if (target_name == "wasm" || target_name == "baremetal-arm" || target_name == "bm") {
        target_pointer_size_ref() = 4;
    } else {
        target_pointer_size_ref() = 8;
    }
}

}  // namespace cm
