# CLAUDE.md - Cm Language Project

## AI Development Guidelines

> [!IMPORTANT]
> **Think-First Development**: 機能実装前に必ず以下を検討すること
> 1. 既存設計との整合性（特に `docs/design/` 参照）
> 2. クロスバックエンド互換性（Interpreter/Rust/TS）
> 3. 技術的課題の確認（`docs/design/technical_challenges.md`）
> 4. テスト戦略（`docs/design/testing.md`）

## Project Overview

Cm (シーマイナー) is a next-generation programming language designed for both low-level systems (OS, embedded) and web frontend development. It is a complete redesign of the [Cb language](https://github.com/shadowlink0122/Cb).

**Goals:**
- Write once, compile to native (via Rust transpilation) or web (WASM/TypeScript)
- Modern type system with generics, async/await, pattern matching
- Integrated package manager (gen)

## Development Language

- **C++20** (required)
- **Recommended**: Clang 17+ (fast compile, good errors)
- **Alternative**: GCC 13+
- MSVC 2017+ (Windows)

## Build Commands

### Docker (Recommended)

```bash
docker compose run --rm dev           # 開発シェル
docker compose run --rm build-clang   # Clangビルド
docker compose run --rm lint          # Lint
docker compose run --rm test          # テスト
```

### Local

```bash
# Configure (Clang推奨)
CC=clang CXX=clang++ cmake -B build -G Ninja

# Build
cmake --build build

# Test
ctest --test-dir build
```

## Debug Mode

```bash
# Enable debug logging
cm run example.cm --debug   # or -d
cm run example.cm -d=trace  # TRACE level (most verbose)
```

Log levels: TRACE > DEBUG > INFO > WARN > ERROR

## Testing

```bash
# All tests
ctest --test-dir build

# By category
ctest --test-dir build -L unit
ctest --test-dir build -L integration
./tests/e2e/run_all.sh
```

## Coding Conventions

- **Language**: C++20
- **Naming**: `snake_case` for functions/variables, `PascalCase` for types
- **Smart Pointers**: `std::unique_ptr` for AST/IR node ownership
- **Variant**: `std::variant` for IR node types
- **Optional**: `std::optional` for nullable values

## Key Design Decisions

1. **Transpiler First**: HIR → Rust/WASM/TS (Phase 1)
2. **C++20**: Modern features (Concepts, Ranges, Coroutines)
3. **Multi-Target HIR**: Designed for Rust ∩ TypeScript common subset
4. **Future Native**: Cranelift or LLVM in Phase 3

## Important Files

- `docs/design/architecture.md` - Overall compiler architecture
- `docs/design/hir.md` - HIR design (multi-target optimized)
- `docs/design/backends.md` - Rust/WASM/TS backends
- `docs/design/package_manager.md` - Package management system
- `docs/design/debug.md` - Debug mode design
- `docs/design/testing.md` - Testing strategy

## Related Projects

- [Cb Language](https://github.com/shadowlink0122/Cb) - Predecessor project
