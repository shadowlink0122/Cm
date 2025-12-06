#pragma once

#include <string>
#include <vector>
#include <optional>

namespace cm::ast {

// 前方宣言
struct Expr;
struct Stmt;
struct Decl;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;
using DeclPtr = std::unique_ptr<Decl>;

// ============================================================
// モジュールパス (e.g., std.io.print)
// ============================================================
struct ModulePath {
    std::vector<std::string> segments;  // ["std", "io", "print"]

    std::string to_string() const {
        std::string result;
        for (size_t i = 0; i < segments.size(); ++i) {
            if (i > 0) result += ".";
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
    std::string name;                    // インポート名
    std::optional<std::string> alias;    // エイリアス（as句）

    ImportItem(std::string n, std::optional<std::string> a = std::nullopt)
        : name(std::move(n)), alias(std::move(a)) {}
};

// ============================================================
// Import宣言
// ============================================================
struct ImportDecl {
    ModulePath path;                      // モジュールパス
    std::vector<ImportItem> items;        // 選択的インポート項目
    bool is_wildcard = false;             // ワイルドカード（*）インポート

    ImportDecl(ModulePath p) : path(std::move(p)) {}
};

// ============================================================
// Export項目
// ============================================================
struct ExportItem {
    std::string name;                     // エクスポート名
    std::optional<ModulePath> from_module;  // 再エクスポート元

    ExportItem(std::string n, std::optional<ModulePath> from = std::nullopt)
        : name(std::move(n)), from_module(std::move(from)) {}
};

// ============================================================
// Export宣言
// ============================================================
struct ExportDecl {
    enum Kind {
        Declaration,    // export fn foo() { ... }
        List,          // export { foo, bar }
        ReExport,      // export { foo, bar } from module
        WildcardReExport  // export * from module
    };

    Kind kind;
    std::vector<ExportItem> items;        // エクスポート項目
    DeclPtr declaration;                   // 直接エクスポートする宣言
    std::optional<ModulePath> from_module;  // 再エクスポート元

    // 宣言の直接エクスポート
    ExportDecl(DeclPtr decl)
        : kind(Declaration), declaration(std::move(decl)) {}

    // リストエクスポート
    ExportDecl(std::vector<ExportItem> items_)
        : kind(List), items(std::move(items_)) {}

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
    ModulePath path;                      // モジュールパス
    std::vector<DeclPtr> declarations;    // モジュール内の宣言

    ModuleDecl(ModulePath p) : path(std::move(p)) {}
};

// ============================================================
// マクロ呼び出し
// ============================================================
struct MacroCall {
    std::string name;                     // マクロ名
    std::vector<ExprPtr> args;            // 引数
    bool is_bang = false;                 // ! マクロ (e.g., assert!)

    MacroCall(std::string n, std::vector<ExprPtr> a, bool bang = false)
        : name(std::move(n)), args(std::move(a)), is_bang(bang) {}
};

// ============================================================
// アトリビュート
// ============================================================
struct AttributeNode {  // Attributeという名前が衝突する可能性があるため変更
    std::string name;                     // アトリビュート名
    std::vector<std::string> args;        // 引数

    AttributeNode(std::string n) : name(std::move(n)) {}
    AttributeNode(std::string n, std::vector<std::string> a)
        : name(std::move(n)), args(std::move(a)) {}
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
        Function,      // 関数風マクロ
        Attribute,     // アトリビュートマクロ
        Procedural     // プロシージャマクロ
    };

    Kind kind;
    std::string name;
    std::vector<MacroParam> params;            // マクロパラメータ
    std::vector<StmtPtr> body;                 // マクロ本体
    std::vector<AttributeNode> attributes;     // マクロ属性

    MacroDecl(Kind k, std::string n, std::vector<MacroParam> p, std::vector<StmtPtr> b)
        : kind(k), name(std::move(n)), params(std::move(p)), body(std::move(b)) {}
};

// ============================================================
// Use文（Rustスタイルのインポート）
// ============================================================
struct UseDecl {
    ModulePath path;                      // モジュールパス
    std::optional<std::string> alias;     // エイリアス
    bool is_pub = false;                  // pub use

    UseDecl(ModulePath p, std::optional<std::string> a = std::nullopt)
        : path(std::move(p)), alias(std::move(a)) {}
};

}  // namespace cm::ast