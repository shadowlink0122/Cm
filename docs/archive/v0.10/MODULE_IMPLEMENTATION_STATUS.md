[English](MODULE_IMPLEMENTATION_STATUS.en.html)

# モジュールシステム実装状況

**更新日時**: 2025-12-21  
**ステータス**: 全機能完了（全バックエンド動作確認済み）

---

## ドキュメント

- **ユーザーガイド**: [MODULE_USER_GUIDE.md](../guides/MODULE_USER_GUIDE.html)
- **設計文書**: [MODULE_SYSTEM_FINAL.md](MODULE_SYSTEM_FINAL.html)

---

## 実装完了した機能 ✅

### Phase 1: 基本的なモジュールシステム（完了）

#### 1. モジュール宣言
- ✅ `module M;` ファイル先頭でのモジュール宣言
- ✅ モジュール名が名前空間として機能

#### 2. インポート構文
- ✅ `import ./relative;` 相対パスインポート
- ✅ `import std::io;` 絶対パスインポート（標準ライブラリ）
- ✅ `import ./io/file;` 深い階層のインポート
- ✅ `import M as alias;` エイリアスインポート
- ✅ `import M::{item1, item2};` 選択的インポート
- ✅ `import ./path/*;` ワイルドカードインポート
- ✅ `import ./path/std::io;` サブモジュールインポート
- ✅ `import ./path/std::io::func;` サブモジュール直接インポート
- ✅ `import ./path/std::io::*;` サブモジュールワイルドカード

#### 3. エクスポート構文
- ✅ `export int func() { }` 宣言時エクスポート
- ✅ `export struct S { }` 構造体エクスポート
- ✅ `export const int X = 1;` 定数エクスポート
- ✅ `export { M };` 再エクスポート
- ✅ `export { M, N };` 複数再エクスポート

#### 4. 名前空間
- ✅ ネストした名前空間の自動生成
- ✅ `std::io::println()` 完全修飾名での関数呼び出し
- ✅ `ns::Type var;` 名前空間付き型宣言
- ✅ 循環依存の検出

#### 5. ディレクトリ構造
- ✅ `io/io.cm` 形式のエントリーポイント検出
- ✅ `io/mod.cm` 形式のエントリーポイント検出
- ✅ 相対パスの解決

### Phase 2: ディレクトリベース名前空間（完了）

- ✅ `./io/file` → `io::read()` 
- ✅ ディレクトリ構造が自動的に名前空間に反映
- ✅ 中間ディレクトリを省略した直接インポート

### Phase 3: 高度なインポート（完了）

- ✅ `import ./path/*;` ディレクトリ内全モジュールインポート
- ✅ `import ./path/std::io::func;` サブモジュール関数の直接インポート
- ✅ `import ./path/std::io::*;` サブモジュールの全エクスポートインポート

### implの可視性ルール（決定済み）

| ルール | 状態 | 説明 |
|--------|------|------|
| **暗黙的エクスポート** | ✅ | 構造体がexportされていればimplも自動的に公開 |
| **上書き禁止** | ✅ | 同じ型への同じインターフェース実装は1回のみ |
| **メソッド名重複禁止** | ✅ | 異なるインターフェースでも同名メソッドは禁止 |

理由:
- implは型の一部であり、構造体をエクスポートするならimplも含めるのが自然
- 予測可能性とデバッグ容易性のため上書きは禁止
- 曖昧さを避けるためメソッド名重複も禁止

---

## バックエンド動作確認結果（2025-12-21 最終更新）

| テストケース | インタプリタ | LLVM Native | WASM |
|--------------|-------------|-------------|------|
| basic_reexport | ✅ | ✅ | ✅ |
| hierarchical | ✅ | ✅ | ✅ |
| deep_hierarchy | ✅ | ✅ | ✅ |
| alias_import | ✅ | ✅ | ✅ |
| nested_namespace | ✅ | ✅ | ✅ |
| phase2_complete | ✅ | ✅ | ✅ |
| phase2_hierarchy_rebuild | ✅ | ✅ | ✅ |
| relative_import | ✅ | ✅ | ✅ |
| selective_import | ✅ | ✅ | ✅ |
| recursive_wildcard | ✅ | ✅ | ✅ |
| simple_test | ✅ | ✅ | ✅ |

---

## テスト済みケース

### 基本的な再エクスポート
```cm
// tests/test_programs/modules/basic_reexport/
// io.cm → std.cm → main.cm
import std::io;
std::io::add(10, 20);  // ✅ 動作
```

### 深い階層
```cm
// tests/test_programs/modules/hierarchical/
// file.cm → io.cm → std.cm → main.cm
import ./std;
std::io::file::read_file(5);  // ✅ 動作
```

### 3階層以上
```cm
// tests/test_programs/modules/phase2_complete/
// strutil.cm → utils.cm → lib.cm → main.cm
lib::utils::strutil::str_length(10);  // ✅ 動作
```

### 選択的インポート
```cm
// tests/test_programs/modules/selective_import/
import ./math::{add, multiply};
add(10, 20);      // ✅ 直接呼び出し可能
multiply(3, 4);   // ✅ 直接呼び出し可能
```

### 再帰的ワイルドカード
```cm
// tests/test_programs/modules/recursive_wildcard/
import ./lib/**;   // lib以下のすべてのモジュールをインポート
a::func_a(10);     // ✅ lib/a/a.cmの関数
b::func_b(25);     // ✅ lib/b/b.cmの関数
```

---

## 実装の設計決定

### 名前空間構築ルール

1. **相対パスインポート**: `./io/file` → ディレクトリパス `io` が名前空間になる
2. **再帰的インポート**: `./lib/**` → 各サブディレクトリが名前空間になる
3. **選択的インポート**: `::{items}` → 名前空間なしで直接アクセス可能

### 代替アプローチ

`export { io::{file, stream} }` の明示的構文は未実装ですが、以下の方法で同等の機能を実現:

```cm
// std.cm
import ./io/file;    // → io::read()
import ./io/stream;  // → io::open()

// main.cm
import ./std;
std::io::read(5);    // ✅ 動作
std::io::open(50);   // ✅ 動作
```

---

## 参照ドキュメント
- `MODULE_SYSTEM_FINAL.md`: 最終設計仕様
- `MODULE_IMPLEMENTATION_PLAN.md`: 実装計画詳細
- `ROADMAP.md`: 全体ロードマップ