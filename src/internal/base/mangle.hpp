#pragma once

#include <string>

// ============================================================
// マングル名の生成規則（C16: 単一シンボルテーブルでの衝突検出の前提）
// ============================================================
// メソッド・ctor/dtor・モジュール修飾名のマングル規則を1箇所に集約する。
// 型検査（衝突検出）とHIR/MIR lowering（シンボル生成）の双方がこのヘルパを使うことで、
// 規則の複製による不一致を防ぐ。

namespace cm::mangle {

// メソッドのマングル名（Type__method）
inline std::string method_name(const std::string& type_name, const std::string& method) {
    return type_name + "__" + method;
}

// コンストラクタのマングル名（Type__ctor / オーバーロードは Type__ctor_N）
inline std::string ctor_name(const std::string& type_name, bool is_overload, size_t param_count) {
    std::string name = type_name + "__ctor";
    if (is_overload) {
        name += "_" + std::to_string(param_count);
    }
    return name;
}

// デストラクタのマングル名（Type__dtor）
inline std::string dtor_name(const std::string& type_name) {
    return type_name + "__dtor";
}

// モジュール修飾名のフラット化（A::b → A__b）。コード生成の '::'→'__' 変換と同一規則
inline std::string flatten_qualified(const std::string& qualified) {
    std::string result = qualified;
    size_t pos = 0;
    while ((pos = result.find("::", pos)) != std::string::npos) {
        result.replace(pos, 2, "__");
        pos += 2;
    }
    return result;
}

}  // namespace cm::mangle
