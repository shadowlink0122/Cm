# インラインアセンブリ設計書

## 概要

Cm言語のインラインアセンブリは、低レベルのハードウェア操作を可能にする機能です。
本設計書では、完全なインラインアセンブリ実装のアーキテクチャを定義します。

## アーキテクチャ

```
┌──────────────┐
│ Cm Source    │  asm("mov {a}, %eax")
└──────┬───────┘
       │
┌──────▼───────┐
│  AST Node    │  AsmStatement
└──────┬───────┘
       │
┌──────▼───────┐
│   HIR/MIR    │  MirRvalue::Asm
└──────┬───────┘
       │
┌──────▼───────────────┐
│   Asm Parser         │  Result<ParsedAsm, AsmError>
│  - 構文解析          │
│  - 変数補間検出      │
│  - オペランド分析    │
└──────┬───────────────┘
       │
       ├────────────────┬─────────────────┐
       │                │                 │
┌──────▼──────┐  ┌─────▼──────┐  ┌──────▼──────┐
│ Interpreter │  │    LLVM    │  │    x86     │
│  Backend    │  │   Backend  │  │  Backend   │
└─────────────┘  └────────────┘  └─────────────┘
```

## コンポーネント

### 1. Result型によるエラーハンドリング

```cpp
template <typename T, typename E = std::string>
class Result {
    // 成功または失敗を型安全に表現
};
```

### 2. AsmParser（構文解析器）

**責務:**
- アセンブリコードの構文解析
- 変数補間の検出
- Intel/AT&T構文の判定

**入力:** `std::string` (アセンブリコード)
**出力:** `Result<ParsedAsm, AsmError>`

### 3. LLVMConstraintBuilder（LLVM制約生成器）

**責務:**
- GCC拡張アセンブリ形式への変換
- 制約文字列の生成（"=r", "+m" など）
- クロバーリストの推定

**入力:** `ParsedAsm`
**出力:** `Result<LLVMAsm, AsmError>`

### 4. バックエンド実装

#### インタプリタ
- 仮想レジスタによるシミュレーション
- 実メモリアドレスへのアクセス（std::any_cast経由）

#### LLVM
- llvm::InlineAsm APIの使用
- GCC互換制約システム

#### ネイティブx86（将来）
- 直接的なマシンコード生成

## 制約システム

### 基本制約

| 制約 | 説明 | 例 |
|-----|------|-----|
| `"r"` | 任意のレジスタ | `"r"(value)` |
| `"m"` | メモリ位置 | `"m"(variable)` |
| `"i"` | 即値 | `"i"(10)` |
| `"="` | 出力のみ | `"=r"(result)` |
| `"+"` | 入出力 | `"+m"(counter)` |
| `"&"` | アーリークロバー | `"=&r"(temp)` |

### 特殊制約

| 制約 | 説明 | アーキテクチャ |
|-----|------|--------------|
| `"a"` | %eax/%rax | x86/x86_64 |
| `"b"` | %ebx/%rbx | x86/x86_64 |
| `"c"` | %ecx/%rcx | x86/x86_64 |
| `"d"` | %edx/%rdx | x86/x86_64 |
| `"S"` | %esi/%rsi | x86/x86_64 |
| `"D"` | %edi/%rdi | x86/x86_64 |

## 変数補間

### Cm構文
```cm
int a = 10;
int b = 20;
int result;

asm("mov {a}, %eax");        // 値を読み込み
asm("add {b}, %eax");        // 値を加算
asm("mov %eax, {result}");   // 値を格納
asm("mov {&a:x}, %ebx");     // アドレスを16進で展開
```

### 変換プロセス

1. **パース段階**
   ```
   {a}      → Variable(name="a", is_address=false)
   {&b}     → Variable(name="b", is_address=true)
   {&c:x}   → Variable(name="c", is_address=true, format="x")
   ```

2. **制約生成**
   ```
   入力:  {a}     → "m"(local_a)
   出力:  {result} → "=m"(local_result)
   アドレス: {&b}  → "r"(&local_b)
   ```

3. **LLVM IR生成**
   ```llvm
   %0 = call asm "mov $0, %eax\n\tadd $1, %eax\n\tmov %eax, $2",
                 "m,m,=m"(i32* %a, i32* %b, i32* %result)
   ```

## エラーハンドリング

### エラー種別

```cpp
enum class AsmErrorKind {
    InvalidSyntax,           // 構文エラー
    UnsupportedInstruction,  // サポートされていない命令
    InvalidOperand,          // 無効なオペランド
    InvalidConstraint,       // 無効な制約
    VariableNotFound,        // 変数が見つからない
    TypeMismatch,           // 型の不一致
    RegisterAllocationFailed, // レジスタ割り当て失敗
    InvalidMemoryAccess      // 無効なメモリアクセス
};
```

### エラー伝播

```cpp
// Result型によるエラー伝播
auto parse_result = AsmParser::parse(asm_code);
if (parse_result.is_err()) {
    return compile_error(parse_result.unwrap_err().to_string());
}

auto parsed = parse_result.unwrap();
auto constraint_result = LLVMConstraintBuilder::build(parsed);
// ...
```

## 実装ステータス

### 完了
- [x] Result型の定義
- [x] エラー型の定義
- [x] AsmParserの基本実装
- [x] LLVMConstraintBuilderの基本実装

### 進行中
- [ ] MIR → LLVM変換の統合
- [ ] インタプリタバックエンドの完全実装
- [ ] テストケースの整備

### 未実装
- [ ] Intel ↔ AT&T構文の自動変換
- [ ] 複雑な制約パターンのサポート
- [ ] アーキテクチャ固有の最適化
- [ ] インラインアセンブリのデバッガサポート

## テスト戦略

### ユニットテスト
- パーサーテスト
- 制約生成テスト
- エラーハンドリングテスト

### 統合テスト
- 各バックエンドでの実行テスト
- パフォーマンステスト
- 互換性テスト

### テストケース例

```cm
// 基本的な値の操作
test("basic_mov") {
    int a = 42;
    int result = 0;
    asm("mov {a}, %eax");
    asm("mov %eax, {result}");
    assert(result == 42);
}

// 算術演算
test("arithmetic") {
    int a = 10, b = 20, result;
    asm("mov {a}, %eax");
    asm("add {b}, %eax");
    asm("mov %eax, {result}");
    assert(result == 30);
}

// 条件分岐
test("comparison") {
    int a = 10, b = 10;
    bool equal;
    asm("cmp {a}, {b}");
    asm("sete %al");
    asm("movzbl %al, {equal}");
    assert(equal == true);
}
```

## まとめ

完全なインラインアセンブリ実装には以下が必要です：

1. **堅牢なエラーハンドリング** - Result型による型安全なエラー処理
2. **正確な構文解析** - Intel/AT&T両構文のサポート
3. **適切な制約生成** - GCC互換の制約システム
4. **バックエンド統合** - 各実行環境での一貫した動作

フォールバックロジックを排除し、エラーを明示的に扱うことで、より信頼性の高いシステムを構築できます。