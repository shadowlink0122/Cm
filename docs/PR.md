[English](PR.en.html)

# v0.13.1 Release - Cm言語コンパイラ

## 概要

v0.13.1は**GPU/Metal対応**、**ARM64ネイティブビルド**、**標準ライブラリ安定化**、**HTTPS対応**、**リテラル型完全サポート**に焦点を当てた大規模アップデートです。テスト数363 PASS / 0 FAILの安定ベースラインを達成しました。

## 🎯 主要な新機能

### 1. GPU/Metal対応

Apple Metal GPUバックエンドによるGPU演算を実装しました。

```cm
import std::gpu::create_context;
import std::gpu::gpu_alloc;
import std::gpu::gpu_compute;

int main() {
    long ctx = create_context();
    long buf_a = gpu_alloc(ctx, a_data, 4);
    long buf_b = gpu_alloc(ctx, b_data, 4);
    gpu_compute(ctx, "vector_add", buf_a, buf_b, buf_out, 4);
    return 0;
}
```

- Metal Shading Language (MSL) カーネル実行
- int/float/double型のGPUバッファ管理
- Nativeコンパイル対応（GPU XOR NN学習テスト搭載）

### 2. ARM64ネイティブビルド & マルチアーキテクチャ対応

x86_64 LLVM（Rosetta 2）からARM64 LLVMへ移行し、`ARCH`オプションで自動切替が可能になりました。

```bash
make build              # デフォルト（LLVM自動検出）
make build ARCH=arm64   # ARM64ビルド
make build ARCH=x86_64  # x86_64ビルド
```

### 3. HTTPS (TLS) 対応

OpenSSL 3.6.0による暗号化通信:

```cm
import std::http::request;

int main() {
    request("https://example.com", "GET");
    return 0;
}
```

### 4. ジェネリックコンストラクタ/デストラクタ

`self()`/`~self()`構文による慣用的なライフサイクル管理:

```cm
struct Vector<T> {
    T* data;
    int size;
    int capacity;

    self() {
        this.data = malloc(8 * sizeof(T)) as T*;
        this.size = 0;
        this.capacity = 8;
    }

    ~self() {
        free(this.data as void*);
    }
}
```

### 5. 標準ライブラリ安定化

| モジュール | 内容 |
|-----------|------|
| `std::collections` | Vector, Queue, HashMap（self()/~self()） |
| `std::sync` | Mutex, Channel, Atomic |
| `std::http` | HTTP/HTTPS通信（struct+impl API） |
| `std::core::result` | CmResult\<T, E\>型 |

### 6. const定数評価拡張

- **const folding**: コンパイル時定数畳み込みの改善
- **Octalリテラル**: `0o777`形式の8進数リテラルサポート

### 7. リテラル型の関数引数・戻り値サポート

リテラル型が関数の引数・戻り値として完全にサポートされました。

```cm
typedef HttpMethod = "GET" | "POST" | "PUT" | "DELETE";
typedef StatusCode = 200 | 400 | 404 | 500;

void handle_request(HttpMethod method) {
    println("Method: {method}");
}

HttpMethod get_method() {
    return "GET";
}
```

### 8. typedef Union配列の関数引数・戻り値対応

Union型配列を関数間で受け渡しできるようになりました。

```cm
typedef Value = string | int | bool;

Value[3] make_values() {
    Value[3] arr = ["hello" as Value, 42 as Value, true as Value];
    return arr;
}

void print_values(Value[3] vals) {
    string s = vals[0] as string;
    println("s={s}");
}
```

---

## 🐛 バグ修正

### 配列リテラル内Union Castの二重生成バグ修正

| 問題 | 原因 | 修正 |
|-----|------|------|
| Union配列の値が破壊される | `lower_array_literal`でtypedef未解決の型を使用 | `expr_basic.cpp`で解決済みの型から取得 |

### リテラル型の関数引数での文字列値破壊バグ修正

| 問題 | 原因 | 修正 |
|-----|------|------|
| 文字列リテラル型がゴミ値に | `resolve_typedef()`がLiteralUnion型を素通り | `base.hpp`/`context.hpp`に基底型変換を追加 |

---

## 🔧 チュートリアル・ドキュメント改善

### 新規チュートリアル

| ドキュメント | 内容 |
|------------|------|
| `docs/tutorials/ja/stdlib/` | 標準ライブラリ（Vector, Queue, HashMap, IO, HTTP, GPU, Math, Mem） |
| `docs/tutorials/ja/stdlib/concurrency/` | 並行処理（Thread, Mutex, Channel, Atomic） |
| `docs/tutorials/ja/advanced/extern.md` | extern/FFI連携 |
| `docs/tutorials/ja/advanced/inline-asm.md` | インラインアセンブリ |
| `docs/tutorials/ja/internals/` | コンパイラ内部構造 |

### 更新チュートリアル

| ドキュメント | 変更 |
|------------|------|
| `docs/tutorials/*/types/typedef.md` | リテラル型セクション拡充（関数引数・戻り値の例を追加） |
| `docs/tutorials/*/types/enums.md` | Tagged Union/Match式の解説強化 |
| 各チュートリアル | 親ページ（index.md）リンクを追加 |

---

## 📁 変更ファイル一覧（主要）

### 新機能

| ファイル | 変更内容 |
|---------|---------|
| `std/gpu/mod.cm` | Metal GPU演算モジュール |
| `std/gpu/gpu_runtime.mm` | Objective-C++ Metal GPU ランタイム |
| `std/http/mod.cm` | HTTP/HTTPS通信モジュール |
| `std/http/http_runtime.cpp` | HTTP通信ランタイム（OpenSSL統合） |
| `std/sync/mod.cm` | Mutex/Channel/Atomic同期プリミティブ |
| `std/sync/sync_runtime.cpp` | 同期ランタイム（pthread） |
| `std/sync/channel_runtime.cpp` | Channelランタイム |
| `std/collections/` | Vector/Queue/HashMap(self()/~self()) |
| `std/core/result.cm` | CmResult\<T, E\>型 |
| `std/net/net_runtime.cpp` | TCP/UDPネットワーキングランタイム |

### コンパイラ修正

| ファイル | 変更内容 |
|---------|---------|
| `src/mir/lowering/base.hpp` | LiteralUnion→基底型変換、typedef解決強化 |
| `src/mir/lowering/context.hpp` | 同上（LoweringContext版） |
| `src/mir/lowering/expr_basic.cpp` | 配列リテラルのelem_type解決修正 |
| `src/mir/lowering/stmt.cpp` | MIR文の改善 |
| `src/codegen/llvm/core/mir_to_llvm.cpp` | Union Cast安全性改善 |
| `src/codegen/llvm/core/types.cpp` | 型変換ロジック改善 |
| `src/codegen/llvm/core/utils.cpp` | ユーティリティ追加 |
| `src/codegen/llvm/native/codegen.hpp` | ARM64フラグ対応 |
| `src/codegen/llvm/native/target.hpp` | ARM64トリプル対応 |
| `src/frontend/lexer/lexer.hpp` | Octalリテラルサポート |
| `src/frontend/parser/parser_expr.cpp` | パーサー改善 |
| `src/frontend/types/checking/call.cpp` | 型チェック改善 |

### ビルドシステム

| ファイル | 変更内容 |
|---------|---------|
| `CMakeLists.txt` | ARM64自動検出、LLVM/OpenSSLパス設定 |
| `Makefile` | マルチアーキテクチャ自動環境設定 |

---

## 🧪 テスト状況

| カテゴリ | 通過 | 失敗 | スキップ |
|---------|-----|------|---------|
| JIT (O0) | 363 | 0 | 9 |

### v0.13.0からの新規テスト (+12)

| テスト | カテゴリ |
|-------|---------|
| `gpu/gpu_basic.cm` | GPU |
| `gpu/gpu_xor_nn_test.cm` | GPU |
| `gpu/gpu_float_test.cm` | GPU |
| `http/http_external_test.cm` | HTTP |
| `http/http_rest_test.cm` | HTTP |
| `sync/thread_channel_atomic_test.cm` | 同期 |
| `const/octal_test.cm` | const |
| `const/const_eval_test.cm` | const |
| `types/typedef_literal_func.cm` | 型 |
| `types/typedef_union_comprehensive.cm` | 型 |
| `types/union_array_func.cm` | 型 |
| `enum/union_array_tuple_test.cm` | enum |

---

## 📊 統計

- **テスト総数**: 372（v0.13.0の360から12増加）
- **テスト通過**: 363
- **変更ファイル数**: 189
- **追加行数**: +12,149
- **削除行数**: -1,947
- **新規標準ライブラリモジュール**: gpu, http, collections, core/result, sync

---

## ✅ チェックリスト

- [x] `make tip` 全テスト通過（363 PASS / 0 FAIL）
- [x] リリースノート更新（`docs/releases/v0.13.1.md`）
- [x] チュートリアル更新（日英両方）
- [x] ローカルパス情報なし

---

**リリース日**: 2026年2月9日
**バージョン**: v0.13.1