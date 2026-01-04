# Cmモジュールシステム設計

## 概要

Cm言語のモジュールシステムは、OS依存の機能を外部モジュールとして分離し、ポータブルなコア言語を実現します。

## 設計原則

1. **明示的な依存関係**: すべての外部依存は`import`で明示
2. **名前空間の分離**: モジュールごとに独立した名前空間
3. **遅延バインディング**: 実行環境に応じて適切な実装を選択
4. **マクロによる拡張**: コンパイル時のメタプログラミング

## モジュール構文

### 1. Import文

```cm
// 単一インポート
import std.io;

// 選択的インポート
import std.io.{print, println, eprint};

// エイリアス付きインポート
import std.collections.HashMap as Map;

// ワイルドカードインポート（非推奨）
import std.io.*;

// マクロのインポート
import std.macros.{derive, test};
```

### 2. Export文

```cm
// 関数のエクスポート
export fn add(a: int, b: int) -> int {
    return a + b;
}

// 型のエクスポート
export struct Point {
    x: float,
    y: float,
}

// 再エクスポート
export { Vec, HashMap } from std.collections;

// モジュール全体の再エクスポート
export * from std.io;
```

### 3. モジュール定義

```cm
// ファイル: std/io.cm
module std.io;

// 外部関数宣言（実装はランタイムが提供）
@[extern("cm_print")]
fn print_impl(s: string) -> void;

// パブリックAPI
export fn print(s: string) -> void {
    print_impl(s);
}

export fn println(s: string) -> void {
    print_impl(s);
    print_impl("\n");
}
```

## マクロシステム

### 1. マクロ定義

```cm
// 関数風マクロ
@[macro]
export fn assert!(condition: expr, message: string = "") {
    if (!$condition) {
        panic!("Assertion failed: " + $message);
    }
}

// アトリビュートマクロ
@[macro]
export fn derive(traits: string...) -> fn(type: Type) -> void {
    // 型に対してトレイトを自動実装
    for trait in traits {
        implement_trait($type, trait);
    }
}

// プロシージャマクロ
@[proc_macro]
export fn sql!(query: string) -> expr {
    // SQL文字列をコンパイル時に検証・変換
    return parse_sql(query);
}
```

### 2. マクロ使用

```cm
import std.macros.{assert, derive};

@[derive("Debug", "Clone", "PartialEq")]
struct User {
    id: int,
    name: string,
}

fn main() {
    let x = 10;
    assert!(x > 0, "x must be positive");

    // プロシージャマクロ
    let result = sql!("SELECT * FROM users WHERE id = ?");
}
```

## 標準ライブラリ構造

```
std/
├── core/           # コア機能（OS非依存）
│   ├── types.cm    # 基本型定義
│   ├── ops.cm      # 演算子オーバーロード
│   └── mem.cm      # メモリ管理
│
├── io/             # 入出力（OS依存）
│   ├── mod.cm      # モジュール定義
│   ├── print.cm    # 出力関数
│   ├── file.cm     # ファイル操作
│   └── stdin.cm    # 標準入力
│
├── collections/    # データ構造
│   ├── vec.cm      # 動的配列
│   ├── map.cm      # ハッシュマップ
│   └── set.cm      # セット
│
├── sys/            # システム依存
│   ├── unix.cm     # Unix系OS
│   ├── windows.cm  # Windows
│   └── wasm.cm     # WebAssembly
│
└── macros/         # 標準マクロ
    ├── derive.cm   # 自動導出
    ├── test.cm     # テスト用
    └── debug.cm    # デバッグ用
```

## 実装計画

### Phase 1: 基本モジュール機能
1. import/export構文のパース
2. モジュール名前空間の実装
3. 基本的な名前解決

### Phase 2: 外部関数インターフェース
1. `@[extern]`アトリビュートの実装
2. ランタイムとのバインディング機構
3. プラットフォーム固有の実装切り替え

### Phase 3: マクロシステム
1. マクロ定義のパース
2. マクロ展開エンジン
3. 衛生的マクロの実装

### Phase 4: 標準ライブラリ
1. std.coreの実装
2. std.ioの実装
3. std.collectionsの実装

## モジュール解決アルゴリズム

```
1. ソースファイルからimport文を抽出
2. モジュールパスを解決:
   - カレントディレクトリ
   - プロジェクトルート
   - 標準ライブラリパス（CM_STD_PATH）
   - 依存ライブラリパス（.cm/deps/）
3. 循環依存をチェック
4. 依存グラフの順にコンパイル
5. シンボルテーブルにエクスポートを登録
```

## コンパイル時の動作

```cm
// main.cm
import std.io.{print, println};

fn main() {
    println("Hello, World!");
}
```

コンパイル時の変換:

1. **パース段階**: import文を認識、ASTに追加
2. **名前解決**: std.io.printlnをstd/io.cmから解決
3. **HIR変換**: 完全修飾名に変換（std::io::println）
4. **MIR変換**: 外部関数呼び出しに変換
5. **コード生成**:
   - Rust: `std_io::println("Hello, World!")`
   - WASM: `cm_runtime.println("Hello, World!")`
   - ネイティブ: リンク時に解決

## プラットフォーム別実装

### Unix/Linux
```cm
// std/sys/unix.cm
@[cfg(target_os = "linux")]
@[extern("write")]
fn sys_write(fd: int, buf: *const u8, count: usize) -> isize;

export fn print_impl(s: string) -> void {
    sys_write(1, s.as_ptr(), s.len());
}
```

### Windows
```cm
// std/sys/windows.cm
@[cfg(target_os = "windows")]
@[extern("WriteConsoleW")]
fn WriteConsoleW(handle: *void, buf: *const u16, len: u32, written: *mut u32, reserved: *void) -> bool;

export fn print_impl(s: string) -> void {
    // UTF-16変換とWriteConsoleW呼び出し
}
```

### WebAssembly
```cm
// std/sys/wasm.cm
@[cfg(target_arch = "wasm32")]
@[wasm_import("console", "log")]
fn console_log(s: string) -> void;

export fn print_impl(s: string) -> void {
    console_log(s);
}
```

## エラー処理

```cm
// モジュールが見つからない
Error: Cannot find module 'std.foo'
  --> main.cm:1:8
  |
1 | import std.foo;
  |        ^^^^^^^
  | help: did you mean 'std.io'?

// 循環依存
Error: Circular dependency detected
  --> a.cm:1:8
  |
1 | import b;
  |        ^
  | note: b.cm imports a.cm at line 2

// エクスポートされていないシンボル
Error: 'internal_func' is not exported from module 'std.io'
  --> main.cm:3:12
  |
3 |     std.io.internal_func();
  |            ^^^^^^^^^^^^^
  | help: available exports: print, println, eprint
```