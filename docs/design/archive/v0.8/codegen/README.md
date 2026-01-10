[English](README.en.html)

# Codegen アーキテクチャ

## 概要

Cmコンパイラのコード生成（Codegen）は、HIRから各ターゲット言語への変換を担当します。

## アーキテクチャ

```
┌─────────────────────────────────────────────────────────────┐
│                        HIR                                  │
└─────────────────────────────────────────────────────────────┘
                            │
        ┌───────────────────┼───────────────────┐
        ▼                   ▼                   ▼
┌───────────────┐   ┌───────────────┐   ┌───────────────┐
│  RustEmitter  │   │ WasmEmitter   │   │  TSEmitter    │
└───────────────┘   └───────────────┘   └───────────────┘
        │                   │                   │
        ▼                   ▼                   ▼
    .rs files           .wasm files         .ts files
        │                   │                   │
        ▼                   ▼                   ▼
    [rustc]             [Browser]           [tsc]
        │                                       │
        ▼                                       ▼
    Native                                  Node.js
```

## Emitter共通インターフェース

```cpp
// src/backend/emitter.hpp
class Emitter {
public:
    virtual ~Emitter() = default;
    
    // トップレベル項目
    virtual void emit(const HirModule& module) = 0;
    virtual void emit(const HirFunction& func) = 0;
    virtual void emit(const HirStruct& s) = 0;
    virtual void emit(const HirInterface& i) = 0;
    virtual void emit(const HirImpl& impl) = 0;
    
    // 式・文
    virtual void emit(const HirExpr& expr) = 0;
    virtual void emit(const HirStmt& stmt) = 0;
    
    // 出力取得
    virtual std::string get_output() const = 0;
};
```

## ディレクトリ構造

```
src/backend/
├── emitter.hpp          # 共通インターフェース
├── rust/
│   ├── rust_emitter.hpp
│   ├── rust_emitter.cpp
│   └── rust_types.hpp   # 型マッピング
├── typescript/
│   ├── ts_emitter.hpp
│   ├── ts_emitter.cpp
│   └── ts_types.hpp
├── wasm/
│   ├── wasm_emitter.hpp
│   └── wasm_emitter.cpp
└── interpreter/
    ├── interpreter.hpp
    └── interpreter.cpp
```

## ドキュメント

| ファイル | 内容 |
|----------|------|
| [rust.md](rust.html) | Rust変換規則 |
| [typescript.md](typescript.html) | TypeScript変換、React連携 |
| [wasm.md](wasm.html) | WASM生成 |