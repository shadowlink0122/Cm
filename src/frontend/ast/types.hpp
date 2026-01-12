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
    ISize,  // ポインタサイズ符号付き (FFI用)
    USize,  // ポインタサイズ符号なし (FFI用)
    Float,
    Double,  // 浮動小数点
    UFloat,
    UDouble,  // 符号なし浮動小数点（非負制約）
    Char,
    String,   // 文字/文字列
    CString,  // NULL終端文字列 (FFI用)

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
        case TypeKind::UFloat:
            return {4, 4};
        case TypeKind::Long:
        case TypeKind::ULong:
        case TypeKind::Double:
        case TypeKind::UDouble:
            return {8, 8};
        case TypeKind::ISize:
        case TypeKind::USize:
        case TypeKind::Pointer:
        case TypeKind::Reference:
        case TypeKind::String:
        case TypeKind::CString:
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
    // 配列用: 定数パラメータによるサイズ指定（const N: intを使用）
    std::string size_param_name;

    // 多次元配列用: 各次元のサイズ（例: int[10][20] → {10, 20}）
    std::vector<uint32_t> dimensions;

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
    bool is_primitive() const { return kind >= TypeKind::Void && kind <= TypeKind::CString; }

    bool is_integer() const {
        return (kind >= TypeKind::Tiny && kind <= TypeKind::ULong) || kind == TypeKind::ISize ||
               kind == TypeKind::USize;
    }

    bool is_signed() const {
        return (kind >= TypeKind::Tiny && kind <= TypeKind::Long) || kind == TypeKind::ISize;
    }

    bool is_floating() const {
        return kind == TypeKind::Float || kind == TypeKind::Double || kind == TypeKind::UFloat ||
               kind == TypeKind::UDouble;
    }

    bool is_unsigned_float() const { return kind == TypeKind::UFloat || kind == TypeKind::UDouble; }

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

    // 多次元配列かどうか判定
    bool is_multidim_array() const { return kind == TypeKind::Array && dimensions.size() >= 2; }

    // フラット化されたサイズを取得
    uint32_t get_flattened_size() const {
        if (!dimensions.empty()) {
            uint32_t total = 1;
            for (auto d : dimensions) {
                total *= d;
            }
            return total;
        }
        return array_size.value_or(1);
    }

    // 最終要素型を取得（多次元配列の基底要素型）
    TypePtr get_base_element_type() const {
        if (kind != TypeKind::Array)
            return nullptr;
        TypePtr current = element_type;
        while (current && current->kind == TypeKind::Array && current->element_type) {
            current = current->element_type;
        }
        return current;
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
inline TypePtr make_tiny() {
    return std::make_shared<Type>(TypeKind::Tiny);
}
inline TypePtr make_utiny() {
    return std::make_shared<Type>(TypeKind::UTiny);
}
inline TypePtr make_short() {
    return std::make_shared<Type>(TypeKind::Short);
}
inline TypePtr make_ushort() {
    return std::make_shared<Type>(TypeKind::UShort);
}
inline TypePtr make_int() {
    return std::make_shared<Type>(TypeKind::Int);
}
inline TypePtr make_uint() {
    return std::make_shared<Type>(TypeKind::UInt);
}
inline TypePtr make_long() {
    return std::make_shared<Type>(TypeKind::Long);
}
inline TypePtr make_ulong() {
    return std::make_shared<Type>(TypeKind::ULong);
}
inline TypePtr make_isize() {
    return std::make_shared<Type>(TypeKind::ISize);
}
inline TypePtr make_usize() {
    return std::make_shared<Type>(TypeKind::USize);
}
inline TypePtr make_float() {
    return std::make_shared<Type>(TypeKind::Float);
}
inline TypePtr make_double() {
    return std::make_shared<Type>(TypeKind::Double);
}
inline TypePtr make_ufloat() {
    return std::make_shared<Type>(TypeKind::UFloat);
}
inline TypePtr make_udouble() {
    return std::make_shared<Type>(TypeKind::UDouble);
}
inline TypePtr make_char() {
    return std::make_shared<Type>(TypeKind::Char);
}
inline TypePtr make_string() {
    return std::make_shared<Type>(TypeKind::String);
}
inline TypePtr make_cstring() {
    return std::make_shared<Type>(TypeKind::CString);
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

// 定数パラメータによる配列サイズ指定
inline TypePtr make_array_with_param(TypePtr elem, const std::string& param_name) {
    auto t = std::make_shared<Type>(TypeKind::Array);
    t->element_type = std::move(elem);
    t->size_param_name = param_name;
    return t;
}

inline TypePtr make_named(const std::string& name) {
    auto t = std::make_shared<Type>(TypeKind::Struct);
    t->name = name;
    return t;
}

// ジェネリックパラメータ型: T, U等
inline TypePtr make_generic_param(const std::string& name) {
    auto t = std::make_shared<Type>(TypeKind::Generic);
    t->name = name;
    return t;
}

// 関数ポインタ型: int(*)(int, int) -> Function { return_type: int, param_types: [int, int] }
inline TypePtr make_function_ptr(TypePtr return_type, std::vector<TypePtr> param_types) {
    auto t = std::make_shared<Type>(TypeKind::Function);
    t->return_type = std::move(return_type);
    t->param_types = std::move(param_types);
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
        case TypeKind::ISize:
            return "isize";
        case TypeKind::USize:
            return "usize";
        case TypeKind::Float:
            return "float";
        case TypeKind::Double:
            return "double";
        case TypeKind::UFloat:
            return "ufloat";
        case TypeKind::UDouble:
            return "udouble";
        case TypeKind::Char:
            return "char";
        case TypeKind::String:
            return "string";
        case TypeKind::CString:
            return "cstring";
        case TypeKind::Pointer:
            return "*" + (t.element_type ? type_to_string(*t.element_type) : "?");
        case TypeKind::Reference:
            return "&" + (t.element_type ? type_to_string(*t.element_type) : "?");
        case TypeKind::Array:
            if (t.array_size) {
                return (t.element_type ? type_to_string(*t.element_type) : "?") + "[" +
                       std::to_string(*t.array_size) + "]";
            }
            if (!t.size_param_name.empty()) {
                return (t.element_type ? type_to_string(*t.element_type) : "?") + "[" +
                       t.size_param_name + "]";
            }
            return (t.element_type ? type_to_string(*t.element_type) : "?") + "[]";
        case TypeKind::Struct:
        case TypeKind::Interface: {
            std::string result = t.name;
            if (!t.type_args.empty()) {
                result += "<";
                for (size_t i = 0; i < t.type_args.size(); ++i) {
                    if (i > 0)
                        result += ", ";
                    result += type_to_string(*t.type_args[i]);
                }
                result += ">";
            }
            return result;
        }
        case TypeKind::Generic:
            return "<" + t.name + ">";
        case TypeKind::Function: {
            // 関数ポインタ型: int(*)(int, int)
            std::string result = (t.return_type ? type_to_string(*t.return_type) : "void");
            result += "(*)";
            result += "(";
            for (size_t i = 0; i < t.param_types.size(); ++i) {
                if (i > 0)
                    result += ", ";
                result += type_to_string(*t.param_types[i]);
            }
            result += ")";
            return result;
        }
        case TypeKind::Error:
            return "<error>";
        case TypeKind::Inferred:
            return "<inferred>";
        default:
            return "<unknown>";
    }
}

// 型名のマングル化版（Container<int> → Container__int）
inline std::string type_to_mangled_name(const Type& t) {
    switch (t.kind) {
        case TypeKind::Struct:
        case TypeKind::Union:
        case TypeKind::TypeAlias: {
            std::string result = t.name;
            // ジェネリック型引数がある場合
            if (!t.type_args.empty()) {
                for (size_t i = 0; i < t.type_args.size(); ++i) {
                    result += "__" + type_to_mangled_name(*t.type_args[i]);
                }
            }
            return result;
        }
        default:
            // その他の型はtype_to_stringと同じ
            return type_to_string(t);
    }
}

}  // namespace cm::ast
