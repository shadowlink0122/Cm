# Cm言語 モジュールシステム v4 設計仕様

## 概要とv3からの変更点

### 主要な改善点
1. **明示的なトップレベルエクスポートのみ** - インラインエクスポートを廃止
2. **const宣言の正しい処理** - const宣言をトップレベルで適切に扱う
3. **2段階インポート戦略** - プリプロセッサと構文解析後の処理を組み合わせ
4. **効率的なパース戦略** - 必要な部分のみパースして無駄を削減

## 基本原則

### 1. モジュール宣言とエクスポート

**v4の重要な変更：エクスポートは常に明示的、2つのスタイルをサポート**

**重要な保証：** `export`された宣言は、同じファイル内では通常の宣言と同様に自由にアクセス可能です。`export`は外部への公開を示すマーカーであり、ローカルスコープには影響しません。

#### スタイル1: 宣言時エクスポート（推奨）
```cm
// math/vector.cm
module math::vector;

// エクスポートする宣言には export を付ける
export struct Vector3 {
    float x, y, z;
}

export float dot(Vector3 a, Vector3 b) {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

// 内部関数（export なし）
float magnitude(Vector3 v) {
    return sqrt(dot(v, v));
}

// 同じファイル内の使用例
export float normalize(Vector3* v) {
    // エクスポートされたdot関数も、されていないmagnitude関数も
    // 同じファイル内では自由に使用可能
    float mag = magnitude(*v);  // OK: 内部関数を使用
    if (mag > 0.0) {
        v->x /= mag;
        v->y /= mag;
        v->z /= mag;
    }
    return mag;
}

// Vector3構造体もエクスポート済みだが、ファイル内では普通に使える
Vector3 internal_temp = {1.0, 2.0, 3.0};  // OK
```

#### スタイル2: 分離エクスポート
```cm
// math/vector.cm
module math::vector;

// トップレベル宣言
struct Vector3 {
    float x, y, z;
}

float dot(Vector3 a, Vector3 b) {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

float magnitude(Vector3 v) {
    return sqrt(dot(v, v));
}

// 後でまとめてエクスポート
export Vector3;
export dot;
// magnitude はエクスポートされない
```

両方のスタイルを混在させることも可能です。

### 2. const宣言の正しい処理

**v3の問題点：** const宣言がexportブロック内で初期化されていた
**v4の解決策：** constは常にトップレベルで宣言、エクスポートは宣言時または別途指定

```cm
// math/constants.cm
module math::constants;

// スタイル1: 宣言時エクスポート
export const float PI = 3.14159265359;
export const float E = 2.71828182846;
export const float PHI = 1.61803398875;  // 黄金比

// プライベート定数（export なし）
const float INTERNAL_EPSILON = 0.000001;
```

または

```cm
// スタイル2: 分離エクスポート
const float PI = 3.14159265359;
const float E = 2.71828182846;
const float PHI = 1.61803398875;
const float INTERNAL_EPSILON = 0.000001;

// 公開する定数を後で指定
export PI, E, PHI;
// INTERNAL_EPSILONはエクスポートされない
```

### 3. エクスポート構文の統一

#### スタイル1: 宣言時エクスポート（推奨）
```cm
// エクスポートする宣言にはexportを付ける
export typedef Vector3 Point3D;
export struct Matrix4 { float m[16]; }
export int global_counter = 0;
export const float GRAVITY = 9.81;

export float add(float a, float b) { return a + b; }
export overload int add(int a, int b) { return a + b; }

// 内部用（exportなし）
struct InternalCache { /* ... */ }
float internal_helper() { /* ... */ }
```

#### スタイル2: 分離エクスポート
```cm
// すべての宣言はトップレベル
typedef Vector3 Point3D;
struct Matrix4 { float m[16]; }
int global_counter = 0;
const float GRAVITY = 9.81;

float add(float a, float b) { return a + b; }
overload int add(int a, int b) { return a + b; }

// エクスポートは名前のみを列挙（1つずつ、または複数）
export Vector3;
export Matrix4, Point3D;
export add;  // オーバーロードされた関数はすべてのバージョンがエクスポートされる
export global_counter;
export GRAVITY;

// エクスポートの別名（オプション）
export Vector3 as Vec3;
export add as sum;
```

両方のスタイルを混在可能：
```cm
// 重要な型は宣言時にエクスポート
export struct Point { float x, y; }
export interface Drawable { void draw(); }

// 関数は後でまとめてエクスポート
float distance(Point a, Point b) { /* ... */ }
float area(Point p1, Point p2, Point p3) { /* ... */ }

export distance, area;
```

### 4. implブロックのエクスポート

v4では、implブロックも明示的にエクスポートする必要があります。これにより、内部用のトレイト実装と公開用の実装を明確に区別できます。

#### スタイル1: 宣言時エクスポート
```cm
export struct Vector3 { float x, y, z; }

export interface Eq {
    bool equals(Self other);
}

interface Debug {
    void debug_print();
}

// 公開用impl（宣言時にexport）
export impl Eq for Vector3 {
    bool equals(Vector3 other) {
        return x == other.x && y == other.y && z == other.z;
    }
}

// 内部用impl（exportなし）
impl Debug for Vector3 {
    void debug_print() {
        // デバッグ出力（内部用）
    }
}
```

#### スタイル2: 分離エクスポート
```cm
// 型とインターフェースの定義
struct Vector3 { float x, y, z; }

interface Eq {
    bool equals(Self other);
}

interface Debug {
    void debug_print();
}

// トレイト実装
impl Eq for Vector3 {
    bool equals(Vector3 other) {
        return x == other.x && y == other.y && z == other.z;
    }
}

impl Debug for Vector3 {
    void debug_print() {
        // デバッグ出力（内部用）
    }
}

// エクスポート宣言
export Vector3;
export impl Eq for Vector3;  // Eqトレイトの実装のみエクスポート
// Debug実装はエクスポートされない（内部用）
```

#### 複数impl の一括エクスポート

同じ型に対する複数のトレイト実装をまとめてエクスポートする構文もサポートします：

```cm
struct Point { float x, y; }

impl Eq for Point { /* ... */ }
impl Ord for Point { /* ... */ }
impl Hash for Point { /* ... */ }
impl Debug for Point { /* ... */ }  // 内部用

// 複数のimplを一括エクスポート
export Point;
export impl for Point {
    Eq,
    Ord,
    Hash
    // Debugはエクスポートしない
};

// または個別にエクスポート
export impl Eq for Point;
export impl Ord for Point;
export impl Hash for Point;
```

## スコープと可視性ルール

### ファイル内スコープ
1. **エクスポートはスコープに影響しない**: `export`キーワードは外部公開のマーカーであり、ファイル内での可視性には影響しません
2. **すべての宣言は同一ファイル内で利用可能**: エクスポートの有無に関わらず、同じファイル内の全宣言は自由に相互参照可能
3. **前方参照も可能**: 宣言の順序に関わらず、同一ファイル内の任意の宣言を参照可能

### モジュール間の可視性
1. **明示的エクスポートのみ公開**: `export`された宣言のみが他のモジュールから参照可能
2. **内部実装の隠蔽**: `export`されていない宣言は完全に隠蔽され、外部からアクセス不可
3. **選択的インポート**: インポート側で必要な宣言のみを選択的に取り込み可能

### デッドコード除去ルール
1. **エクスポートされた宣言の依存は保護**: エクスポートされた関数が内部関数を使用している場合、その内部関数はデッドコード除去されません
2. **推移的な依存も保持**: エクスポートされた宣言から直接・間接的に参照されるすべてのコードは保持されます
3. **完全に未使用のコードのみ除去**: どこからも参照されない（エクスポートされた宣言からも到達不可能な）コードのみがデッドコード除去の対象となります

### 例：スコープとデッドコード除去
```cm
// math/advanced.cm
module math::advanced;

// 内部ヘルパー関数（エクスポートなし）
float internal_clamp(float x, float min, float max) {
    return x < min ? min : (x > max ? max : x);
}

// internal_clampに依存する別の内部関数
float safe_log(float x) {
    float clamped = internal_clamp(x, 0.0001, 1000000.0);
    return log(clamped);
}

// エクスポートされる関数
export float sigmoid(float x) {
    // internal_clamp は sigmoid から使用されているため、
    // デッドコード除去されない
    float clamped = internal_clamp(x, -10.0, 10.0);
    return 1.0 / (1.0 + exp(-clamped));
}

// エクスポートされる関数から内部関数を呼ぶ
export float log_sigmoid(float x) {
    // safe_log と internal_clamp の両方が保持される
    return safe_log(sigmoid(x));
}

// 完全に未使用の内部関数（デッドコード除去の対象）
float unused_helper() {
    return 42.0;  // どこからも参照されない → 除去される
}

// デバッグ用関数（エクスポートされていない）
void debug_print(float x) {
    // sigmoidから呼ばれていないため、除去される
    println("Debug: {x}");
}

// 他のモジュールから:
// import math::advanced::{sigmoid, log_sigmoid};  // OK
// import math::advanced::internal_clamp;          // エラー: エクスポートされていない
// import math::advanced::safe_log;                 // エラー: エクスポートされていない
```

### コンパイラの動作
1. `sigmoid` と `log_sigmoid` はエクスポートされているため、エントリポイントとして扱われる
2. `internal_clamp` は `sigmoid` から使用されているため、デッドコード除去されない
3. `safe_log` は `log_sigmoid` から使用されているため、保持される
4. `unused_helper` と `debug_print` はどこからも参照されないため、最適化時に除去される

## インポート処理戦略

### ハイブリッドアプローチ（推奨）

v4では、効率性と正確性のバランスを取るため、2段階のハイブリッドアプローチを採用します：

#### 第1段階：プリプロセッサ（軽量解析）
```cpp
class ModulePreprocessor {
    // 第1段階：エクスポートシグネチャのみを抽出
    struct ExportSignature {
        std::string name;
        std::string type;  // "function", "struct", "const", etc.
        std::string signature;  // 関数の場合はシグネチャ、型の場合は定義の概要
        int line_number;
    };

    // モジュールのエクスポートを軽量解析で抽出
    std::vector<ExportSignature> extract_exports(const std::string& module_path) {
        // トークン化と軽量パースのみ
        // - export文を探す
        // - エクスポートされた名前を収集
        // - 対応する宣言のシグネチャを抽出
        // 完全なASTは構築しない
    }

    // 選択的インポートのフィルタリング
    std::string filter_module(
        const std::string& module_source,
        const std::vector<std::string>& import_items
    ) {
        // エクスポートされた項目のみを含むソースを生成
        // 依存関係も考慮（型定義など）
    }
};
```

#### 第2段階：構文解析後の処理
```cpp
class ModuleResolver {
    // 第2段階：完全な型チェックと解決
    void resolve_imports(ast::TranslationUnit& unit) {
        for (auto& import_decl : unit.imports) {
            // 1. プリプロセッサで処理済みのシグネチャを取得
            auto signatures = preprocessor.get_cached_exports(import_decl.module);

            // 2. 必要な項目のみを完全にパース
            if (import_decl.is_selective()) {
                // 選択的インポート：必要な項目のみパース
                parse_selected_items(import_decl.items);
            } else {
                // 全インポート：すべてのエクスポートをパース
                parse_all_exports(signatures);
            }

            // 3. 型チェックと名前解決
            resolve_types_and_names(import_decl);
        }
    }
};
```

### 効率化戦略

#### 1. インクリメンタルパース
```cm
// モジュールのエクスポートシグネチャをキャッシュ
// math.cm.exports というファイルに保存
{
    "exports": [
        {"name": "Vector3", "type": "struct", "signature": "struct { float x, y, z; }"},
        {"name": "dot", "type": "function", "signature": "float(Vector3, Vector3)"},
        {"name": "PI", "type": "const", "signature": "float", "value": "3.14159265359"}
    ],
    "timestamp": "2025-01-01T00:00:00Z",
    "hash": "sha256:..."
}
```

#### 2. 遅延評価
```cpp
// 実際に使用されるまでパースを遅延
class LazyModule {
    std::optional<ast::Module> cached_ast;

    const ast::Module& get_ast() {
        if (!cached_ast) {
            cached_ast = parse_module(module_path);
        }
        return *cached_ast;
    }
};
```

## モジュール構造

### ディレクトリ構成
```
project/
├── cm.toml
├── main.cm
├── math/
│   ├── math.cm         # module math;
│   ├── vector.cm       # module math::vector; (サブモジュール)
│   ├── matrix.cm       # module math::matrix; (サブモジュール)
│   └── internal.cm     # 内部実装（module宣言なし）
└── utils/
    └── utils.cm        # module utils;
```

### モジュールエントリーポイント

```cm
// math/math.cm
module math;

// サブモジュールのインポート
import ./vector;   // math/vector.cm
import ./matrix;   // math/matrix.cm
import ./internal; // 内部実装

// 定数の定義
const float PI = 3.14159265359;
const float E = 2.71828182846;

// 関数の定義
float clamp(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

// エクスポート宣言
export PI, E;
export clamp;

// サブモジュールからの再エクスポート
export Vector3 from ./vector;
export Matrix4 from ./matrix;
export dot, cross from ./vector;
export multiply, inverse from ./matrix;
```

## インポート構文

### エクスポート構文の形式的定義

```
// スタイル1: 宣言時エクスポート
exported_decl ::=
    | 'export' function_decl           // export float add(float a, float b) { ... }
    | 'export' struct_decl              // export struct Point { ... }
    | 'export' interface_decl           // export interface Drawable { ... }
    | 'export' impl_decl                // export impl Eq for Point { ... }
    | 'export' const_decl               // export const float PI = 3.14;
    | 'export' typedef_decl             // export typedef Vector3 Point3D;
    | 'export' enum_decl                // export enum Color { ... }

// スタイル2: 分離エクスポート
export_stmt ::=
    | 'export' name_list ';'                          // 名前のエクスポート
    | 'export' name_list 'from' module_path ';'       // 再エクスポート
    | 'export' '*' 'from' module_path ';'             // ワイルドカード再エクスポート
    | 'export' name 'as' alias ';'                    // エイリアス付きエクスポート
    | 'export' 'impl' interface 'for' type ';'        // 単一implエクスポート
    | 'export' 'impl' 'for' type '{' interface_list '}' ';'  // 複数implエクスポート

name_list ::= name (',' name)*
interface_list ::= interface (',' interface)*
```

### 基本インポート
```cm
import math;                        // モジュール全体
import math::vector;                // サブモジュール
import std::io;                     // 標準ライブラリ
```

### 選択的インポート（Rust風）
```cm
import math::{PI, E, clamp};                    // 複数項目
import math::vector::{Vector3, dot, cross};     // サブモジュールから
import std::io::{println, eprintln};            // 標準ライブラリから
```

### エイリアス
```cm
import math as m;                               // モジュールエイリアス
import math::vector as vec;                     // サブモジュールエイリアス
import math::{PI as PI_VALUE, E as EULER};     // 項目エイリアス
import math::vector::{Vector3 as Vec3};         // 型エイリアス
```

### 相対インポート
```cm
import ./sibling;                   // 同じディレクトリ
import ../parent;                    // 親ディレクトリ
import ./subdir/module;              // サブディレクトリ
```

## 実装詳細

### プリプロセッサ実装

```cpp
class ImportPreprocessor {
private:
    // モジュールキャッシュ
    struct ModuleCache {
        std::string source;
        std::vector<ExportInfo> exports;
        std::chrono::system_clock::time_point timestamp;
        std::string hash;
    };
    std::map<std::string, ModuleCache> cache;

    // 循環依存検出
    std::vector<std::string> import_stack;
    std::set<std::string> imported_modules;

public:
    // モジュールのエクスポートを抽出（軽量解析）
    std::vector<ExportInfo> extract_exports(const std::string& module_path) {
        std::vector<ExportInfo> exports;
        auto source = read_file(module_path);

        // トークン単位で解析
        Lexer lexer(source);
        Token tok;

        // トップレベル宣言を収集
        std::map<std::string, Declaration> declarations;

        while ((tok = lexer.next()).kind != TokenKind::Eof) {
            // export文を探す
            if (tok.kind == TokenKind::Export) {
                // export NAME; または export NAME, NAME2;
                do {
                    tok = lexer.next();
                    if (tok.kind == TokenKind::Ident) {
                        std::string name = tok.text;

                        // from句の処理
                        if (lexer.peek().text == "from") {
                            lexer.next(); // consume "from"
                            std::string from_module = lexer.next().text;
                            exports.push_back({name, "re-export", from_module});
                        } else {
                            // 通常のエクスポート
                            if (declarations.count(name)) {
                                exports.push_back({name, declarations[name].type, ""});
                            }
                        }
                    }
                } while (lexer.peek().kind == TokenKind::Comma && lexer.next());

                expect(lexer.next(), TokenKind::Semicolon);
            }
            // 宣言を記録（struct, typedef, const, function）
            else if (is_declaration_start(tok)) {
                auto decl = parse_declaration(lexer, tok);
                declarations[decl.name] = decl;
            }
        }

        return exports;
    }

    // 選択的インポートのフィルタリング
    std::string process_selective_import(
        const std::string& module_path,
        const std::vector<std::string>& items,
        const std::map<std::string, std::string>& aliases
    ) {
        auto source = read_file(module_path);
        auto exports = extract_exports(module_path);

        // 要求された項目が存在するか確認
        for (const auto& item : items) {
            bool found = false;
            for (const auto& exp : exports) {
                if (exp.name == item) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                throw ImportError("Item '" + item + "' not exported from module");
            }
        }

        // 必要な宣言のみを抽出
        return filter_declarations(source, items, aliases);
    }
};
```

### パーサー統合

```cpp
// parser_module.cpp
std::unique_ptr<ast::ImportDecl> Parser::parse_import() {
    expect(TokenKind::Import);
    auto import_decl = std::make_unique<ast::ImportDecl>();

    // モジュールパス
    import_decl->module_path = parse_module_path();

    // 選択的インポート
    if (consume_if(TokenKind::ColonColon)) {
        expect(TokenKind::LBrace);
        do {
            std::string name = expect_ident();
            std::optional<std::string> alias;

            // as エイリアス
            if (check_ident("as")) {
                advance();
                alias = expect_ident();
            }

            import_decl->items.push_back({name, alias});
        } while (consume_if(TokenKind::Comma));
        expect(TokenKind::RBrace);
    }
    // モジュール全体のエイリアス
    else if (check_ident("as")) {
        advance();
        import_decl->module_alias = expect_ident();
    }

    expect(TokenKind::Semicolon);
    return import_decl;
}

// エクスポート宣言のパース
std::unique_ptr<ast::ExportDecl> Parser::parse_export() {
    expect(TokenKind::Export);
    auto export_decl = std::make_unique<ast::ExportDecl>();

    // export NAME1, NAME2 from MODULE;
    // export NAME1, NAME2;
    do {
        export_decl->names.push_back(expect_ident());
    } while (consume_if(TokenKind::Comma));

    // from句（再エクスポート）
    if (check_ident("from")) {
        advance();
        export_decl->from_module = parse_module_path();
    }

    expect(TokenKind::Semicolon);
    return export_decl;
}
```

## エラー処理

### 明確なエラーメッセージ

```cm
// エラー例1: エクスポートされていない項目のインポート
import math::{Vector4};
// Error: 'Vector4' is not exported from module 'math'
// Available exports: Vector3, Matrix4, PI, E, clamp

// エラー例2: 循環依存
// a.cm: import ./b;
// b.cm: import ./a;
// Error: Circular dependency detected:
//   a.cm → b.cm → a.cm

// エラー例3: 重複する名前
import math::{Vector3};
import physics::{Vector3};
// Error: 'Vector3' is already imported from 'math'
// Use an alias to resolve the conflict:
//   import physics::{Vector3 as PhysicsVector3};
```

## 最適化とキャッシュ

### モジュールキャッシュ戦略

```cpp
class ModuleCache {
private:
    struct CacheEntry {
        std::string source_hash;
        std::vector<ExportInfo> exports;
        std::optional<ast::Module> parsed_ast;
        std::chrono::system_clock::time_point last_modified;
    };

    std::map<std::string, CacheEntry> cache;

public:
    // エクスポート情報のみを取得（高速）
    std::vector<ExportInfo> get_exports(const std::string& module_path) {
        auto& entry = cache[module_path];

        // キャッシュが有効か確認
        if (is_cache_valid(entry, module_path)) {
            return entry.exports;
        }

        // 再解析（軽量）
        entry.exports = extract_exports(module_path);
        entry.source_hash = compute_hash(module_path);
        entry.last_modified = get_file_mtime(module_path);

        return entry.exports;
    }

    // 完全なASTを取得（必要時のみ）
    const ast::Module& get_ast(const std::string& module_path) {
        auto& entry = cache[module_path];

        if (!entry.parsed_ast) {
            entry.parsed_ast = parse_module(module_path);
        }

        return *entry.parsed_ast;
    }
};
```

## 移行ガイド（v3からv4へ）

### 1. インラインエクスポートの廃止

```cm
// v3（非推奨）
export {
    struct Vector3 { float x, y, z; }
    float dot(Vector3 a, Vector3 b) { return /*...*/; }
}

// v4（推奨）
struct Vector3 { float x, y, z; }
float dot(Vector3 a, Vector3 b) { return /*...*/; }
export Vector3, dot;
```

### 2. const宣言の修正

```cm
// v3（誤り）
export {
    const float PI = 3.14159;  // exportブロック内での初期化
}

// v4（正しい）
const float PI = 3.14159;  // トップレベルで宣言
export PI;                  // 名前のみエクスポート
```

### 3. 選択的インポートの活用

```cm
// v3
import math;
// すべてがインポートされる

// v4
import math::{Vector3, dot, PI};  // 必要なものだけ
// パース量とメモリ使用量を削減
```

## パフォーマンス比較

### プリプロセッサのみ（v3）
- 利点：シンプルな実装
- 欠点：構文エラーの検出が困難、不完全な型情報

### 完全パース後の処理のみ
- 利点：完全な型チェック、正確なエラー検出
- 欠点：大量のパース（使用しない宣言も含む）

### ハイブリッド方式（v4）
- 利点：
  - 第1段階で高速なエクスポート抽出
  - 第2段階で必要な部分のみ完全パース
  - エクスポートキャッシュによる再利用
  - 選択的インポートでパース量削減
- 欠点：実装の複雑性（ただし保守性は向上）

### ベンチマーク想定値
```
1000ファイルのプロジェクト、各ファイル平均10エクスポート：

v3（プリプロセッサのみ）:
- 初回: 500ms
- キャッシュあり: 50ms

完全パース:
- 初回: 3000ms
- キャッシュあり: 100ms

v4（ハイブリッド）:
- 初回: 800ms（第1段階: 300ms + 第2段階: 500ms）
- キャッシュあり: 60ms
- 選択的インポート使用時: 400ms（50%削減）
```

## まとめ

v4モジュールシステムは、以下の目標を達成します：

1. **明示性** - すべてのエクスポートが明示的でトップレベル
2. **正確性** - const宣言を含むすべての宣言を正しく処理
3. **効率性** - ハイブリッドアプローチで必要最小限のパース
4. **保守性** - シンプルで一貫性のある構文
5. **互換性** - Rust風の選択的インポートをサポート

この設計により、大規模プロジェクトでもスケーラブルで保守しやすいモジュールシステムを実現します。