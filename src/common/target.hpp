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

}  // namespace cm
