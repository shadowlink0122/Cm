#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cm::ast {

// ============================================================
// 型の種類
// ============================================================
enum class TypeKind {
    // プリミティブ型
    Void,
    Bool,
    Tiny,
    Short,
    Int,
    Long,  // 符号付き整数
    UTiny,
    UShort,
    UInt,
    ULong,  // 符号なし整数
    Float,
    Double,  // 浮動小数点
    Char,
    String,  // 文字/文字列

    // 派生型
    Pointer,    // *T
    Reference,  // &T
    Array,      // [T] or [T; N]

    // ユーザー定義型
    Struct,
    Interface,
    Function,  // (T1, T2) -> R

    // 特殊
    Generic,       // <T>
    Error,         // 型エラー（エラー回復用）
    Inferred,      // 型推論待ち
    Union,         // タグ付きユニオン型
    LiteralUnion,  // リテラルユニオン型（"a" | "b" | 100）
    TypeAlias,     // typedef による型エイリアス
};

// ============================================================
// 型情報
// ============================================================
struct TypeInfo {
    uint32_t size;   // バイト数
    uint32_t align;  // アライメント
};

// プリミティブ型のサイズ情報
inline TypeInfo get_primitive_info(TypeKind kind) {
    switch (kind) {
        case TypeKind::Void:
            return {0, 1};
        case TypeKind::Bool:
            return {1, 1};
        case TypeKind::Tiny:
        case TypeKind::UTiny:
        case TypeKind::Char:
            return {1, 1};
        case TypeKind::Short:
        case TypeKind::UShort:
            return {2, 2};
        case TypeKind::Int:
        case TypeKind::UInt:
        case TypeKind::Float:
            return {4, 4};
        case TypeKind::Long:
        case TypeKind::ULong:
        case TypeKind::Double:
            return {8, 8};
        case TypeKind::Pointer:
        case TypeKind::Reference:
        case TypeKind::String:
            return {8, 8};  // ポインタサイズ
        default:
            return {0, 1};
    }
}

// ============================================================
// 型修飾子
// ============================================================
struct TypeQualifiers {
    bool is_const : 1 = false;
    bool is_volatile : 1 = false;
    bool is_mutable : 1 = false;
};

// ============================================================
// 型表現（前方宣言）
// ============================================================
struct Type;
using TypePtr = std::shared_ptr<Type>;

// ============================================================
// 型ノード
// ============================================================
struct Type {
    TypeKind kind;
    TypeQualifiers qualifiers;

    // 派生型用: Pointer, Reference, Array の要素型
    TypePtr element_type;

    // 配列用: 固定長の場合サイズ
    std::optional<uint32_t> array_size;

    // ユーザー定義型/ジェネリック用: 型名
    std::string name;

    // ジェネリック型引数
    std::vector<TypePtr> type_args;

    // 関数型用: 引数型と戻り値型
    std::vector<TypePtr> param_types;
    TypePtr return_type;

    // コンストラクタ
    explicit Type(TypeKind k) : kind(k) {}

    // ヘルパー
    bool is_primitive() const { return kind >= TypeKind::Void && kind <= TypeKind::String; }

    bool is_integer() const { return (kind >= TypeKind::Tiny && kind <= TypeKind::ULong); }

    bool is_signed() const { return (kind >= TypeKind::Tiny && kind <= TypeKind::Long); }

    bool is_floating() const { return kind == TypeKind::Float || kind == TypeKind::Double; }

    bool is_numeric() const { return is_integer() || is_floating(); }

    bool is_pointer_like() const {
        return kind == TypeKind::Pointer || kind == TypeKind::Reference;
    }

    bool is_error() const { return kind == TypeKind::Error; }

    TypeInfo info() const {
        if (is_primitive() || is_pointer_like()) {
            return get_primitive_info(kind);
        }
        // TODO: 構造体サイズ計算
        return {0, 1};
    }
};

// ============================================================
// 型作成ヘルパー
// ============================================================
inline TypePtr make_void() {
    return std::make_shared<Type>(TypeKind::Void);
}
inline TypePtr make_bool() {
    return std::make_shared<Type>(TypeKind::Bool);
}
inline TypePtr make_int() {
    return std::make_shared<Type>(TypeKind::Int);
}
inline TypePtr make_long() {
    return std::make_shared<Type>(TypeKind::Long);
}
inline TypePtr make_float() {
    return std::make_shared<Type>(TypeKind::Float);
}
inline TypePtr make_double() {
    return std::make_shared<Type>(TypeKind::Double);
}
inline TypePtr make_char() {
    return std::make_shared<Type>(TypeKind::Char);
}
inline TypePtr make_string() {
    return std::make_shared<Type>(TypeKind::String);
}
inline TypePtr make_error() {
    return std::make_shared<Type>(TypeKind::Error);
}

inline TypePtr make_pointer(TypePtr elem) {
    auto t = std::make_shared<Type>(TypeKind::Pointer);
    t->element_type = std::move(elem);
    return t;
}

inline TypePtr make_reference(TypePtr elem) {
    auto t = std::make_shared<Type>(TypeKind::Reference);
    t->element_type = std::move(elem);
    return t;
}

inline TypePtr make_array(TypePtr elem, std::optional<uint32_t> size = std::nullopt) {
    auto t = std::make_shared<Type>(TypeKind::Array);
    t->element_type = std::move(elem);
    t->array_size = size;
    return t;
}

inline TypePtr make_named(const std::string& name) {
    auto t = std::make_shared<Type>(TypeKind::Struct);
    t->name = name;
    return t;
}

// ============================================================
// 型の文字列表現
// ============================================================
inline std::string type_to_string(const Type& t) {
    switch (t.kind) {
        case TypeKind::Void:
            return "void";
        case TypeKind::Bool:
            return "bool";
        case TypeKind::Tiny:
            return "tiny";
        case TypeKind::Short:
            return "short";
        case TypeKind::Int:
            return "int";
        case TypeKind::Long:
            return "long";
        case TypeKind::UTiny:
            return "utiny";
        case TypeKind::UShort:
            return "ushort";
        case TypeKind::UInt:
            return "uint";
        case TypeKind::ULong:
            return "ulong";
        case TypeKind::Float:
            return "float";
        case TypeKind::Double:
            return "double";
        case TypeKind::Char:
            return "char";
        case TypeKind::String:
            return "string";
        case TypeKind::Pointer:
            return "*" + (t.element_type ? type_to_string(*t.element_type) : "?");
        case TypeKind::Reference:
            return "&" + (t.element_type ? type_to_string(*t.element_type) : "?");
        case TypeKind::Array:
            if (t.array_size) {
                return "[" + (t.element_type ? type_to_string(*t.element_type) : "?") + "; " +
                       std::to_string(*t.array_size) + "]";
            }
            return "[" + (t.element_type ? type_to_string(*t.element_type) : "?") + "]";
        case TypeKind::Struct:
        case TypeKind::Interface:
            return t.name;
        case TypeKind::Generic:
            return "<" + t.name + ">";
        case TypeKind::Error:
            return "<error>";
        case TypeKind::Inferred:
            return "<inferred>";
        default:
            return "<unknown>";
    }
}

}  // namespace cm::ast
