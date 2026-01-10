[English](inline_asm.en.html)

# インラインアセンブリ設計

## 概要

Cm言語でインラインアセンブリをサポートし、低レベルアクセスとハードウェア固有機能の利用を可能にします。

## 設計原則

1. **LLVM IR埋め込み**: LLVMバックエンドでは`llvm.asm`を使用
2. **プラットフォーム固有**: ターゲットアーキテクチャに応じた構文
3. **型安全性**: 入出力の型チェック
4. **unsafeコンテキスト**: アセンブリは潜在的に危険

## 構文

### 基本構文

```cm
// LLVM IRスタイル（推奨）
unsafe {
    int result;
    asm {
        "add $0, $1, $2"
        : "=r"(result)           // 出力オペランド
        : "r"(10), "r"(20)       // 入力オペランド
        : "memory"               // クロバーリスト
    };
}

// GCCスタイル（互換性）
unsafe {
    int eax, ebx;
    asm volatile (
        "cpuid"
        : "=a"(eax), "=b"(ebx)   // 出力
        : "a"(0)                  // 入力
        : "ecx", "edx"            // クロバー
    );
}
```

### 簡略構文（出力のみ）

```cm
unsafe {
    int x;
    asm { "mov $0, #42" : "=r"(x) };
}
```

### 制約指定子

| 制約 | 意味 |
|------|------|
| `r` | 汎用レジスタ |
| `m` | メモリオペランド |
| `i` | 即値定数 |
| `a`, `b`, `c`, `d` | x86の特定レジスタ (eax, ebx, ecx, edx) |
| `=` | 書き込み専用（出力） |
| `+` | 読み書き両用 |
| `&` | 早期クロバー |

## AST設計

```cpp
struct InlineAsm {
    std::string assembly_code;              // アセンブリコード本体
    std::vector<AsmOperand> outputs;        // 出力オペランド
    std::vector<AsmOperand> inputs;         // 入力オペランド
    std::vector<std::string> clobbers;      // クロバーリスト
    bool is_volatile = false;               // volatile指定
    bool is_alignstack = false;             // スタックアライメント
    std::string dialect = "att";            // ATT or Intel構文
};

struct AsmOperand {
    std::string constraint;  // "=r", "r", "m" など
    ExprPtr expression;      // 変数または式
};
```

## 実装フェーズ

### Phase 1: パーサー拡張 ✅予定
- `asm` ブロックの認識
- オペランド解析
- 制約文字列のパース

### Phase 2: 型チェッカー ✅予定
- unsafe コンテキストチェック
- オペランド型の検証
- 制約と型の互換性確認

### Phase 3: HIR/MIR変換 ✅予定
- InlineAsmノードの追加
- オペランドの型情報保持

### Phase 4: LLVMバックエンド ✅予定
- `llvm::InlineAsm`の生成
- 制約文字列の変換
- レジスタ割り当て

### Phase 5: エラー処理 ✅予定
- アセンブリ構文エラー
- 制約違反の検出
- プラットフォーム固有警告

## 使用例

### 1. CPUID命令（x86）

```cm
unsafe int get_cpu_vendor() {
    int eax, ebx, ecx, edx;
    asm {
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0)
    };
    return ebx;
}
```

### 2. メモリバリア

```cm
unsafe void memory_barrier() {
    asm volatile {
        "mfence"
        :
        :
        : "memory"
    };
}
```

### 3. タイムスタンプカウンタ

```cm
unsafe uint64 rdtsc() {
    uint low, high;
    asm {
        "rdtsc"
        : "=a"(low), "=d"(high)
    };
    return (static_cast<uint64>(high) << 32) | low;
}
```

### 4. ポート入出力

```cm
unsafe uint8 inb(uint16 port) {
    uint8 value;
    asm {
        "inb %1, %0"
        : "=a"(value)
        : "d"(port)
    };
    return value;
}

unsafe void outb(uint16 port, uint8 value) {
    asm {
        "outb %0, %1"
        :
        : "a"(value), "d"(port)
    };
}
```

## プラットフォーム別サポート

### x86/x86_64 (LLVM)
- AT&T構文（デフォルト）
- Intel構文（`dialect="intel"`）
- すべてのx86命令

### ARM/AArch64 (LLVM)
- ARM Unified Assembly Language
- NEON命令サポート

### WASM
- インラインアセンブリは**非サポート**
- コンパイル時エラー

## エラー処理

```cm
Error: Inline assembly is not supported in WASM target
  --> main.cm:10:5
   |
10 |     asm { "cpuid" : "=a"(eax) : "a"(0) };
   |     ^^^ cannot use inline assembly in WASM
   |
   = help: Consider using platform-specific functions

Error: Inline assembly must be in unsafe context
  --> main.cm:5:5
   |
5  |     asm { "nop" };
   |     ^^^ unsafe operation outside unsafe block
   |
   = help: Wrap in `unsafe { ... }`

Error: Invalid constraint '=x' for operand type 'int'
  --> main.cm:8:20
   |
8  |     asm { "mov $0, #42" : "=x"(result) };
   |                           ^^^^
   |
   = help: Use '=r' for general purpose register
```

## 制限事項

1. **WASMターゲット**: インラインアセンブリ非サポート
2. **インタプリタ**: 実行不可（スキップまたはエラー）
3. **型安全性**: 限定的（アセンブリレベルでは型なし）
4. **移植性**: プラットフォーム固有コードとなる

## 代替案（推奨）

インラインアセンブリの代わりに、以下を推奨：

1. **LLVM IR組み込み関数**: `@llvm.ctpop.i32`など
2. **extern "C"**: 外部アセンブリファイルとリンク
3. **プラットフォーム抽象化**: OSごとの実装を切り替え

```cm
// 推奨: LLVM組み込み関数
extern "llvm" {
    int ctpop_i32(int value);  // ビットカウント
}

// 推奨: 外部関数
extern "C" {
    void custom_asm_func();
}
```

## 実装状況

| 機能 | パーサー | 型チェック | HIR/MIR | LLVM | WASM | テスト |
|------|----------|-----------|---------|------|------|------|
| asm {} ブロック | ⬜ | ⬜ | ⬜ | ⬜ | - | ⬜ |
| 出力オペランド | ⬜ | ⬜ | ⬜ | ⬜ | - | ⬜ |
| 入力オペランド | ⬜ | ⬜ | ⬜ | ⬜ | - | ⬜ |
| クロバーリスト | ⬜ | ⬜ | ⬜ | ⬜ | - | ⬜ |
| volatile指定 | ⬜ | ⬜ | ⬜ | ⬜ | - | ⬜ |
| unsafeチェック | ⬜ | ⬜ | - | - | - | ⬜ |
| WASMエラー | - | ⬜ | - | - | ⬜ | ⬜ |

## 次のステップ

1. TokenKindに`KwAsm`, `KwUnsafe`, `KwVolatile`を追加
2. パーサーに`parse_inline_asm()`を実装
3. ASTに`InlineAsmExpr`/`InlineAsmStmt`を追加
4. 型チェッカーでunsafeコンテキストを検証
5. LLVMバックエンドで`llvm::InlineAsm::get()`を呼び出し