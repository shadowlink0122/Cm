# Cmモジュールシステム実装まとめ

## 🎯 実装完了項目

### 1. レクサー拡張
- ✅ 新キーワード追加: `module`, `import`, `export`, `from`, `as`, `use`, `pub`, `macro`
- ✅ トークン定義とマッピング更新

### 2. AST拡張
- ✅ モジュール関連ノード定義 (`src/frontend/ast/module.hpp`)
  - `ModulePath`: モジュールパス表現
  - `ImportDecl`: import文
  - `ExportDecl`: export文
  - `ModuleDecl`: module宣言
  - `MacroDecl`: マクロ定義
  - `UseDecl`: use文
  - `Attribute`: アトリビュート

### 3. 標準ライブラリ構造
```
std/
├── core/          # OS非依存のコア機能
│   └── mod.cm     # Option, Result, 基本トレイト
├── io/            # 入出力
│   ├── mod.cm     # print, println関数
│   └── file.cm    # ファイル操作
├── collections/   # コレクション
│   └── vec.cm     # 動的配列Vec<T>
└── macros/        # 標準マクロ
    └── mod.cm     # assert!, println!, vec!等
```

## 📝 モジュール構文

### Import文
```cm
// 基本インポート
import std.io;

// 選択的インポート
import std.io.{print, println};

// エイリアス
import std.collections.HashMap as Map;

// ワイルドカード（非推奨）
import std.io.*;
```

### Export文
```cm
// 関数のエクスポート
export fn add(a: int, b: int) -> int { ... }

// 型のエクスポート
export struct Point { x: f64, y: f64 }

// リストエクスポート
export { foo, bar, baz };

// 再エクスポート
export { Vec, HashMap } from std.collections;
```

### Module宣言
```cm
module math.geometry;

// モジュール内の定義...
```

## 🔧 マクロシステム

### 関数風マクロ
```cm
@[macro]
fn assert!(condition: expr, msg: string = "") {
    if (!$condition) {
        panic!($msg);
    }
}
```

### アトリビュートマクロ
```cm
@[derive("Debug", "Clone")]
struct Person {
    name: string,
    age: int,
}
```

### プロシージャマクロ
```cm
@[proc_macro]
fn sql!(query: string) -> expr {
    // SQLをコンパイル時に検証
}
```

## 🏗️ アーキテクチャ

### モジュール解決プロセス
1. **インポート解析**: ソースからimport文を抽出
2. **パス解決**: モジュールファイルを検索
   - カレントディレクトリ
   - プロジェクトルート
   - 標準ライブラリ（CM_STD_PATH）
   - 依存ライブラリ（.cm/deps/）
3. **循環依存チェック**: 依存グラフを構築
4. **順次コンパイル**: 依存順にコンパイル
5. **シンボル登録**: エクスポートを名前空間に登録

### 外部関数インターフェース（FFI）
```cm
@[extern("cm_print")]
fn print_impl(s: string) -> void;
```

プラットフォーム別実装:
- **Unix/Linux**: システムコール直接呼び出し
- **Windows**: Win32 API
- **WebAssembly**: WASM imports
- **Rust**: 標準ライブラリバインディング

## 📊 実装状況

### ✅ 完了
- レクサーのキーワード追加
- AST定義（モジュール関連ノード）
- 標準ライブラリ構造設計
- 基本モジュール実装:
  - `std.core`: 基本型とトレイト
  - `std.io`: 入出力関数
  - `std.collections.vec`: 動的配列
  - `std.macros`: 標準マクロ
- サンプルプログラム

### 🚧 未実装
- パーサーのモジュール構文対応
- モジュール解決エンジン
- HIR/MIRでのモジュール表現
- 名前空間管理
- マクロ展開エンジン
- FFIランタイムバインディング

## 📁 サンプルプログラム

### 08_modules.cm
標準ライブラリの使用例:
```cm
import std.io.{println};
import std.collections.Vec;
import std.core.{Option, Result};

fn main() {
    let mut vec = Vec::new();
    vec.push(42);
    println!("Vector length: {}", vec.len());
}
```

### 09_custom_module.cm
カスタムモジュールの定義:
```cm
module math.geometry;

export struct Point { x: f64, y: f64 }
export fn distance(p1: &Point, p2: &Point) -> f64 { ... }
```

### 10_macros.cm
マクロシステムの活用:
```cm
@[macro]
fn time_it!(label: string, code: block) {
    let start = get_time();
    $code
    println!("{}: {}ms", $label, get_time() - start);
}
```

## 🎯 設計方針

1. **明示的な依存関係**: すべての外部依存を明示的にimport
2. **OS機能の分離**: print等のOS機能は外部モジュール化
3. **プラットフォーム独立**: コア言語はOS非依存
4. **遅延バインディング**: 実行環境で適切な実装を選択
5. **衛生的マクロ**: 名前衝突を避ける設計

## 🚀 次のステップ

1. パーサーを拡張してimport/export構文をサポート
2. モジュール解決エンジンの実装
3. HIRでモジュール情報を保持
4. 名前空間とシンボルテーブルの実装
5. マクロ展開パスの実装
6. FFIランタイムの実装

これにより、Cm言語は完全にモジュール化され、OS依存機能を外部ライブラリとして分離した、ポータブルな言語となります。