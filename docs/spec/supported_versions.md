# Supported Language Versions

## Target Language Versions

Cmコンパイラは以下のバージョンをターゲットとしてコード生成を行います。

### Rust
- **Target Version**: Rust 2021 Edition
- **Minimum Version**: 1.70.0
- **Features**:
  - async/await
  - const generics
  - Edition 2021 features
  - Standard library FFI

### TypeScript
- **Target Version**: TypeScript 5.0+
- **Runtime**: Node.js 18.0+ (LTS)
- **ECMAScript Target**: ES2022
- **Features**:
  - Strict mode
  - ES modules (with CommonJS output)
  - Type checking
  - Async/await

### WebAssembly (Future)
- **Target Version**: WASM 1.0
- **Features**: To be determined

## Compiler Requirements

### Build Requirements
- **C++ Standard**: C++20
- **Compiler**:
  - Clang 17+ (recommended)
  - GCC 13+
  - MSVC 2019+

### Runtime Dependencies
- **Rust Backend**: cargo, rustc 1.70+
- **TypeScript Backend**: npm 9+, node 18+

## Version Compatibility Matrix

| Cm Version | Rust Edition | TypeScript | Node.js | WASM |
|------------|-------------|------------|---------|------|
| 0.1.x      | 2021        | 5.0+       | 18+     | -    |
| 0.2.x      | 2021        | 5.0+       | 18+     | 1.0  |

## Update Policy

- Rust: 新しいEditionがリリースされた場合、次のメジャーバージョンで対応
- TypeScript: 年1回のメジャーバージョン更新時に最新安定版に対応
- Node.js: LTSバージョンのみサポート