# Cm言語 モジュールシステム 最終仕様書

**バージョン**: 5.0  
**最終更新**: 2025-12-20  
**ステータス**: 確定

---

## 目次

1. [概要](#概要)
2. [基本原則](#基本原則)
3. [ディレクトリ構造](#ディレクトリ構造)
4. [モジュール宣言](#モジュール宣言)
5. [インポート構文](#インポート構文)
6. [エクスポート構文](#エクスポート構文)
7. [名前空間システム](#名前空間システム)
8. [階層的モジュール](#階層的モジュール)
9. [使用例](#使用例)
10. [実装ロードマップ](#実装ロードマップ)
11. [エラー処理](#エラー処理)
12. [ベストプラクティス](#ベストプラクティス)

---

## 概要

Cm言語のモジュールシステムは、以下の目標を達成します：

- **明確な階層構造**: ディレクトリ構造とモジュール階層の対応
- **柔軟な構成**: 明示的制御と自動検出の選択可能
- **名前空間の保護**: デフォルトで完全修飾名を使用
- **段階的実装**: Phase 1 から Phase 3 へ段階的に機能追加

### 設計方針

1. **`module M;` 宣言が名前空間を定義**
2. **再エクスポートで階層を構築**
3. **ディレクトリ構造でサブモジュールを整理**
4. **デフォルトで完全修飾名（Go言語風）**

---

## 基本原則

### 1. モジュール = 名前空間

```cm
// vector.cm
module vector;  // 'vector' 名前空間を定義

export struct Vector3 { float x, y, z; }
export float dot(Vector3 a, Vector3 b) { /* ... */ }
```

### 2. 再エクスポートで階層構築

```cm
// std.cm
module std;

import ./io;
export { io };  // std::io として公開
```

### 3. ディレクトリ = サブモジュール

```
std/
  ├── std.cm              # module std;
  └── io/
      ├── io.cm           # module io;
      └── file/
          └── file.cm     # module file;
```

---

## ディレクトリ構造

### 推奨構造: 同名ファイル

```
project/
  ├── main.cm
  ├── mylib/
  │   ├── mylib.cm        # module mylib; (エントリーポイント)
  │   ├── core.cm         # module core;
  │   └── utils.cm        # module utils;
  └── std/
      ├── std.cm          # module std;
      ├── io/
      │   ├── io.cm       # module io;
      │   └── file/
      │       └── file.cm # module file;
      └── collections/
          └── collections.cm
```

**ルール**:
- モジュール名と同じディレクトリ名
- ディレクトリ内に同名の `.cm` ファイル（`io/io.cm`）
- または `mod.cm`（Rust風、許容）

### パスの解決順序

```
import ./io;

1. ./io.cm              (同階層のファイル)
2. ./io/io.cm           (サブディレクトリ + 同名) ← 推奨
3. ./io/mod.cm          (サブディレクトリ + mod.cm)
```

---

## モジュール宣言

### 基本形

```cm
// io.cm
module io;

export void println(string s) { /* ... */ }
export void print(string s) { /* ... */ }
```

**ポイント**:
- ファイルの先頭で `module M;` を宣言
- モジュール名は名前空間になる
- 階層的な宣言（`module std::io;`）は**使用しない**

---

## インポート構文

### 1. 基本インポート

```cm
import vector;                  // モジュール全体
import std::io;                 // 階層的モジュール
import std::io::file;           // 深い階層
```

**動作**: デフォルトで完全修飾名が必要

```cm
import vector;

int main() {
    vector::Vector3 v = {1.0, 2.0, 3.0};  // OK
    Vector3 v2;                            // エラー: Vector3 は未定義
    return 0;
}
```

### 2. エイリアス

```cm
import vector as v;
import std::io as io;

int main() {
    v::Vector3 v = {1.0, 2.0, 3.0};
    io::println("Hello!");
    return 0;
}
```

### 3. 選択的インポート

```cm
import vector::{Vector3, dot};
import std::io::{println, print};

int main() {
    Vector3 v = {1.0, 2.0, 3.0};  // プレフィックスなし
    println("Hello!");             // プレフィックスなし
    return 0;
}
```

### 4. ワイルドカードインポート

```cm
import std::*;  // std の全サブモジュール

int main() {
    io::println("Hello!");
    collections::Vec<int> v;
    return 0;
}
```

### 5. 相対パスインポート

```cm
import ./sibling;       // 同じディレクトリ
import ../parent;       // 親ディレクトリ
import ./sub/module;    // サブディレクトリ
```

### 6. 深い階層の直接インポート（Phase 1）

```cm
import ./io/file;       // io/file/file.cm を直接
import ./io/file/ops;   // io/file/ops/ops.cm を直接
```

---

## エクスポート構文

### 1. 宣言時エクスポート（推奨）

```cm
export struct Vector3 { float x, y, z; }
export float dot(Vector3 a, Vector3 b) { /* ... */ }
export const float PI = 3.14159;
```

### 2. 分離エクスポート

```cm
struct Vector3 { float x, y, z; }
float dot(Vector3 a, Vector3 b) { /* ... */ }
const float PI = 3.14159;

export Vector3;
export dot;
export PI;
```

### 3. 一括エクスポート

```cm
export { Vector3, dot, PI };
```

### 4. サブモジュールの再エクスポート

```cm
// std.cm
import ./io;
import ./collections;

export { io };
export { collections };
// または
export { io, collections };
```

### 5. 階層再構築エクスポート（Phase 2）

```cm
// std.cm
import ./io/file;
import ./io/stream;

// file と stream を io の下に配置
export { io::{file, stream} };
```

### 6. ワイルドカード再エクスポート（Phase 3）

```cm
// std.cm
import ./io/**;        // io 以下すべて
export { io::* };      // すべてを階層構造で公開
```

---

## 名前空間システム

### デフォルト: 完全修飾名

```cm
import std::io;
import std::collections;

int main() {
    std::io::println("Hello!");           // 明確
    std::collections::Vec<int> v;         // 名前衝突なし
    return 0;
}
```

### 名前衝突の回避

```cm
// 衝突しない（完全修飾名）
import math_vector;
import physics_vector;

int main() {
    math_vector::Vector3 mv;
    physics_vector::Vector3 pv;  // OK
    return 0;
}
```

```cm
// 衝突する（選択的インポート）
import math_vector::{Vector3};
import physics_vector::{Vector3};  // エラー: Vector3 が重複

int main() {
    Vector3 v;  // どちらの Vector3?
    return 0;
}
```

**解決策**:

```cm
// 方法1: エイリアス
import math_vector::{Vector3 as MathVec3};
import physics_vector::{Vector3 as PhysVec3};

// 方法2: 完全修飾名
import math_vector;
import physics_vector;
math_vector::Vector3 mv;
```

---

## 階層的モジュール

### パターン1: 明示的階層（推奨）

各階層でエクスポートを明示。

```
std/
  ├── std.cm              # import ./io; export { io };
  └── io/
      ├── io.cm           # import ./file; export { file };
      └── file/
          └── file.cm     # module file;
```

```cm
// std/std.cm
module std;
import ./io;
export { io };

// std/io/io.cm
module io;
import ./file;
export void println(string s) { /* ... */ }
export { file };

// std/io/file/file.cm
module file;
export string read(string path) { /* ... */ }
```

**使用**:
```cm
import std::io;
import std::io::file;

int main() {
    std::io::println("Hello!");
    string content = std::io::file::read("test.txt");
    return 0;
}
```

---

### パターン2: 直接インポート + 階層再構築（Phase 2）

中間階層のファイルを省略。

```
std/
  ├── std.cm
  └── io/
      ├── file/
      │   └── file.cm
      └── stream/
          └── stream.cm
```

```cm
// std/std.cm
module std;

import ./io/file;
import ./io/stream;

// 階層を再構築
export { io::{file, stream} };
```

**使用**:
```cm
import std::io::file;
import std::io::stream;

int main() {
    std::io::file::read("test.txt");
    std::io::stream::open("out.txt");
    return 0;
}
```

---

### パターン3: ワイルドカード自動検出（Phase 3）

すべてを自動的に検出。

```cm
// std/std.cm
module std;

import ./io/**;        // io 以下すべて再帰的に
export { io::* };      // 階層構造を維持
```

**使用**:
```cm
import std::io::file;
import std::io::stream;
import std::io::console;  // 自動的に利用可能

int main() {
    std::io::file::read("test.txt");
    std::io::stream::open("out.txt");
    std::io::console::clear();
    return 0;
}
```

---

## 使用例

### 例1: 標準ライブラリの使用

```cm
// main.cm
import std::io;
import std::io::file;

int main() {
    std::io::println("=== ファイル操作 ===");
    
    if (std::io::file::exists("input.txt")) {
        string content = std::io::file::read("input.txt");
        std::io::println("内容: {content}");
        
        std::io::file::write("output.txt", content);
        std::io::println("output.txt に書き込みました");
    } else {
        std::io::eprintln("エラー: input.txt が見つかりません");
    }
    
    return 0;
}
```

### 例2: エイリアスで短縮

```cm
// main.cm
import std::io as io;
import std::io::file as file;

int main() {
    io::println("ファイル処理開始");
    
    if (file::exists("data.txt")) {
        string data = file::read("data.txt");
        io::println("データ: {data}");
    }
    
    return 0;
}
```

### 例3: 選択的インポート

```cm
// main.cm
import std::io::{println, eprintln};
import std::io::file::{read, write, exists};

int main() {
    println("処理開始");
    
    if (exists("config.txt")) {
        string config = read("config.txt");
        println("設定: {config}");
    } else {
        eprintln("警告: config.txt がありません");
        write("config.txt", "default=true");
    }
    
    return 0;
}
```

### 例4: プロジェクトのモジュール構造

```
myproject/
  ├── main.cm
  └── mylib/
      ├── mylib.cm
      ├── parser/
      │   └── parser.cm
      └── lexer/
          └── lexer.cm
```

```cm
// mylib/mylib.cm
module mylib;

import ./parser;
import ./lexer;

export { parser, lexer };
```

```cm
// main.cm
import mylib::parser;
import mylib::lexer;

int main() {
    mylib::parser::parse("input.txt");
    mylib::lexer::tokenize("code");
    return 0;
}
```

---

## 実装ロードマップ

### Phase 1: 基本的な再エクスポート（優先度: 🔴 高）

**目標**: 階層的モジュールシステムの基礎

**実装内容**:
1. `export { M };` 構文のパーサー実装
2. 深い階層の直接インポート（`import ./io/file;`）
3. プリプロセッサでの再エクスポート解決
4. 階層的インポート（`import std::io;`）の処理
5. `namespace` 生成

**テストケース**:
```cm
// io.cm
module io;
export void println(string s) { }

// std.cm
module std;
import ./io;
export { io };

// main.cm
import std::io;
int main() {
    std::io::println("Hello");
    return 0;
}
```

---

### Phase 2: 階層再構築（優先度: 🟡 中）

**目標**: 中間階層を省略可能に

**実装内容**:
1. `export { parent::{child} };` 構文
2. 階層的な名前空間の再構築
3. 深いパスの解決強化

**テストケース**:
```cm
// std.cm
import ./io/file;
import ./io/stream;
export { io::{file, stream} };

// main.cm
import std::io::file;
std::io::file::read("test.txt");
```

---

### Phase 3: ワイルドカード自動検出（優先度: 🟢 低）

**目標**: 自動モジュール検出

**実装内容**:
1. `import ./path/**;` 構文
2. `export { module::* };` 構文
3. ディレクトリの再帰的探索
4. モジュールの自動検出

**テストケース**:
```cm
// std.cm
import ./io/**;
export { io::* };

// main.cm
import std::io::file;
import std::io::stream;
import std::io::console;  // 自動検出
```

---

## エラー処理

### エラー1: モジュールが見つからない

```cm
import ./io;
```

```
エラー: モジュール 'io' が見つかりません
  main.cm:1:8
    import ./io;
           ^~~~

以下を探しましたが見つかりませんでした:
  1. ./io.cm
  2. ./io/io.cm
  3. ./io/mod.cm

修正方法:
  - io/ ディレクトリを作成
  - io/io.cm または io/mod.cm を作成
  - module io; を宣言
```

---

### エラー2: 再エクスポートされていない

```cm
// std.cm
import ./io;
// export { io }; を忘れた
```

```cm
// main.cm
import std::io;  // エラー
```

```
エラー: 'io' は 'std' から再エクスポートされていません
  main.cm:1:8
    import std::io;
           ^~~~~~~

'io' は std.cm でインポートされていますが、再エクスポートされていません。

修正方法: std.cm に以下を追加:
  export { io };
```

---

### エラー3: 名前衝突

```cm
import math_vec::{Vector3};
import phys_vec::{Vector3};  // エラー
```

```
エラー: 'Vector3' が複数のモジュールでインポートされています
  main.cm:2:20
    import phys_vec::{Vector3};
                      ^~~~~~~

'Vector3' は既に 'math_vec' からインポートされています (main.cm:1)

解決方法:
  1. エイリアスを使用:
     import math_vec::{Vector3 as MathVec3};
     import phys_vec::{Vector3 as PhysVec3};

  2. 完全修飾名を使用:
     import math_vec;
     import phys_vec;
     math_vec::Vector3 mv;
     phys_vec::Vector3 pv;
```

---

### エラー4: 循環依存

```cm
// a.cm
import ./b;
export { b };

// b.cm
import ./a;
export { a };
```

```
エラー: 循環依存が検出されました
  a.cm:1:8
    import ./b;
           ^~~~

依存関係:
  a.cm → b.cm → a.cm

モジュール間の循環依存は許可されていません。
```

---

## ベストプラクティス

### 1. ディレクトリ構造

```
✅ 推奨: モジュール名 = ディレクトリ名 = ファイル名
std/
  └── io/
      └── io.cm         # module io;

✅ 許容: mod.cm
std/
  └── io/
      └── mod.cm        # module io;

❌ 非推奨: 混在
std/
  └── io/
      ├── io.cm
      └── mod.cm        # どちらか一方のみ
```

---

### 2. インポートスタイル

```cm
✅ 推奨: 完全修飾名（デフォルト）
import vector;
import std::io;

int main() {
    vector::Vector3 v;
    std::io::println("Hello");
    return 0;
}
```

```cm
✅ 許容: エイリアス（長い名前の場合）
import very_long_module_name as vlmn;
vlmn::Type t;
```

```cm
⚠️ 慎重に: 選択的インポート（局所的に）
import std::io::{println, print};
// 短いスコープでのみ使用
```

```cm
❌ 非推奨: ワイルドカード（テストのみ）
import std::*;
// 名前の由来が不明確
```

---

### 3. エクスポート

```cm
✅ 推奨: 宣言時エクスポート
export struct Vector3 { /* ... */ }
export float dot(Vector3 a, Vector3 b) { /* ... */ }
```

```cm
✅ 許容: コメント付き分離エクスポート
struct Vector3 { /* ... */ }
float dot(Vector3 a, Vector3 b) { /* ... */ }

// ========================================
// 公開API
// ========================================
export { Vector3, dot };
```

---

### 4. サブモジュールの整理

```
✅ 推奨: 関連機能をディレクトリで整理
std/
  └── io/
      ├── io.cm         # 基本機能
      ├── file/         # ファイル関連
      │   └── file.cm
      └── stream/       # ストリーム関連
          └── stream.cm
```

```
❌ 非推奨: フラットに全て配置
std/
  ├── io.cm
  ├── file.cm
  ├── stream.cm
  ├── reader.cm
  └── writer.cm
```

---

### 5. 階層の深さ

```
✅ 推奨: 2-3階層
std::io::file

✅ 許容: 4階層
std::io::file::path

❌ 非推奨: 5階層以上
std::io::file::path::internal::impl
```

---

### 6. 命名規則

- **モジュール名**: `snake_case`（例: `std`, `io`, `collections`）
- **ディレクトリ名**: モジュール名と同じ
- **ファイル名**: `module_name.cm` または `mod.cm`
- **エイリアス**: 短く明確に（例: `io`, `col`, `vec`）

---

## まとめ

### 採用する設計

**再エクスポートベース + ディレクトリ階層 + 柔軟なインポート**

### 主要な特徴

1. **`module M;` = 名前空間**: シンプルで明確
2. **再エクスポートで階層構築**: 柔軟な制御
3. **ディレクトリでサブモジュール整理**: 視覚的に分かりやすい
4. **デフォルトで完全修飾名**: Go言語風、安全
5. **段階的実装**: Phase 1 → Phase 2 → Phase 3

### 次のアクション

1. **Phase 1 の実装**
   - `export { M };` 構文のパーサー
   - 深い階層インポート（`import ./io/file;`）
   - プリプロセッサの拡張
   - テストケースの作成

2. **ドキュメント整備**
   - ユーザーガイド
   - サンプルコード
   - API リファレンス

3. **標準ライブラリの構築**
   - `std::io`
   - `std::collections`
   - `std::math`

---

**この仕様に基づいて実装を開始します！**
