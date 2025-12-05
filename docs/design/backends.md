# マルチバックエンド設計

## 概要

Cm言語はトランスパイラとして複数のバックエンドをサポートし、ネイティブアプリケーションとWebアプリケーションの両方を単一のコードベースから生成します。

## 開発段階

| Phase | バックエンド | 実装 |
|-------|-------------|------|
| **Phase 1** | Rust, WASM, TS | HIR → 直接出力 |
| Phase 2 | + MIR最適化 | HIR → MIR → 出力 |
| Phase 3 | + ネイティブ直接 | MIR → Cranelift/LLVM |

## アーキテクチャ

```
┌─────────────────────────────────────────────────────────────┐
│ Phase 1: トランスパイラ                                      │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   Cm Source → Lexer → Parser → AST → TypeCheck → HIR       │
│                                                    │        │
│                          ┌─────────────────────────┼────┐   │
│                          ▼           ▼            ▼     │   │
│                     ┌────────┐ ┌──────────┐ ┌─────────┐ │   │
│                     │  Rust  │ │   WASM   │ │   TS    │ │   │
│                     │ (主力) │ │(Rust経由)│ │         │ │   │
│                     └───┬────┘ └────┬─────┘ └────┬────┘ │   │
│                         │          │            │       │   │
│                         ▼          ▼            ▼       │   │
│                     [rustc]   [wasm-pack]      [tsc]    │   │
│                         │          │            │       │   │
│                         ▼          ▼            ▼       │   │
│                     Native     Browser       Node.js    │   │
│                                                         │   │
│                     ┌─────────────┐                     │   │
│                     │ Interpreter │ ← 開発・REPL用      │   │
│                     └─────────────┘                     │   │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

## 1. Rustバックエンド（主力）

### 概要

CmコードをRustに変換し、rustcでネイティブバイナリを生成します。

### 変換例

```cpp
// Cm (C++風構文)
struct Point {
    int x;
    int y;
};

int distance(Point p) {
    return sqrt(p.x * p.x + p.y * p.y);
}

// → Rust
struct Point {
    x: i64,
    y: i64,
}

fn distance(p: Point) -> i64 {
    ((p.x * p.x + p.y * p.y) as f64).sqrt() as i64
}
```

### 型マッピング

| Cm | Rust |
|----|------|
| `int` | `i64` |
| `float` | `f64` |
| `bool` | `bool` |
| `String` | `String` |
| `Option<T>` | `Option<T>` |
| `Result<T, E>` | `Result<T, E>` |

---

## 2. WASMバックエンド

### 概要

WebAssemblyを生成し、ブラウザやNode.jsで実行可能にします。

### 実装方式

| 方式 | 説明 |
|------|------|
| **Rust経由** | Cm → Rust → wasm-pack → WASM |
| 直接生成 | Cm → HIR → WASM (将来) |

---

## 3. TypeScriptバックエンド

### 概要

TypeScriptコードを生成し、既存のJavaScriptエコシステムと統合します。

### 変換例

```cpp
// Cm (C++風構文)
interface Printable {
    void print();
};

struct User {
    String name;
    int age;
};

impl Printable for User {
    void print() {
        println("${this.name}: ${this.age}");
    }
};

// → TypeScript
interface Printable {
    print(): void;
}

interface User {
    name: string;
    age: number;
}

function User_print(self: User): void {
    console.log(`${self.name}: ${self.age}`);
}
```

### 型マッピング

| Cm | TypeScript |
|----|------------|
| `int` | `number` |
| `float` | `number` |
| `bool` | `boolean` |
| `String` | `string` |
| `Option<T>` | `T \| null` |
| `Result<T, E>` | `T \| E` |
| `Array<T>` | `T[]` |

---

## 4. インタープリター

### 概要

HIRを直接解釈実行し、高速な開発サイクルを実現します。

### 用途

- **REPL**: 対話的な実行環境
- **テスト**: 高速なテスト実行
- **開発**: コンパイル不要の即時実行

---

## ターゲット選択

```toml
# Cm.toml

[target.native]
backend = "rust"  # Rust経由

[target.web]
backend = "wasm"   # WASM (Rust経由)
# backend = "typescript"  # 代替

[target.dev]
backend = "interpreter"
```

### コマンドライン

```bash
# Rust経由ネイティブ
cm build --target native

# WASM出力
cm build --target web

# TypeScript出力
cm build --target typescript

# インタープリター実行
cm run
```

---

## 将来の拡張 (Phase 3)

| バックエンド | 用途 |
|-------------|------|
| **Cranelift** | 高速ネイティブ生成（Rustなし） |
| LLVM | 最適化重視 |
| C | 移植性最大化 |
