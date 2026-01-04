# Cm言語 実装ロードマップ

## フェーズ1：基本コンパイラ（現在）

### 1.1 Lexer（字句解析） ✅ 完了
```
実装済み：
- 基本トークン（識別子、リテラル、演算子）
- キーワード認識
- コメント処理
- 位置情報追跡

テスト：
tests/unit/lexer_test.cpp
tests/regression/lexer/
  ├── basic_tokens.cm
  ├── keywords.cm
  ├── operators.cm
  └── edge_cases.cm
```

### 1.2 Parser（構文解析） 🔧 80%完了
```
実装済み：
- 基本文（let, if, while, return）
- 式（二項演算、単項演算、関数呼び出し）
- 関数定義
- 構造体定義
- import/export（部分的）

未実装：
- ジェネリック構文 <T: Trait>
- impl ブロック
- overload 修飾子
- #macro, #define, #test

テスト：
tests/unit/parser_test.cpp
tests/regression/parser/
  ├── statements/
  ├── expressions/
  ├── functions/
  └── errors/
```

### 1.3 AST（抽象構文木） ✅ 完了
```
実装済み：
- 全基本ノード
- Visitor パターン
- Pretty printer

テスト：
tests/unit/ast_test.cpp
```

### 1.4 HIR（高レベル中間表現） ✅ 完了
```
実装済み：
- AST → HIR 変換
- 型解決
- 脱糖処理

テスト：
tests/unit/hir_lowering_test.cpp
```

### 1.5 MIR（中レベル中間表現） ✅ 完了
```
実装済み：
- SSA形式
- CFG構築
- 基本ブロック

テスト：
tests/unit/mir_lowering_test.cpp
tests/unit/mir_optimization_test.cpp
```

---

## フェーズ2：言語機能の実装（次のステップ）

### 優先度1：基本機能の完成

#### 2.1 型システム
```cm
// 実装する機能
typedef Int = int;                    // 型エイリアス
typedef Result<T> = Ok(T) | Err(string); // ユニオン型

// テストファイル
tests/regression/types/
  ├── typedef.cm
  ├── union_types.cm
  └── type_inference.cm
```

#### 2.2 関数オーバーロード
```cm
// overload修飾子
overload int add(int a, int b) { }
overload double add(double a, double b) { }

// テストファイル
tests/regression/overload/
  ├── basic_overload.cm
  ├── constructor_overload.cm
  └── resolution_rules.cm
```

#### 2.3 ジェネリクス
```cm
// ジェネリック関数
<T: Ord> T max(T a, T b) {
    return a > b ? a : b;
}

// テストファイル
tests/regression/generics/
  ├── generic_functions.cm
  ├── generic_structs.cm
  └── trait_bounds.cm
```

### 優先度2：構造体とメソッド

#### 2.4 impl ブロック
```cm
impl<T> Vec<T> {
    self() { }                    // コンストラクタ
    overload self(size_t cap) { } // オーバーロード
    ~self() { }                   // デストラクタ
}

impl<T> Vec<T> for Container<T> {
    void push(T item) { }         // メソッド
}

// テストファイル
tests/regression/impl/
  ├── constructors.cm
  ├── destructors.cm
  ├── methods.cm
  └── trait_impl.cm
```

### 優先度3：マクロシステム

#### 2.5 プリプロセッサ
```cm
#define bool DEBUG = true
#if DEBUG
    // デバッグコード
#endif

#macro void LOG(string msg) {
    println("[LOG] " + msg);
}

// テストファイル
tests/regression/preprocessor/
  ├── define.cm
  ├── conditional_compilation.cm
  └── macros.cm
```

### 優先度4：高度な機能

#### 2.6 非同期処理
```cm
async Task<int> fetch_data() {
    return await http.get("...");
}

// テストファイル
tests/regression/async/
  ├── async_functions.cm
  └── await_expressions.cm
```

---

## フェーズ3：コード生成

### 3.1 Rustトランスパイラ
```
実装順序：
1. 基本型マッピング
2. 関数変換
3. 構造体変換
4. オーバーロード解決（名前マングリング）
5. ジェネリック変換

出力ディレクトリ：
build/transpiled/rust/

テスト：
tests/codegen/rust/
  ├── basic_types.cm → basic_types.rs
  ├── functions.cm → functions.rs
  └── overloads.cm → overloads.rs
```

### 3.2 TypeScriptトランスパイラ
```
実装順序：
1. 型システムマッピング
2. クラス変換
3. オーバーロードシグネチャ
4. async/await

出力ディレクトリ：
build/transpiled/typescript/

テスト：
tests/codegen/typescript/
  ├── classes.cm → classes.ts
  └── async.cm → async.ts
```

### 3.3 WebAssembly生成
```
実装順序：
1. 基本演算
2. メモリ管理
3. 関数呼び出し規約
4. モジュールシステム

出力ディレクトリ：
build/wasm/

テスト：
tests/codegen/wasm/
  ├── arithmetic.cm → arithmetic.wasm
  └── memory.cm → memory.wasm
```

---

## テストファイル構造

```
tests/
├── unit/                      # C++ユニットテスト
│   ├── lexer_test.cpp
│   ├── parser_test.cpp
│   ├── ast_test.cpp
│   ├── hir_lowering_test.cpp
│   ├── mir_lowering_test.cpp
│   ├── mir_optimization_test.cpp
│   ├── type_checker_test.cpp
│   └── codegen_test.cpp
│
├── regression/                # Cmリグレッションテスト
│   ├── lexer/
│   ├── parser/
│   ├── types/
│   ├── overload/
│   ├── generics/
│   ├── impl/
│   ├── preprocessor/
│   ├── async/
│   └── errors/               # エラーケース
│
├── codegen/                  # コード生成テスト
│   ├── rust/
│   ├── typescript/
│   └── wasm/
│
└── e2e/                      # End-to-Endテスト
    ├── hello_world.cm
    ├── fibonacci.cm
    └── data_structures.cm
```

---

## 実装優先順位

### Phase 1（〜2025年1月）
1. ✅ 基本パイプライン完成
2. ⏳ Parser完成（ジェネリック、overload）
3. ⏳ 型システム強化

### Phase 2（2025年2月〜3月）
1. impl ブロック実装
2. マクロシステム
3. オーバーロード解決

### Phase 3（2025年4月〜）
1. Rustトランスパイラ
2. TypeScriptトランスパイラ
3. 最適化パス

---

## 各段階での成果物

### Milestone 1：Hello World
```cm
int main() {
    println("Hello, World!");
    return 0;
}
```
必要：Lexer, Parser, AST, 基本型

### Milestone 2：データ構造
```cm
struct Vec<T> {
    T* data;
    size_t size;
}

impl<T> Vec<T> {
    self() { }
    void push(T item) { }
}
```
必要：ジェネリック、impl、メソッド

### Milestone 3：実用プログラム
```cm
#define bool DEBUG = true

<T: Ord> T* binary_search(T arr[], size_t size, T target) {
    // 実装
}

async Task<string> fetch_url(string url) {
    return await http.get(url);
}
```
必要：完全な言語機能

---

## テスト駆動開発

各機能実装前に：
1. `tests/regression/` に期待するCmコードを作成
2. パーサーテストを追加
3. 変換テストを追加
4. 実装
5. 全テスト通過を確認