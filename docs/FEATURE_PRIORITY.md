# Cm言語 機能実装優先順位

## コア言語機能（実装順）

### 🎯 Priority 0: Minimum Viable Compiler
**目標**: Hello Worldが動く最小コンパイラ

```cm
int main() {
    println("Hello, World!");
    return 0;
}
```

| 機能 | 状態 | 必要性 |
|------|------|--------|
| 基本型（int, bool, string） | ✅ | 必須 |
| 関数定義 | ✅ | 必須 |
| println（組み込み） | ✅ | 必須 |
| return文 | ✅ | 必須 |

---

### 🔴 Priority 1: 基本プログラミング
**目標**: FizzBuzzが書ける

```cm
int main() {
    for (int i = 1; i <= 100; i++) {
        if (i % 15 == 0) {
            println("FizzBuzz");
        } else if (i % 3 == 0) {
            println("Fizz");
        } else if (i % 5 == 0) {
            println("Buzz");
        } else {
            println(to_string(i));
        }
    }
    return 0;
}
```

| 機能 | 状態 | テストファイル |
|------|------|---------------|
| 変数宣言（let, const） | ✅ | stage1_basics/002_variables.cm |
| 算術演算（+, -, *, /, %） | ✅ | stage1_basics/003_arithmetic.cm |
| 比較演算（==, !=, <, >, <=, >=） | ✅ | stage1_basics/003_arithmetic.cm |
| if-else文 | ✅ | stage1_basics/004_control_flow.cm |
| forループ | 🔧 | stage1_basics/004_control_flow.cm |
| whileループ | ✅ | stage1_basics/004_control_flow.cm |
| 型変換（to_string） | 🔧 | stage1_basics/007_strings.cm |

---

### 🟠 Priority 2: 構造化プログラミング
**目標**: データ構造が扱える

```cm
struct Point {
    double x;
    double y;
}

double distance(Point a, Point b) {
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    return sqrt(dx * dx + dy * dy);
}
```

| 機能 | 状態 | テストファイル |
|------|------|---------------|
| 構造体定義 | ✅ | stage2_types/104_struct_basic.cm |
| メンバアクセス（.） | 🔧 | stage2_types/104_struct_basic.cm |
| ポインタ（*） | 🔧 | stage1_basics/006_arrays.cm |
| 参照（&） | ❌ | - |
| 配列 | 🔧 | stage1_basics/006_arrays.cm |
| 標準ライブラリ（math） | ❌ | - |

---

### 🟡 Priority 3: 型システム
**目標**: 型安全なコード

```cm
typedef Int32 = int;
typedef Result<T> = Ok(T) | Err(string);

Result<Int32> parse_int(string s) {
    // 実装
}
```

| 機能 | 状態 | テストファイル |
|------|------|---------------|
| typedef（型エイリアス） | ❌ | stage2_types/101_typedef_basic.cm |
| ユニオン型 | ❌ | stage2_types/103_union_types.cm |
| パターンマッチ | ❌ | - |
| Option/Result型 | ❌ | - |

---

### 🟢 Priority 4: オーバーロード
**目標**: 多重定義

```cm
overload int add(int a, int b) { return a + b; }
overload double add(double a, double b) { return a + b; }
overload string add(string a, string b) { return a + b; }
```

| 機能 | 状態 | テストファイル |
|------|------|---------------|
| overload修飾子 | ❌ | stage3_overload/201_overload_functions.cm |
| コンストラクタオーバーロード | ❌ | stage3_overload/202_overload_constructors.cm |
| 演算子オーバーロード | ❌ | stage3_overload/203_overload_operators.cm |
| オーバーロード解決 | ❌ | stage3_overload/204_overload_resolution.cm |

---

### 🔵 Priority 5: ジェネリクス
**目標**: 汎用プログラミング

```cm
<T: Ord> T max(T a, T b) {
    return a > b ? a : b;
}

struct Vec<T> {
    T* data;
    size_t size;
    size_t capacity;
}
```

| 機能 | 状態 | テストファイル |
|------|------|---------------|
| ジェネリック関数 | ❌ | stage4_generics/301_generic_functions.cm |
| トレイト境界 | ❌ | stage4_generics/302_generic_constraints.cm |
| ジェネリック構造体 | ❌ | stage4_generics/303_generic_structs.cm |
| 型推論 | ❌ | - |

---

### 🟣 Priority 6: オブジェクト指向
**目標**: カプセル化とメソッド

```cm
impl<T> Vec<T> {
    self() {
        this.data = null;
        this.size = 0;
        this.capacity = 0;
    }

    void push(T item) {
        // 実装
    }
}
```

| 機能 | 状態 | テストファイル |
|------|------|---------------|
| implブロック | ❌ | stage5_impl/401_impl_constructor.cm |
| コンストラクタ（self） | ❌ | stage5_impl/401_impl_constructor.cm |
| デストラクタ（~self） | ❌ | stage5_impl/402_impl_destructor.cm |
| メソッド | ❌ | stage5_impl/403_impl_methods.cm |
| this参照 | ❌ | - |

---

### ⚫ Priority 7: マクロ・メタプログラミング
**目標**: コンパイル時処理

```cm
#define bool DEBUG = true

#macro void LOG(string msg) {
    #if DEBUG
        println("[LOG] " + msg);
    #endif
}

#test
void test_addition() {
    assert(1 + 1 == 2);
}
```

| 機能 | 状態 | テストファイル |
|------|------|---------------|
| #define（型付き定数） | ❌ | stage6_macros/501_define_constants.cm |
| #if/#endif（条件コンパイル） | ❌ | stage6_macros/502_conditional_compile.cm |
| #macro（コードマクロ） | ❌ | stage6_macros/503_macro_functions.cm |
| #test（テスト関数） | ❌ | stage6_macros/505_test_bench.cm |
| #bench（ベンチマーク） | ❌ | stage6_macros/505_test_bench.cm |
| constexpr | ❌ | - |

---

### ⚪ Priority 8: 高度な機能
**目標**: 実用的なアプリケーション

```cm
async Task<string> fetch_data(string url) {
    return await http.get(url);
}

module DataStructures {
    export struct BTree<K, V> {
        // 実装
    }
}
```

| 機能 | 状態 | テストファイル |
|------|------|---------------|
| async/await | ❌ | - |
| モジュールシステム | ❌ | - |
| トレイト/インターフェース | ❌ | - |
| クロージャ/ラムダ | ❌ | - |
| エラーハンドリング（try/catch） | ❌ | - |

---

## 実装マイルストーン

### M1: 基本コンパイラ（2024年12月）✅
- [x] Lexer
- [x] Parser（基本）
- [x] AST
- [x] HIR
- [x] MIR

### M2: 実用レベル（2025年1月）
- [ ] 完全なParser
- [ ] 型システム
- [ ] オーバーロード

### M3: ジェネリック対応（2025年2月）
- [ ] ジェネリクス
- [ ] impl ブロック
- [ ] トレイト

### M4: マクロシステム（2025年3月）
- [ ] プリプロセッサ
- [ ] マクロ
- [ ] テスト/ベンチマーク

### M5: トランスパイラ（2025年4月〜）
- [ ] Rustバックエンド
- [ ] TypeScriptバックエンド
- [ ] WASMバックエンド

---

## 各優先度での到達目標

| Priority | できること | サンプルプログラム |
|----------|-----------|------------------|
| P0 | Hello World | hello.cm |
| P1 | FizzBuzz, 基本アルゴリズム | fizzbuzz.cm |
| P2 | データ構造操作 | linked_list.cm |
| P3 | 型安全なエラー処理 | file_reader.cm |
| P4 | 多態的な関数 | calculator.cm |
| P5 | 汎用コンテナ | vector.cm |
| P6 | オブジェクト指向設計 | shapes.cm |
| P7 | デバッグ/テスト | test_suite.cm |
| P8 | 実用アプリケーション | web_server.cm |