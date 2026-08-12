#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cm::ast {

// typeof(式) 型が被演算式を保持するための前方宣言（型チェッカで具体型へ解決する）
struct Expr;

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
    Null,          // null型（ユニオン内でのみ使用可能: int | null）
    Generic,       // <T>
    Error,         // 型エラー（エラー回復用）
    Inferred,      // 型推論待ち
    Union,         // タグ付きユニオン型
    LiteralUnion,  // リテラルユニオン型（"a" | "b" | 100）
    TypeAlias,     // typedef による型エイリアス

    // SV固有型（SystemVerilogターゲットのみで有効）
    Posedge,  // 立ち上がりエッジクロック信号
    Negedge,  // 立ち下がりエッジクロック信号
    Wire,     // wire修飾（組み合わせ出力）
    Reg,      // reg修飾（レジスタ/順序回路出力）
    Bit,      // bit[N] 任意ビット幅型（1-bit単位）
};

// ============================================================
// 型情報
// ============================================================
struct TypeInfo {
    uint32_t size;   // バイト数
    uint32_t align;  // アライメント
};

// プリミティブ型のサイズ情報（実装は types.cpp）
TypeInfo get_primitive_info(TypeKind kind);

// ============================================================
// 型修飾子
// ============================================================
struct TypeQualifiers {
    bool is_const : 1;
    bool is_volatile : 1;
    bool is_mutable : 1;

    TypeQualifiers() : is_const(false), is_volatile(false), is_mutable(false) {}
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

    // typeof(式) 型（kind==Inferred・name=="__typeof__"）の被演算式。型チェッカで具体型へ解決する（従来は破棄していた）
    std::shared_ptr<Expr> typeof_operand;

    // 配列用: パース時にリテラルへ畳めなかった定数サイズ式（int[N+1]・int[N*2]等）。
    // 型チェッカがevaluate_const_exprで畳んでarray_sizeへ確定する（const名を含む算術はスコープが要るため解決を後段化する）
    std::shared_ptr<Expr> size_expr;

    // コンストラクタ
    explicit Type(TypeKind k) : kind(k) {}

    // ヘルパー
    bool is_primitive() const { return kind >= TypeKind::Void && kind <= TypeKind::CString; }

    bool is_integer() const {
        return (kind >= TypeKind::Tiny && kind <= TypeKind::ULong) || kind == TypeKind::ISize ||
               kind == TypeKind::USize;
    }

    // 32ビット整数（int/uint）かどうか判定
    bool is_int32() const { return kind == TypeKind::Int || kind == TypeKind::UInt; }

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

    // サイズ・アライメント情報を取得（実装は types.cpp）
    TypeInfo info() const;

    // 多次元配列かどうか判定
    bool is_multidim_array() const { return kind == TypeKind::Array && dimensions.size() >= 2; }

    // フラット化されたサイズを取得（実装は types.cpp）
    uint32_t get_flattened_size() const;

    // 最終要素型を取得（多次元配列の基底要素型。実装は types.cpp）
    TypePtr get_base_element_type() const;
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
inline TypePtr make_null() {
    return std::make_shared<Type>(TypeKind::Null);
}

// SV固有型ヘルパー
inline TypePtr make_posedge() {
    return std::make_shared<Type>(TypeKind::Posedge);
}
inline TypePtr make_negedge() {
    return std::make_shared<Type>(TypeKind::Negedge);
}
inline TypePtr make_wire(TypePtr elem) {
    auto t = std::make_shared<Type>(TypeKind::Wire);
    t->element_type = std::move(elem);
    return t;
}
inline TypePtr make_reg(TypePtr elem) {
    auto t = std::make_shared<Type>(TypeKind::Reg);
    t->element_type = std::move(elem);
    return t;
}
inline TypePtr make_bit() {
    return std::make_shared<Type>(TypeKind::Bit);
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

// パース時に畳めない定数サイズ式（int[N+1]等）による配列。型チェッカで具体サイズへ畳む
inline TypePtr make_array_with_expr(TypePtr elem, std::shared_ptr<Expr> size_expr) {
    auto t = std::make_shared<Type>(TypeKind::Array);
    t->element_type = std::move(elem);
    t->size_expr = std::move(size_expr);
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
std::string type_to_string(const Type& t);

// ============================================================
// 型パラメータ置換（正準API）
// ============================================================
// 型ツリー内の型パラメータ名を実引数ツリーで置換する（名前の平坦化を行わず構造を保つ）。
// element_type・type_argsを再帰置換し、"Box<T>"表記が名前に残る場合は基底名へ正規化する（type_argsが真実）。
// モノモーフィゼーション・MIRローワの総称フィールド型判定が共有する（フィールド型・要素型の判定は必ず置換後の型で行う）
TypePtr substitute_type_params(const TypePtr& type,
                               const std::unordered_map<std::string, TypePtr>& subst);

}  // namespace cm::ast
