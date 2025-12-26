# Cm言語 トランスコンパイラ設計

## 概要

MIRを基盤として、複数のターゲット言語（Rust、TypeScript、WASM）へのトランスパイルを実現する統一アーキテクチャ。

## アーキテクチャ

```
┌─────────────┐
│   Cm Code   │
└─────┬───────┘
      ▼
┌─────────────┐
│   Parser    │
└─────┬───────┘
      ▼
┌─────────────┐
│     AST     │
└─────┬───────┘
      ▼
┌─────────────┐
│     HIR     │ ← 型チェック済み
└─────┬───────┘
      ▼
┌─────────────┐
│     MIR     │ ← SSA形式、最適化済み
└─────┬───────┘
      ▼
┌─────┴─────────────┬─────────────┬──────────────┐
│   Rust Backend    │ TS Backend  │ WASM Backend │
└─────┬─────────────┴─────────────┴──────────────┘
      ▼
┌─────────────────────────────────────────────────┐
│        Target Code (Rust/TS/WASM)              │
└─────────────────────────────────────────────────┘
```

## MIRベースの利点

1. **共通最適化**: 全バックエンドで同じ最適化パスを共有
2. **SSA形式**: データフロー解析が容易
3. **型安全**: HIRで型チェック済み、MIRで型情報保持
4. **段階的実装**: バックエンドを独立に開発可能

## バックエンド実装

### 基底クラス設計

```cpp
// src/codegen/backend.hpp
namespace cm::codegen {

class Backend {
public:
    virtual ~Backend() = default;

    // エントリポイント
    virtual std::string generate(const mir::MirProgram& program) = 0;

protected:
    // 共通ヘルパー
    virtual std::string emit_function(const mir::MirFunction& func) = 0;
    virtual std::string emit_basic_block(const mir::BasicBlock& bb) = 0;
    virtual std::string emit_statement(const mir::MirStatement& stmt) = 0;
    virtual std::string emit_terminator(const mir::MirTerminator& term) = 0;

    // 型マッピング
    virtual std::string map_type(const hir::TypePtr& type) = 0;

    // 名前マングリング
    std::string mangle_name(const std::string& name,
                           const std::vector<hir::TypePtr>& param_types);
};

}  // namespace cm::codegen
```

### Rustバックエンド

```cpp
// src/codegen/rust_backend.hpp
class RustBackend : public Backend {
public:
    std::string generate(const mir::MirProgram& program) override;

private:
    std::string emit_function(const mir::MirFunction& func) override {
        // fn func_name(params) -> RetType { ... }
    }

    std::string map_type(const hir::TypePtr& type) override {
        // int → i32, double → f64, string → String
    }

    // Rust固有
    std::string emit_lifetime_annotations(const mir::MirFunction& func);
    std::string emit_trait_bounds(const hir::TypePtr& type);
};
```

### TypeScriptバックエンド

```cpp
// src/codegen/typescript_backend.hpp
class TypeScriptBackend : public Backend {
public:
    std::string generate(const mir::MirProgram& program) override;

private:
    std::string emit_function(const mir::MirFunction& func) override {
        // function funcName(params): RetType { ... }
    }

    std::string map_type(const hir::TypePtr& type) override {
        // int/double → number, string → string
    }

    // TypeScript固有
    std::string emit_overload_signatures(const std::string& name);
    std::string emit_type_guards(const hir::TypePtr& type);
};
```

### WASMバックエンド

```cpp
// src/codegen/wasm_backend.hpp
class WasmBackend : public Backend {
public:
    std::string generate(const mir::MirProgram& program) override;

private:
    // WAT (WebAssembly Text) 形式で出力
    std::string emit_function(const mir::MirFunction& func) override {
        // (func $name (param ...) (result ...) ...)
    }

    std::string map_type(const hir::TypePtr& type) override {
        // int → i32, double → f64, bool → i32
    }

    // WASM固有
    std::string emit_memory_layout();
    std::string emit_table_section();
};
```

## 型マッピング戦略

| Cm型 | Rust | TypeScript | WASM |
|------|------|------------|------|
| int | i32/i64 | number | i32/i64 |
| double | f64 | number | f64 |
| bool | bool | boolean | i32 |
| string | String | string | (memory) |
| T* | &T/&mut T/Box<T> | T | i32 (ptr) |
| struct S | struct S | interface S | (memory) |
| Vec<T> | Vec<T> | T[] | (memory) |

## 組み込み関数

```cpp
// src/codegen/builtins.hpp
struct BuiltinFunction {
    std::string name;
    std::string rust_impl;
    std::string ts_impl;
    std::string wasm_impl;
};

const std::vector<BuiltinFunction> BUILTINS = {
    {
        "println",
        "println!(\"{}\", {})",           // Rust
        "console.log({})",                // TypeScript
        "(call $print (local.get {}))"    // WASM
    },
    {
        "sqrt",
        "({} as f64).sqrt()",              // Rust
        "Math.sqrt({})",                   // TypeScript
        "(f64.sqrt (local.get {}))"       // WASM
    }
};
```

## 実装段階

### Phase 1: 基本機能（現在）
- [x] MIRインタープリタ
- [ ] 基本型のマッピング
- [ ] println等の組み込み関数
- [ ] 基本的な制御フロー

### Phase 2: Rustバックエンド
- [ ] Rust型システムへのマッピング
- [ ] 所有権の推論と注釈
- [ ] トレイト境界の生成
- [ ] 標準ライブラリ統合

### Phase 3: TypeScriptバックエンド
- [ ] TypeScript型システムへのマッピング
- [ ] オーバーロードシグネチャ
- [ ] 型ガードの生成
- [ ] npm パッケージ生成

### Phase 4: WASMバックエンド
- [ ] WAT/WASM生成
- [ ] メモリ管理
- [ ] ホストとのインターフェース
- [ ] WASI対応

## テスト戦略

```bash
# 同一のテストプログラムを全バックエンドで実行
./tests/runners/test_runner.sh --backend=all --suite=basic

# 生成コードの検証
cm --emit-rust test.cm -o test.rs
rustc test.rs && ./test

cm --emit-typescript test.cm -o test.ts
tsc test.ts && node test.js

cm --emit-wasm test.cm -o test.wasm
wasmtime test.wasm
```

## 最適化

MIRレベルで共通最適化を実施：
- 定数伝播
- デッドコード削除
- インライン展開
- ループ最適化

バックエンド固有最適化：
- Rust: 借用チェッカ最適化
- TypeScript: バンドルサイズ最適化
- WASM: バイナリサイズ最適化

## まとめ

MIRベースのトランスコンパイラにより：
- **統一アーキテクチャ**: 全バックエンドで共通基盤
- **段階的実装**: 各バックエンドを独立開発
- **品質保証**: 同一テストで全バックエンド検証
- **最適化**: MIRレベルで共通最適化を適用