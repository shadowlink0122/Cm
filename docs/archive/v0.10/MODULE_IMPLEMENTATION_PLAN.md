[English](MODULE_IMPLEMENTATION_PLAN.en.html)

# モジュールシステム実装計画

## 現状分析（2025-12-20）

### ✅ 既に実装されている部分

#### 1. AST構造（`src/frontend/ast/module.hpp`）
- ✅ `ModulePath` - モジュールパス表現
- ✅ `ImportDecl` - import文のAST
- ✅ `ExportDecl` - export文のAST
- ✅ `ModuleDecl` - module宣言のAST
- ✅ 選択的インポート（`::` 構文）
- ✅ ワイルドカードインポート

#### 2. パーサー（`src/frontend/parser/parser_module.cpp`）
- ✅ `module M;` 宣言のパース
- ✅ `import path::to::module;` のパース
- ✅ `import path::*;` のパース
- ✅ `import path::{item1, item2};` のパース
- ✅ `export` キーワードのパース

### ❌ 不足している部分（設計文書と比較）

#### 1. パーサーの拡張

##### A. 相対パスのサポート
```cm
import ./sibling;       // 未実装
import ../parent;       // 未実装
import ./sub/module;    // 未実装
```

**現状**: 絶対パス（`std::io`）のみサポート  
**必要**: `./` と `../` プレフィックスの認識

##### B. 再エクスポート構文
```cm
export { io };                    // 未実装（基本）
export { io, collections };       // 未実装（複数）
export { io as input_output };    // 未実装（エイリアス）
```

**現状**: `export fn foo()` は実装済み  
**必要**: `export { ... }` 構文の完全実装

##### C. 階層再構築（Phase 2）
```cm
export { io::{file, stream} };    // 未実装
```

#### 2. プリプロセッサ（`src/preprocessor/import_preprocessor.cpp`）

##### A. モジュール解決の強化
**必要な機能**:
- [ ] 相対パスの解決（`./`, `../`）
- [ ] ディレクトリ探索（`io/io.cm` または `io/mod.cm`）
- [ ] 深い階層のインポート（`import ./io/file;`）
- [ ] 循環依存の検出強化

##### B. 再エクスポートの処理
**必要な機能**:
- [ ] `export { M }` の検出
- [ ] 再エクスポートされたモジュールの追跡
- [ ] 階層的インポート（`import std::io`）の解決
  - std.cm を読み込む
  - export { io } を発見
  - io.cm を読み込む
  - namespace std { namespace io { ... } } を生成

##### C. 名前空間の生成
**必要な機能**:
- [ ] ネストした namespace の生成
- [ ] モジュール名から名前空間への変換
- [ ] 重複防止

#### 3. モジュール解決器（`src/module/module_resolver.cpp`）

**現状**: 基本的なファイル検索のみ  
**必要な機能**:
- [ ] ディレクトリベースの解決
- [ ] `io/io.cm` と `io/mod.cm` の両方をサポート
- [ ] 再エクスポートのチェーン追跡

## 実装順序（Phase 1）

### ステップ1: パーサーの拡張（優先度: 高）

#### 1.1 相対パスのサポート
**ファイル**: `src/frontend/parser/parser_module.cpp`

```cpp
// 修正箇所
ast::DeclPtr Parser::parse_import_stmt() {
    // ...
    
    // 相対パスのチェック
    bool is_relative = false;
    std::string prefix;
    
    if (consume_if(TokenKind::Dot)) {
        is_relative = true;
        prefix = ".";
        expect(TokenKind::Slash);
        
        // ../ のチェック
        if (consume_if(TokenKind::Dot)) {
            prefix = "..";
            expect(TokenKind::Slash);
        }
    }
    
    // パスの解析
    // ...
}
```

#### 1.2 再エクスポート構文の実装
**ファイル**: `src/frontend/parser/parser_module.cpp`

```cpp
// export { M }; の実装
ast::DeclPtr Parser::parse_export_list() {
    expect(TokenKind::LBrace);
    
    std::vector<ast::ExportItem> items;
    
    do {
        std::string name = expect_ident();
        std::optional<std::string> alias;
        
        // as エイリアス
        if (consume_if(TokenKind::KwAs)) {
            alias = expect_ident();
        }
        
        items.push_back(ast::ExportItem{name, alias});
    } while (consume_if(TokenKind::Comma));
    
    expect(TokenKind::RBrace);
    expect(TokenKind::Semicolon);
    
    return create_export_decl(std::move(items));
}
```

### ステップ2: モジュール解決の強化（優先度: 高）

#### 2.1 ディレクトリ探索の実装
**ファイル**: `src/module/module_resolver.cpp`

```cpp
std::optional<fs::path> resolve_module_path(
    const std::string& specifier,
    const fs::path& current_file
) {
    auto base_dir = current_file.parent_path();
    
    // 相対パスの処理
    if (specifier.starts_with("./") || specifier.starts_with("../")) {
        auto relative_path = parse_relative_path(specifier);
        return resolve_relative_module(base_dir, relative_path);
    }
    
    // 絶対パスの処理
    return resolve_absolute_module(specifier);
}

std::optional<fs::path> resolve_relative_module(
    const fs::path& base_dir,
    const std::vector<std::string>& segments
) {
    fs::path path = base_dir;
    
    // パスを構築
    for (const auto& seg : segments) {
        path /= seg;
    }
    
    // パターン1: path.cm
    if (fs::exists(path.string() + ".cm")) {
        return path.string() + ".cm";
    }
    
    // パターン2: path/path.cm
    if (fs::is_directory(path)) {
        auto dir_name = path.filename().string();
        auto same_name = path / (dir_name + ".cm");
        if (fs::exists(same_name)) {
            return same_name;
        }
        
        // パターン3: path/mod.cm
        auto mod_file = path / "mod.cm";
        if (fs::exists(mod_file)) {
            return mod_file;
        }
    }
    
    return std::nullopt;
}
```

### ステップ3: プリプロセッサの拡張（優先度: 高）

#### 3.1 再エクスポートの処理
**ファイル**: `src/preprocessor/import_preprocessor.cpp`

```cpp
struct ModuleInfo {
    std::string name;
    fs::path file_path;
    std::string source;
    std::vector<std::string> exports;  // 再エクスポートするモジュール
};

class ImportPreprocessor {
private:
    std::unordered_map<std::string, ModuleInfo> loaded_modules;
    
public:
    std::string resolve_hierarchical_import(const std::string& path) {
        // "std::io::file" → ["std", "io", "file"]
        auto segments = split_by_double_colon(path);
        
        // ルートモジュールを読み込み
        auto root = load_module(segments[0]);
        std::string current_namespace = segments[0];
        std::string result;
        
        // 階層を辿る
        for (size_t i = 1; i < segments.size(); ++i) {
            auto sub_name = segments[i];
            
            // 現在のモジュールが sub_name を再エクスポートしているか
            if (!has_export(root, sub_name)) {
                throw ImportError(
                    "Module '" + sub_name + "' is not exported by '" + 
                    current_namespace + "'"
                );
            }
            
            // サブモジュールを読み込み
            root = load_module(sub_name);
            current_namespace += "::" + sub_name;
        }
        
        // 名前空間を生成
        return build_nested_namespace(segments, root.source);
    }
    
    std::string build_nested_namespace(
        const std::vector<std::string>& segments,
        const std::string& source
    ) {
        std::string result;
        
        // namespace を開く
        for (const auto& seg : segments) {
            result += "namespace " + seg + " {\n";
        }
        
        // ソースコードを挿入
        result += source;
        result += "\n";
        
        // namespace を閉じる
        for (size_t i = 0; i < segments.size(); ++i) {
            result += "}\n";
        }
        
        return result;
    }
};
```

### ステップ4: テストケースの作成（優先度: 高）

#### 4.1 基本的な再エクスポート
```cm
// tests/test_programs/modules/basic_reexport/io.cm
module io;
export void println(string s) { }

// tests/test_programs/modules/basic_reexport/std.cm
module std;
import ./io;
export { io };

// tests/test_programs/modules/basic_reexport/main.cm
import std::io;
int main() {
    std::io::println("Hello");
    return 0;
}
```

#### 4.2 深い階層のインポート
```cm
// tests/test_programs/modules/deep_hierarchy/file.cm
module file;
export string read(string path) { return "content"; }

// tests/test_programs/modules/deep_hierarchy/io.cm
module io;
import ./file;
export { file };

// tests/test_programs/modules/deep_hierarchy/std.cm
module std;
import ./io;
export { io };

// tests/test_programs/modules/deep_hierarchy/main.cm
import std::io::file;
int main() {
    string content = std::io::file::read("test.txt");
    return 0;
}
```

## 実装スケジュール

### Week 1: パーサー拡張
- [ ] 相対パスのサポート（1日）
- [ ] 再エクスポート構文（2日）
- [ ] テスト（1日）

### Week 2: モジュール解決
- [ ] ディレクトリ探索（2日）
- [ ] 相対パス解決（1日）
- [ ] テスト（1日）

### Week 3: プリプロセッサ
- [ ] 再エクスポート処理（3日）
- [ ] 階層的インポート解決（2日）
- [ ] 名前空間生成（1日）
- [ ] テスト（2日）

### Week 4: 統合テストとバグ修正
- [ ] 統合テスト（2日）
- [ ] バグ修正（3日）
- [ ] ドキュメント更新（2日）

## 成功基準

### Phase 1 完了の条件
- [ ] `export { M };` 構文が動作
- [ ] `import ./path;` が動作
- [ ] `import std::io;` が動作（再エクスポート経由）
- [ ] 深い階層（`import std::io::file;`）が動作
- [ ] 全テストが通過
- [ ] エラーメッセージが適切

## リスクと対策

### リスク1: 既存コードの破壊
**対策**: 
- 既存のテストを全て実行
- 段階的な実装
- feature flagで新機能を制御

### リスク2: パフォーマンス低下
**対策**:
- モジュールキャッシュの実装
- 不要な再パースの回避

### リスク3: 循環依存の検出漏れ
**対策**:
- 訪問済みモジュールの追跡
- デバッグモードでの詳細ログ

## 次のアクション

1. **今すぐ開始**: パーサーの拡張
   - ファイル: `src/frontend/parser/parser_module.cpp`
   - タスク: 相対パスと `export { M }` のパース

2. **並行作業**: テストケースの準備
   - ディレクトリ: `tests/test_programs/modules/`
   - 基本的なテストケースを先に作成

3. **ドキュメント**: 実装ログの作成
   - 進捗を記録
   - 問題点と解決策を文書化