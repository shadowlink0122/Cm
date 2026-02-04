# LLVM インラインアセンブリ拡張ロードマップ

## 概要

OS開発、ベアメタルプログラミング、および`std::net`などの低レベルライブラリ開発を
サポートするためのLLVMインラインアセンブリ機能の拡張計画。

## 現状 (v0.13.0)

```cm
// 現在: インラインasm、ループ/ラベル、配列アクセス
__llvm__(`
    movl ${r:x}, %eax
    addl ${r:y}, %eax
    movl %eax, ${=r:result}
`);
```

**サポート済み:**
- 基本的な入出力制約 (`r`, `=r`, `+r`, `m`)
- ローカルラベルとジャンプ (`1:`, `jmp 1b`)
- 配列ポインタ経由のメモリアクセス
- callee-savedレジスタの保護

**未サポート:**
- asm関数定義
- 複数asmブロックの合成
- clobber指定
- 関数呼び出し

---

## フェーズ1: 基盤強化 (v0.14.0)

### 1.1 カスタムclobber指定

```cm
// 提案構文
__llvm__(`
    movq %rax, %rcx
`, clobbers: ["rax", "rcx", "memory"]);
```

**実装:**
- パーサー拡張: 第2引数でclobberリスト受付
- HIR: `InlineAsm`ノードにclobbersフィールド追加
- LLVM生成: `~{rax},~{rcx}`形式に変換

### 1.2 asm関数定義

```cm
// 提案構文
asm fn syscall3(num: i64, arg1: i64, arg2: i64, arg3: i64) -> i64 {
    __llvm__(`
        movq ${r:num}, %rax
        movq ${r:arg1}, %rdi
        movq ${r:arg2}, %rsi
        movq ${r:arg3}, %rdx
        syscall
        movq %rax, ${=r:__return__}
    `);
}
```

**実装:**
- 新AST: `AsmFunctionDecl`
- パーサー: `asm fn`キーワード
- 型チェック: 通常関数と同様
- LLVM生成: naked関数 + インラインasm

### 1.3 asm内からの関数呼び出し

```cm
// 提案構文
__llvm__(`
    call ${fn:helper_function}
`, calls: [helper_function]);
```

**実装:**
- シンボル解決: Cm関数名→LLVMシンボル名マッピング
- ABI自動処理: 引数/戻り値のレジスタ配置

---

## フェーズ2: OS/ベアメタル向け (v0.15.0)

### 2.1 割り込みハンドラ

```cm
#[interrupt]
asm fn timer_handler() {
    __llvm__(`
        pushq %rax
        # 割り込み処理
        popq %rax
        iretq
    `);
}
```

### 2.2 naked関数

```cm
#[naked]
fn context_switch(old: *Context, new: *Context) {
    __llvm__(`
        # レジスタ保存
        pushq %rbx
        pushq %r12
        ...
        # スタック切り替え
        movq %rsp, (%rdi)
        movq (%rsi), %rsp
        # レジスタ復元
        popq %r12
        popq %rbx
        ret
    `);
}
```

### 2.3 リンカスクリプト統合

```cm
#[section(".text.boot")]
asm fn _start() {
    // ブートコード
}
```

---

## フェーズ3: std::net向け (v0.16.0)

### 3.1 システムコールラッパー

```cm
// std/sys/syscall.cm
module std::sys;

asm fn syscall(num: i64, args: i64[]) -> i64 {
    // プラットフォーム別実装
}

// Linux専用
#[cfg(linux)]
asm fn socket(domain: i32, type: i32, protocol: i32) -> i32 {
    __llvm__(`
        movl $$41, %eax    # SYS_socket
        syscall
    `);
}
```

### 3.2 SIMD最適化

```cm
// std/net/checksum.cm
asm fn ip_checksum(data: *u8, len: usize) -> u16 {
    __llvm__(`
        # SSE4.2 CRC32命令使用
        ...
    `);
}
```

---

## 実装優先順位

| 優先度 | 機能 | バージョン | 必要性 |
|--------|------|------------|--------|
| 高 | clobber指定 | v0.14.0 | レジスタ安全性 |
| 高 | asm関数定義 | v0.14.0 | コード再利用 |
| 中 | naked関数 | v0.15.0 | OS開発 |
| 中 | 割り込みハンドラ | v0.15.0 | OS開発 |
| 中 | システムコール | v0.16.0 | std::net |
| 低 | SIMD最適化 | v0.17.0 | パフォーマンス |

---

## 技術的考慮事項

### ABI互換性
- System V AMD64 ABI (Linux/macOS)
- Microsoft x64 ABI (Windows)
- ARM64 AAPCS

### 安全性
- `unsafe`ブロック必須化の検討
- スタックアライメント検証
- レジスタ状態の追跡

### テスト戦略
- QEMU/模擬環境でのOS起動テスト
- socketモックによるネットワークテスト
