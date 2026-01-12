# ランタイムライブラリのモジュール分割設計

## 概要

現在のCmコンパイラは、ランタイムライブラリ関数を `cm_*()` プレフィックス付きのフラットな関数群として実装しています。この設計文書では、将来的にこれらをCmの標準ライブラリモジュールとして再編成する計画について記述します。

## 現状の問題点

### 1. 命名の一貫性
現在の命名規則 `cm_{type}_{operation}_{suffix}` は機能的ですが、ユーザーフレンドリーではありません：

```c
// 現在の命名例
cm_slice_push_i32()
cm_slice_push_f64()
cm_slice_get_ptr()
cm_string_concat()
cm_format_int()
```

### 2. スケーラビリティ
関数が増えるにつれ、フラットな構造は管理が困難になります：

```
runtime_slice.c: 50+ 関数
runtime.c: 30+ 関数
```

### 3. ユーザー公開の検討
将来的にFFI経由でランタイム関数を公開する場合、現在の命名は不適切です。

---

## 提案: Cmモジュールベースの実装

### Phase 1: 内部リファクタリング

ランタイム関数をC++クラスとして再編成（コンパイラ内部のみの変更）：

```cpp
// src/runtime/slice_runtime.hpp
namespace cm::runtime {

class SliceRuntime {
public:
    static void* create(int64_t elem_size, int64_t initial_cap);
    static void free(void* slice);
    static int64_t len(void* slice);
    static int64_t cap(void* slice);
    
    // 型別操作
    template<typename T>
    static void push(void* slice, T value);
    
    template<typename T>
    static T get(void* slice, int64_t index);
    
    template<typename T>
    static T pop(void* slice);
};

} // namespace cm::runtime
```

### Phase 2: Cm標準ライブラリ化

Cmソースコードで定義される標準ライブラリモジュールを作成：

```cm
// std/slice.cm
module std::slice;

// スライスメソッドの定義
impl<T> T[] {
    // コンパイラはこれらを内部ランタイム関数にマッピング
    @builtin("slice_len")
    int len();
    
    @builtin("slice_cap")
    int cap();
    
    @builtin("slice_push")
    void push(T value);
    
    @builtin("slice_pop")
    T pop();
    
    @builtin("slice_get")
    T get(int index);
    
    @builtin("slice_clear")
    void clear();
}
```

### Phase 3: ユーザーアクセス可能な標準ライブラリ

```cm
// std/io.cm
module std::io;

@builtin("println_string")
void println(string s);

@builtin("print_int")  
void print(int n);

// ...
```

---

## モジュール構成案

```
std/
├── core/
│   ├── slice.cm      # スライス操作
│   ├── array.cm      # 配列操作
│   └── memory.cm     # メモリ管理
├── io/
│   ├── print.cm      # 出力関数
│   └── file.cm       # ファイルI/O
├── string/
│   └── string.cm     # 文字列操作
├── math/
│   └── math.cm       # 数学関数
└── collections/
    ├── vector.cm     # 動的配列
    └── hashmap.cm    # ハッシュマップ
```

---

## 内部ランタイムの再編成

### ファイル構成

```
src/runtime/
├── core/
│   ├── slice.cpp         # スライスクラス
│   ├── slice.hpp
│   ├── string.cpp        # 文字列クラス
│   └── string.hpp
├── io/
│   ├── print.cpp         # 出力関数
│   └── print.hpp
├── ffi/
│   ├── c_bridge.cpp      # Cランタイムブリッジ
│   └── wasm_bridge.cpp   # WASMランタイムブリッジ
└── runtime.hpp           # 統合ヘッダ
```

### 命名規則の変更

| 現在 | 新規 |
|------|------|
| `cm_slice_push_i32` | `Slice::push<int32_t>` |
| `cm_slice_get_f64` | `Slice::get<double>` |
| `cm_string_concat` | `String::concat` |
| `cm_println_string` | `IO::println` |

---

## WASM/JIT対応

### シンボル名のマングリング

内部クラス名とCシンボル名のマッピングを維持：

```cpp
// コンパイラが生成するLLVM IR
// SliceRuntime::push<int32_t> → 外部シンボル "cm_slice_push_i32"
```

### 互換性レイヤー

既存のCランタイム関数を新クラスのラッパーとして維持：

```c
// runtime_compat.c
void cm_slice_push_i32(void* slice, int32_t value) {
    SliceRuntime::push<int32_t>(slice, value);
}
```

---

## 実装ロードマップ

### v0.12.0: 内部リファクタリング
- [ ] ランタイム関数をC++クラスに再編成
- [ ] 既存シンボル名との互換性を維持
- [ ] テスト通過を確認

### v0.13.0: Cm標準ライブラリの基盤
- [ ] `@builtin` アトリビュートの実装
- [ ] `std/` ディレクトリ構造の確立
- [ ] コンパイラによるビルトインマッピング

### v0.14.0: 完全なモジュールシステム
- [ ] ユーザー定義モジュールのサポート
- [ ] 標準ライブラリの公開API確定
- [ ] ドキュメント生成

---

## 設計上の注意点

### 1. 後方互換性
既存のCmプログラムは影響を受けない（構文 `.push()`, `.len()` は変更なし）

### 2. パフォーマンス
クラスベースでもインライン化により性能劣化なし

### 3. WASM互換
WASMではCシンボル名が必要なため、ブリッジ関数を維持

### 4. 段階的移行
一度に全てを変更せず、モジュールごとに段階的に移行

---

## まとめ

この設計により、Cmのランタイムライブラリは以下の恩恵を受けます：

1. **保守性向上**: クラスベースで論理的に整理
2. **拡張性**: 新機能追加が容易
3. **ユーザビリティ**: 将来的に標準ライブラリとして公開可能
4. **一貫性**: Cmらしい命名規則への統一

現在の `cm_*()` 命名は内部実装詳細として維持しつつ、ユーザー向けにはCm標準ライブラリとして見せる二層構造を採用します。
