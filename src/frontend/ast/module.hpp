#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cm::ast {

// 前方宣言
struct Expr;
struct Stmt;
struct Decl;
struct Type;  // FFI関数宣言用

// ============================================================
// アトリビュート
// ============================================================
struct AttributeNode {  // Attributeという名前が衝突する可能性があるため変更
    std::string name;   // アトリビュート名
    std::vector<std::string> args;  // 引数

    AttributeNode(std::string n) : name(std::move(n)) {}
    AttributeNode(std::string n, std::vector<std::string> a)
        : name(std::move(n)), args(std::move(a)) {}
};
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;
using DeclPtr = std::unique_ptr<Decl>;
using TypePtr = std::shared_ptr<Type>;  // FFI関数宣言用

// ============================================================
// モジュールパス (e.g., std.io.print)
// ============================================================
struct ModulePath {
    std::vector<std::string> segments;  // ["std", "io", "print"]

    std::string to_string() const {
        std::string result;
        for (size_t i = 0; i < segments.size(); ++i) {
            if (i > 0)
                result += "::";
            result += segments[i];
        }
        return result;
    }

    ModulePath() = default;
    ModulePath(std::vector<std::string> segs) : segments(std::move(segs)) {}
};

// ============================================================
// Import項目
// ============================================================
struct ImportItem {
    std::string name;                  // インポート名
    std::optional<std::string> alias;  // エイリアス（as句）

    ImportItem(std::string n, std::optional<std::string> a = std::nullopt)
        : name(std::move(n)), alias(std::move(a)) {}
};

// ============================================================
// Import宣言
// ============================================================
struct ImportDecl {
    ModulePath path;                // モジュールパス
    std::vector<ImportItem> items;  // 選択的インポート項目
    bool is_wildcard = false;       // ワイルドカード（*）インポート
    std::vector<AttributeNode> attributes;

    ImportDecl(ModulePath p) : path(std::move(p)) {}
};

// ============================================================
// Export項目
// ============================================================
struct ExportItem {
    std::string name;                       // エクスポート名
    std::optional<ModulePath> from_module;  // 再エクスポート元
    std::optional<ModulePath> namespace_path;  // 階層的再エクスポート用パス (e.g., io::file)

    ExportItem(std::string n, std::optional<ModulePath> from = std::nullopt)
        : name(std::move(n)), from_module(std::move(from)) {}

    // 階層的再エクスポート用コンストラクタ
    ExportItem(std::string n, ModulePath ns_path, std::optional<ModulePath> from = std::nullopt)
        : name(std::move(n)), from_module(std::move(from)), namespace_path(std::move(ns_path)) {}
};

// ============================================================
// Export宣言
// ============================================================
struct ExportDecl {
    enum Kind {
        Declaration,      // export fn foo() { ... }
        List,             // export { foo, bar }
        ReExport,         // export { foo, bar } from module
        WildcardReExport  // export * from module
    };

    Kind kind;
    std::vector<ExportItem> items;          // エクスポート項目
    DeclPtr declaration;                    // 直接エクスポートする宣言
    std::optional<ModulePath> from_module;  // 再エクスポート元

    // 宣言の直接エクスポート
    ExportDecl(DeclPtr decl) : kind(Declaration), declaration(std::move(decl)) {}

    // リストエクスポート
    ExportDecl(std::vector<ExportItem> items_) : kind(List), items(std::move(items_)) {}

    // 再エクスポート
    ExportDecl(std::vector<ExportItem> items_, ModulePath from)
        : kind(ReExport), items(std::move(items_)), from_module(std::move(from)) {}

    // ワイルドカード再エクスポート
    static ExportDecl wildcard_from(ModulePath from) {
        ExportDecl decl(std::vector<ExportItem>{});
        decl.kind = WildcardReExport;
        decl.from_module = std::move(from);
        return decl;
    }
};

// ============================================================
// Module宣言
// ============================================================
struct ModuleDecl {
    ModulePath path;                    // モジュールパス
    std::vector<DeclPtr> declarations;  // モジュール内の宣言

    ModuleDecl(ModulePath p) : path(std::move(p)) {}
};

// ============================================================
// マクロ呼び出し
// ============================================================
struct MacroCall {
    std::string name;           // マクロ名
    std::vector<ExprPtr> args;  // 引数
    bool is_bang = false;       // ! マクロ (e.g., assert!)

    MacroCall(std::string n, std::vector<ExprPtr> a, bool bang = false)
        : name(std::move(n)), args(std::move(a)), is_bang(bang) {}
};

// ============================================================
// マクロ定義
// ============================================================
struct MacroParam {
    std::string name;
    std::string type_hint;  // 型ヒント（オプション）
    bool is_variadic = false;

    MacroParam(std::string n, std::string t = "", bool var = false)
        : name(std::move(n)), type_hint(std::move(t)), is_variadic(var) {}
};

struct MacroDecl {
    enum Kind {
        Function,   // 関数風マクロ
        Attribute,  // アトリビュートマクロ
        Procedural  // プロシージャマクロ
    };

    Kind kind;
    std::string name;
    std::vector<MacroParam> params;         // マクロパラメータ
    std::vector<StmtPtr> body;              // マクロ本体
    std::vector<AttributeNode> attributes;  // マクロ属性

    MacroDecl(Kind k, std::string n, std::vector<MacroParam> p, std::vector<StmtPtr> b)
        : kind(k), name(std::move(n)), params(std::move(p)), body(std::move(b)) {}
};

// ============================================================
// FFI関数宣言（use libc { ... }用）
// ============================================================
struct FFIFunctionDecl {
    std::string name;                                     // 関数名
    TypePtr return_type;                                  // 戻り値型
    std::vector<std::pair<std::string, TypePtr>> params;  // パラメータ
    bool is_variadic = false;                             // 可変引数（...）

    FFIFunctionDecl() = default;
    FFIFunctionDecl(std::string n) : name(std::move(n)) {}
};

// ============================================================
// Use文（FFI/モジュールインポート）
// ============================================================
struct UseDecl {
    enum Kind {
        ModuleUse,  // use std::io; (モジュールエイリアス)
        FFIUse      // use libc { ... }; (FFI宣言)
    };

    Kind kind = ModuleUse;
    ModulePath path;           // ライブラリ/モジュールパス
    std::string package_name;  // 文字列ベースのパッケージ名 (e.g., "axios", "@scope/pkg")
    std::optional<std::string> alias;        // エイリアス（as句）
    bool is_pub = false;                     // pub use
    std::vector<FFIFunctionDecl> ffi_funcs;  // FFI関数宣言（FFIUseの場合）

    // アトリビュート
    bool is_static_link = false;                  // #[static]
    bool is_framework = false;                    // #[framework] (macOS)
    std::optional<std::string> os_condition;      // #[os(linux)] など
    std::optional<std::string> target_condition;  // #[target(wasm)] など
    std::vector<AttributeNode> attributes;

    // モジュールuseコンストラクタ
    UseDecl(ModulePath p, std::optional<std::string> a = std::nullopt)
        : kind(ModuleUse), path(std::move(p)), alias(std::move(a)) {}

    // パッケージuseコンストラクタ (文字列)
    UseDecl(std::string pkg, std::optional<std::string> a = std::nullopt)
        : kind(ModuleUse), package_name(std::move(pkg)), alias(std::move(a)) {}

    // FFI useコンストラクタ (ModulePath)
    UseDecl(ModulePath p, std::vector<FFIFunctionDecl> funcs,
            std::optional<std::string> a = std::nullopt)
        : kind(FFIUse), path(std::move(p)), alias(std::move(a)), ffi_funcs(std::move(funcs)) {}

    // FFI useコンストラクタ (文字列)
    UseDecl(std::string pkg, std::vector<FFIFunctionDecl> funcs,
            std::optional<std::string> a = std::nullopt)
        : kind(FFIUse),
          package_name(std::move(pkg)),
          alias(std::move(a)),
          ffi_funcs(std::move(funcs)) {}
};

}  // namespace cm::ast
