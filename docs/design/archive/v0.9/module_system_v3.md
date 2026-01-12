[English](module_system_v3.en.html)

# Cm言語 モジュールシステム v3 設計仕様

## 基本原則

### 1. module文の役割

`module NAME;` 文は、そのファイルがモジュールのエントリーポイントであることを宣言します。

```
math/
├── math.cm         // module math; ← エントリーポイント
├── vector.cm       // 内部実装（mathモジュール内でのみ使用可能）
├── matrix.cm       // 内部実装（mathモジュール内でのみ使用可能）
└── helpers.cm      // 内部実装（mathモジュール内でのみ使用可能）
```

**重要なルール：**
- `module` 文があるファイルがそのディレクトリの公開インターフェース
- 同じディレクトリの他のファイルは内部実装として扱われる
- 外部からは `module` 文で宣言されたモジュールのみアクセス可能

### 2. ディレクトリ構造とモジュールの関係

```
project/
├── main.cm
├── math/
│   ├── math.cm        // module math; ← エントリーポイント
│   ├── vector.cm      // 内部実装
│   └── matrix.cm      // 内部実装
├── graphics/
│   ├── graphics.cm    // module graphics; ← エントリーポイント
│   ├── 2d/
│   │   ├── core2d.cm  // module graphics::2d; ← サブモジュールのエントリー
│   │   ├── shapes.cm  // 内部実装
│   │   └── canvas.cm  // 内部実装
│   └── 3d/
│       ├── core3d.cm  // module graphics::3d; ← サブモジュールのエントリー
│       └── mesh.cm    // 内部実装
└── utils.cm           // module utils; または単一ファイルモジュール
```

### 3. モジュール解決ルール

#### モジュールエントリーポイントの検索順序
1. ディレクトリ内で `module NAME;` を含むファイルを検索
2. 見つからない場合は `mod.cm` を検索（後方互換性）
3. 単一ファイルの場合はファイル名をモジュール名として使用

#### 内部ファイルのアクセス制御
```cm
// math/math.cm
module math;

// 同じディレクトリの他のファイルをインポート（内部使用）
import ./vector;   // math/vector.cm
import ./matrix;   // math/matrix.cm

// 外部に公開するAPI
export {
    // vector.cmから選択的に再エクスポート
    Vector3,
    dot,
    cross,

    // matrix.cmから選択的に再エクスポート
    Matrix4,
    multiply,

    // このファイルで定義
    const float PI = 3.14159;
}
```

```cm
// math/vector.cm
// module宣言なし = 内部実装ファイル

export {  // mathモジュール内でのみ使用可能
    struct Vector3 { float x, y, z; }

    float dot(Vector3 a, Vector3 b) {
        return a.x*b.x + a.y*b.y + a.z*b.z;
    }

    Vector3 cross(Vector3 a, Vector3 b) {
        // ...
    }

    // 内部ヘルパー（mathモジュールでも再エクスポートしない）
    float magnitude(Vector3 v) {
        // ...
    }
}
```

### 3. インポート構文

```cm
// 相対パス
import ./math;           // 同じディレクトリのmathモジュール
import ../utils;         // 親ディレクトリのutilsモジュール
import ./graphics/2d;    // サブディレクトリのモジュール

// 絶対パス（プロジェクトルートから）
import math;             // プロジェクトルートから検索
import graphics::2d;     // ネストされたモジュール
import std::io;          // 標準ライブラリ

// エイリアス
import graphics::2d as G2D;
import ./math/vector as Vec;

// 選択的インポート
import { Vector3, normalize } from math::vector;
import { Point, Rectangle } from graphics::2d::shapes;
```

## エクスポート構文

### 基本的なエクスポート

#### 方法1: インライン定義（ブロック内で定義）
```cm
// math/vector.cm
module math::vector;

export {
    struct Vector3 {
        float x, y, z;
    }

    float length(Vector3 v) {
        return sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
    }
}
```

#### 方法2: 外部定義（名前のみエクスポート）
```cm
// math/vector.cm
module math::vector;

// 実装を先に定義
struct Vector3 {
    float x, y, z;
}

float length(Vector3 v) {
    return sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
}

float normalize(Vector3* v) {
    float len = length(*v);
    v->x /= len; v->y /= len; v->z /= len;
}

// プライベート関数（エクスポートされない）
float internal_helper(float x) {
    return x * x;
}

// 公開APIを明示的に列挙
export {
    Vector3,        // 構造体
    length,         // 関数
    normalize       // 関数
    // internal_helper はエクスポートされない
}
```

### サブ名前空間のエクスポート

#### 方法1: インライン定義
```cm
// graphics/mod.cm
module graphics;

export 2d {
    struct Point { float x, y; }
    struct Rectangle { Point tl, br; }

    float area(Rectangle r) {
        return (r.br.x - r.tl.x) * (r.br.y - r.tl.y);
    }
}
```

#### 方法2: 外部定義 + 名前列挙
```cm
// graphics/mod.cm
module graphics;

// 2D関連の実装
struct Point { float x, y; }
struct Rectangle { Point tl, br; }

float area(Rectangle r) {
    return (r.br.x - r.tl.x) * (r.br.y - r.tl.y);
}

float perimeter(Rectangle r) {
    float w = r.br.x - r.tl.x;
    float h = r.br.y - r.tl.y;
    return 2 * (w + h);
}

// 3D関連の実装
struct Vector3 { float x, y, z; }
struct Mesh { Vector3* vertices; int count; }

void render_mesh(Mesh* m) { /* ... */ }

// トップレベルの実装
void initialize() { /* ... */ }
void cleanup() { /* ... */ }
void internal_setup() { /* プライベート */ }

// 公開APIを整理して定義
export 2d {
    Point,
    Rectangle,
    area,
    perimeter
}

export 3d {
    Vector3,
    Mesh,
    render_mesh
}

// トップレベルのエクスポート
export {
    initialize,
    cleanup
    // internal_setup はエクスポートされない
}
```

#### 方法3: 混合スタイル
```cm
module math;

// 定数は直接exportブロックで定義（シンプルなため）
export {
    const float PI = 3.14159265359;
    const float E = 2.71828182846;
}

// 複雑な関数は外部で実装
struct Complex {
    float real, imag;
}

Complex add(Complex a, Complex b) {
    return Complex{a.real + b.real, a.imag + b.imag};
}

Complex multiply(Complex a, Complex b) {
    return Complex{
        a.real * b.real - a.imag * b.imag,
        a.real * b.imag + a.imag * b.real
    };
}

// ヘルパー関数（プライベート）
float magnitude_squared(Complex c) {
    return c.real * c.real + c.imag * c.imag;
}

// 公開する関数と型を列挙
export {
    Complex,
    add,
    multiply
    // magnitude_squared はプライベート
}
```

使用例：
```cm
import graphics;
graphics::2d::Point p = {10.0, 20.0};
graphics::3d::Vector3 v = {1.0, 2.0, 3.0};
graphics::initialize();
```

### mod.cm での再エクスポート

```cm
// graphics/mod.cm
module graphics;

// サブモジュールの再エクスポート（公開API）
export * from ./2d;  // 2d/mod.cm の全エクスポートを公開
export { Mesh } from ./3d/mesh;  // 選択的に再エクスポート

// または明示的にサブ名前空間として定義
export 2d from ./2d;  // graphics::2d:: としてアクセス可能
export 3d from ./3d;  // graphics::3d:: としてアクセス可能
```

## インポート管理とエラー処理

### 循環依存の検出

```cm
// a.cm
import b;  // エラー: 循環依存を検出

// b.cm
import a;
```

プリプロセッサでの実装：
```cpp
class ImportPreprocessor {
private:
    // インポート済みモジュールのセット（再インポート防止）
    std::unordered_set<std::string> imported_modules;

    // 現在のインポートスタック（循環依存検出）
    std::vector<std::string> import_stack;

    // インポードガード（#pragma once相当）
    std::unordered_map<std::string, std::string> module_cache;

public:
    Result<std::string> import_module(const std::string& module_path) {
        // 正規化されたパスを取得
        auto canonical_path = resolve_module_path(module_path);

        // 既にインポート済みならスキップ
        if (imported_modules.count(canonical_path)) {
            return "";  // 空文字列を返す（再インポート防止）
        }

        // 循環依存チェック
        if (std::find(import_stack.begin(), import_stack.end(),
                     canonical_path) != import_stack.end()) {
            return Error("Circular dependency detected: " +
                        format_import_chain(import_stack, canonical_path));
        }

        // インポート処理
        import_stack.push_back(canonical_path);
        auto content = process_module(canonical_path);
        import_stack.pop_back();

        imported_modules.insert(canonical_path);
        module_cache[canonical_path] = content;

        return content;
    }
};
```

### モジュール衝突の処理

```cm
// エラー例1: 同じ名前を異なるモジュールからインポート
import { Point } from graphics::2d;
import { Point } from math;  // エラー: Point は既に定義されています

// 解決策: エイリアスを使用
import { Point as Point2D } from graphics::2d;
import { Point as MathPoint } from math;

// エラー例2: モジュール名の衝突
import ./utils;
import ../lib/utils;  // エラー: utils モジュールは既に存在

// 解決策: エイリアスを使用
import ./utils as LocalUtils;
import ../lib/utils as LibUtils;
```

## モジュール解決アルゴリズム

```cpp
std::string resolve_module_path(const std::string& import_path,
                               const std::string& current_file) {
    // 1. 相対パス（./ または ../）
    if (starts_with(import_path, "./") || starts_with(import_path, "../")) {
        return resolve_relative_path(current_file, import_path);
    }

    // 2. 標準ライブラリ（std::）
    if (starts_with(import_path, "std::")) {
        return resolve_std_library(import_path);
    }

    // 3. プロジェクトルートから検索
    // プロジェクトルートは以下の優先順位で決定:
    // a) cm.toml があるディレクトリ
    // b) .git があるディレクトリ
    // c) 環境変数 CM_PROJECT_ROOT
    // d) 現在の作業ディレクトリ
    auto project_root = find_project_root(current_file);

    // :: を / に変換してパスを構築
    auto file_path = replace(import_path, "::", "/");

    // 4. 検索順序
    // a) ディレクトリ/mod.cm
    if (exists(project_root / file_path / "mod.cm")) {
        return project_root / file_path / "mod.cm";
    }

    // b) ファイル.cm
    if (exists(project_root / (file_path + ".cm"))) {
        return project_root / (file_path + ".cm");
    }

    // c) CM_MODULE_PATH 環境変数のパス
    for (const auto& path : get_module_paths()) {
        auto full_path = try_resolve_in_path(path, file_path);
        if (full_path) return *full_path;
    }

    throw ModuleNotFoundError(import_path);
}
```

## 実装例

### 完全なプロジェクト構造

```
myproject/
├── cm.toml              # プロジェクト設定ファイル
├── main.cm
├── math/
│   ├── math.cm         # module math; エントリーポイント
│   ├── vector.cm       # 内部実装
│   ├── matrix.cm       # 内部実装
│   └── helpers.cm      # 内部ユーティリティ
└── tests/
    └── math_test.cm
```

**math/math.cm:** （モジュールのエントリーポイント）
```cm
module math;

// 内部実装ファイルをインポート
import ./vector;   // 同じディレクトリのvector.cm
import ./matrix;   // 同じディレクトリのmatrix.cm
import ./helpers;  // 内部ヘルパー

// 公開APIを明確に定義
export {
    // 定数
    const float PI = 3.14159265359;
    const float E = 2.71828182846;
}

// vector.cmから選択的に再エクスポート
export {
    Vector3,        // 構造体
    dot,           // 関数
    cross,         // 関数
    normalize      // 関数
    // magnitude は公開しない（内部使用のみ）
}

// matrix.cmから選択的に再エクスポート
export {
    Matrix4,
    multiply,
    transpose,
    inverse
    // determinant は公開しない（内部使用のみ）
}

// サブ名前空間として公開
export advanced {
    // 高度な数学関数（helpers.cmから）
    quaternion,
    slerp,
    bezier
}
```

**math/vector.cm:** （内部実装）
```cm
// module宣言なし = 内部実装ファイル
// このファイルのexportはmath.cm内でのみ使用可能

struct Vector3 {
    float x, y, z;
}

float dot(Vector3 a, Vector3 b) {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

Vector3 cross(Vector3 a, Vector3 b) {
    return Vector3{
        a.y*b.z - a.z*b.y,
        a.z*b.x - a.x*b.z,
        a.x*b.y - a.y*b.x
    };
}

float magnitude(Vector3 v) {
    return sqrt(dot(v, v));
}

Vector3 normalize(Vector3 v) {
    float m = magnitude(v);
    return Vector3{v.x/m, v.y/m, v.z/m};
}

// mathモジュール内でのみ使用可能
export {
    Vector3,
    dot,
    cross,
    magnitude,  // math.cmで再エクスポートされない
    normalize
}
```

**math/matrix.cm:** （内部実装）
```cm
// module宣言なし = 内部実装ファイル

struct Matrix4 {
    float m[16];
}

Matrix4 multiply(Matrix4 a, Matrix4 b) {
    // 実装...
}

Matrix4 transpose(Matrix4 m) {
    // 実装...
}

float determinant(Matrix4 m) {
    // 実装...
}

Matrix4 inverse(Matrix4 m) {
    float det = determinant(m);  // 内部関数を使用
    // 実装...
}

// mathモジュール内でのみ使用可能
export {
    Matrix4,
    multiply,
    transpose,
    determinant,  // math.cmで再エクスポートされない
    inverse
}
```

**main.cm:**
```cm
// プロジェクトルートからの絶対インポート
import math;
import math::vector as vec;

// または相対インポート
import ./math;
import { Vector3, dot } from ./math/vector;

int main() {
    // 完全修飾名
    math::vector::Vector3 v1 = {1.0, 2.0, 3.0};

    // エイリアス経由
    vec::Vector3 v2 = {4.0, 5.0, 6.0};

    // 選択的インポート
    Vector3 v3 = {7.0, 8.0, 9.0};

    float result = dot(v1, v2);
    float pi = math::PI;

    return 0;
}
```

**tests/math_test.cm:**
```cm
// 親ディレクトリのモジュールをインポート
import ../math;
import ../math/vector;

void test_vector_add() {
    auto v1 = math::vector::Vector3{1.0, 2.0, 3.0};
    auto v2 = math::vector::Vector3{4.0, 5.0, 6.0};
    auto result = math::vector::add(v1, v2);
    assert(result.x == 5.0);
}
```

## プリプロセッサ実装の要点

### エクスポート処理

プリプロセッサは2つのエクスポート形式を処理する必要があります：

1. **インライン定義の処理**
```cpp
// 入力: export { struct Point { float x, y; } }
// 出力: namespace ModuleName { struct Point { float x, y; } }
```

2. **名前列挙の処理**
```cpp
// 入力:
// struct Point { float x, y; }
// export { Point }

// 処理:
// 1. まず全体をパースして定義を収集
// 2. exportブロックで列挙された名前を識別
// 3. 該当する定義を名前空間に移動

// 出力:
// namespace ModuleName {
//     struct Point { float x, y; }  // exportされた定義のみ
// }
// // エクスポートされない定義はグローバルスコープに残る
```

### 実装アルゴリズム

```cpp
class ExportProcessor {
    // 定義を保持する構造体
    struct Definition {
        std::string name;
        std::string type;  // "struct", "function", "const", etc.
        std::string body;
        size_t line_number;
    };

    std::map<std::string, Definition> definitions;
    std::set<std::string> exported_names;

    void process_module(const std::string& source) {
        // Phase 1: 定義を収集
        collect_definitions(source);

        // Phase 2: exportブロックを解析
        parse_export_blocks(source);

        // Phase 3: 出力を生成
        generate_output();
    }

    void parse_export_blocks(const std::string& source) {
        // export { name1, name2, ... } 形式
        if (auto match = find_export_list(source)) {
            for (const auto& name : match.names) {
                exported_names.insert(name);
            }
        }

        // export NS { name1, name2, ... } 形式
        if (auto match = find_namespace_export(source)) {
            namespace_exports[match.ns_name] = match.names;
        }

        // export { ... } インライン定義形式
        if (auto match = find_inline_export(source)) {
            // インライン定義は直接処理
        }
    }
};
```

### その他の要点

1. **インポートガード**: 各モジュールは一度だけ処理される
2. **循環依存検出**: インポートスタックで追跡
3. **パス正規化**: 相対パスを絶対パスに変換
4. **キャッシュ**: 処理済みモジュールをキャッシュ
5. **エラー報告**: 明確なエラーメッセージとインポートチェーン

## メリット

1. **明確な公開API管理**:
   - `module` 文があるファイルがエントリーポイント
   - 公開APIと内部実装の明確な分離
   - 一箇所で全ての公開APIを管理

2. **カプセル化の強化**:
   - 内部実装ファイルは外部から直接アクセス不可
   - モジュール内でのみ使用可能な関数を定義可能
   - 実装の詳細を隠蔽

3. **保守性の向上**:
   - モジュールのエントリーポイントを見れば公開APIが一目瞭然
   - 内部実装を自由に変更してもAPIが保たれる
   - リファクタリングが容易

4. **直感的なディレクトリ構造**:
   - ファイルシステムがモジュール階層を反映
   - 大規模プロジェクトでも整理された構造

5. **柔軟なエクスポート形式**:
   - インライン定義と外部定義の両方をサポート
   - `export NS { }` で簡潔にサブ名前空間を定義
   - 選択的な再エクスポートで細かい制御が可能

6. **堅牢性**:
   - 循環依存の防止
   - 再インポートの自動排除
   - 明確なエラーメッセージ

## この設計により実現される開発フロー

1. **モジュール作成時**:
   - ディレクトリを作成
   - エントリーポイントファイル（例: `math.cm`）に `module math;` を宣言
   - 内部実装を複数ファイルに分割して整理
   - エントリーポイントで公開APIを選択的にエクスポート

2. **モジュール使用時**:
   - `import math;` でモジュールをインポート
   - 公開されたAPIのみ使用可能
   - 内部実装の詳細を意識する必要なし

3. **メンテナンス時**:
   - 内部実装の変更はエントリーポイントに影響しない限り自由
   - 新機能追加時はエントリーポイントで明示的に公開
   - 廃止予定の機能はエントリーポイントから除外するだけ