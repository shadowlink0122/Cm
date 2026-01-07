[English](module_compilation_strategy.en.html)

# Cm言語 モジュール分割コンパイル戦略

## 概要
モジュール単位でのバイナリ化により、効率的なコンパイルとデプロイメントを実現します。

## デッドコード削除の原則

### 1. エクスポートされた要素の保護

```cm
// math/vector.cm
struct Vector3 { float x, y, z; }

float dot(Vector3 a, Vector3 b) {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

// 内部ヘルパー関数
static float magnitude_internal(Vector3 v) {
    return sqrt(dot(v, v));  // dotを使用
}

Vector3 normalize(Vector3 v) {
    float m = magnitude_internal(v);  // 内部関数を使用
    return Vector3{v.x/m, v.y/m, v.z/m};
}

export {
    normalize  // これだけをエクスポート
}
```

**デッドコード削除の動作:**
- `normalize` → エクスポートされているので**保持**
- `magnitude_internal` → `normalize`が使用しているので**保持**
- `dot` → `magnitude_internal`が使用しているので**保持**
- `Vector3` → `normalize`の型として必要なので**保持**

つまり、エクスポートされた要素から到達可能な全てのコードは保持されます。

### 2. ツリーシェイキングの範囲

```cm
// graphics/renderer.cm
module graphics::renderer;

// 使用される関数群
void render_point(Point p) { /* ... */ }
void render_line(Line l) { /* ... */ }
void render_polygon(Polygon p) {
    // render_line を内部で使用
    for (edge in p.edges) {
        render_line(edge);
    }
}

// 未使用の関数群
void render_bezier(Bezier b) { /* ... */ }
void render_nurbs(NURBS n) { /* ... */ }

export {
    render_point,
    render_polygon
    // render_bezier, render_nurbs はエクスポートされない
}
```

**最適化結果:**
- `render_point` → エクスポート、**保持**
- `render_polygon` → エクスポート、**保持**
- `render_line` → `render_polygon`から使用、**保持**
- `render_bezier` → 未使用、**削除**
- `render_nurbs` → 未使用、**削除**

## 分割コンパイル戦略

### 1. モジュール単位のバイナリ生成

```
project/
├── build/
│   ├── modules/
│   │   ├── math.cmo          # Cm Module Object
│   │   ├── math.cmi          # Cm Module Interface
│   │   ├── graphics.cmo
│   │   ├── graphics.cmi
│   │   ├── graphics.2d.cmo   # サブモジュール
│   │   └── graphics.2d.cmi
│   └── bin/
│       └── app               # 最終実行ファイル
```

### 2. モジュールオブジェクト形式 (.cmo)

```
[CMO Header]
- Magic Number: "CMO\x01"
- Version: 1.0.0
- Module Name: "math"
- Dependencies: ["std::math", "std::io"]
- Export Table Hash: SHA256

[Export Table]
- Symbol: Vector3 (type)
- Symbol: dot (function)
- Symbol: normalize (function)

[Symbol Table]
- All internal symbols with offsets

[Code Section]
- LLVM Bitcode or Native Object Code

[Data Section]
- Global variables
- String literals
- Constants

[Debug Info]
- Source maps
- Line numbers
```

### 3. モジュールインターフェース形式 (.cmi)

```
[CMI Header]
- Module Name: "math"
- Version: 1.0.0
- API Version: 1.0  # 互換性チェック用

[Public Declarations]
struct Vector3 { float x, y, z; }
float dot(Vector3 a, Vector3 b);
Vector3 normalize(Vector3 v);

[Type Signatures]
- Mangled names
- ABI information

[Dependency Requirements]
- Required modules and versions
```

## インクリメンタルコンパイル

### 1. 依存関係グラフ

```cm
// cm.toml で依存関係を管理
[modules]
math = { version = "1.0.0" }
graphics = { version = "2.1.0", features = ["2d", "3d"] }
physics = { version = "1.5.0" }

[build]
incremental = true
cache_dir = ".cm-cache"
```

### 2. 変更検出アルゴリズム

```cpp
class ModuleCompiler {
    struct ModuleInfo {
        std::string name;
        std::string source_hash;
        std::string interface_hash;
        std::vector<std::string> dependencies;
        std::time_t last_modified;
    };

    bool needs_recompilation(const ModuleInfo& module) {
        // 1. ソースファイルの変更チェック
        if (source_changed(module)) return true;

        // 2. インターフェースの変更チェック
        if (interface_changed(module)) {
            // 依存モジュールも再コンパイルが必要
            mark_dependents_for_recompilation(module);
            return true;
        }

        // 3. 依存モジュールのインターフェース変更チェック
        for (const auto& dep : module.dependencies) {
            if (dependency_interface_changed(dep)) {
                return true;
            }
        }

        return false;
    }
};
```

### 3. スマートリンキング

```cpp
// リンク時の最適化
class SmartLinker {
    void link_modules(const std::vector<Module>& modules) {
        // 1. 使用されるシンボルのみを収集
        auto used_symbols = collect_used_symbols(modules);

        // 2. 各モジュールから必要な部分のみ抽出
        for (const auto& module : modules) {
            extract_required_symbols(module, used_symbols);
        }

        // 3. Link Time Optimization (LTO)
        if (config.enable_lto) {
            perform_lto(extracted_code);
        }

        // 4. 最終バイナリ生成
        generate_executable();
    }
};
```

## ホットリロード対応

### 1. 動的モジュール更新

```cm
// runtime/module_loader.cm
module runtime::module_loader;

struct ModuleHandle {
    void* handle;
    version: string;
    exports: Map<string, void*>;
}

ModuleHandle load_module(string path) {
    // 1. 新しいモジュールをロード
    auto new_module = dlopen(path);

    // 2. バージョンチェック
    if (!check_compatibility(new_module)) {
        throw IncompatibleModuleError();
    }

    // 3. 既存モジュールとの置き換え
    auto old_module = current_modules[module_name];
    if (old_module) {
        migrate_state(old_module, new_module);
        unload_module(old_module);
    }

    return new_module;
}
```

### 2. ABIバージョニング

```cm
// モジュールのABI定義
#define MODULE_ABI_VERSION 1

// エクスポート時にABI情報を付加
export(abi = MODULE_ABI_VERSION) {
    struct Vector3 { float x, y, z; }
    float dot(Vector3 a, Vector3 b);
}

// インポート時の互換性チェック
import math(min_abi = 1, max_abi = 2);
```

## ビルドシステム統合

### 1. Makefileの例

```makefile
# Cmモジュールビルドルール
MODULES := math graphics physics
MODULE_OBJS := $(MODULES:%=build/modules/%.cmo)

# モジュール個別ビルド
build/modules/%.cmo: src/%/*.cm
	@echo "Building module $*..."
	@cm compile --module $* \
		--output $@ \
		--interface build/modules/$*.cmi \
		--incremental \
		--optimize 2

# 依存関係の自動解決
-include $(MODULES:%=build/deps/%.d)

# 最終リンク
app: $(MODULE_OBJS)
	@cm link $^ -o $@ --lto --gc-sections
```

### 2. ビルドキャッシュ

```
.cm-cache/
├── modules/
│   ├── math/
│   │   ├── 1.0.0/
│   │   │   ├── math.cmo
│   │   │   └── math.cmi
│   │   └── 1.0.1/
│   │       ├── math.cmo
│   │       └── math.cmi
│   └── graphics/
│       └── 2.1.0/
│           ├── graphics.cmo
│           └── graphics.cmi
└── metadata/
    └── dependencies.json
```

## デプロイメント戦略

### 1. モジュール単位の更新

```bash
# 単一モジュールの更新
cm deploy --module math --version 1.0.1

# 差分のみ転送
rsync -av --checksum \
    build/modules/math.cmo \
    server:/app/modules/

# ホットリロード
ssh server "cm reload --module math"
```

### 2. バージョン管理

```json
// deployment.json
{
    "modules": {
        "math": {
            "current": "1.0.0",
            "available": ["1.0.0", "1.0.1"],
            "rollback": "0.9.5"
        },
        "graphics": {
            "current": "2.1.0",
            "available": ["2.0.0", "2.1.0"],
            "features": ["2d", "3d"]
        }
    }
}
```

## パフォーマンス最適化

### 1. プロファイルガイド最適化 (PGO)

```bash
# プロファイル収集
cm run --profile app

# プロファイルベースの再コンパイル
cm compile --pgo-use profile.data \
    --module math \
    --optimize 3

# ホットパスの特定と最適化
cm analyze --profile profile.data \
    --suggest-optimizations
```

### 2. モジュール間最適化

```cm
// hint.cm - 最適化ヒント
#pragma cm_inline_across_modules
#pragma cm_specialize_for(Vector3)

// コンパイラへのヒント
[[hot]] float critical_calculation() { }
[[cold]] void error_handler() { }
```

## メリット

1. **高速な増分ビルド**: 変更されたモジュールのみ再コンパイル
2. **効率的なデプロイ**: 変更モジュールのみ配布
3. **ホットリロード対応**: 実行中のアプリケーションを停止せずに更新
4. **最適なバイナリサイズ**: デッドコード削除とLTOによる最小化
5. **バージョン管理**: モジュール単位でのバージョニングとロールバック
6. **並列ビルド**: モジュール間の依存関係に基づく並列コンパイル

## 今後の展望

1. **分散ビルドシステム**: 複数マシンでの並列ビルド
2. **バイナリキャッシュサーバー**: ビルド済みモジュールの共有
3. **自動依存解決**: パッケージマネージャーとの統合
4. **WebAssemblyモジュール**: ブラウザでの動的ロード対応