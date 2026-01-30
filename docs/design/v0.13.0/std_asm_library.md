# std::asm 純粋Cmライブラリ化計画

## 現状分析

### 既存のインフラ（実装済み）

| レイヤー | 実装 | 場所 |
|---------|------|------|
| HIR | `HirAsm` | `src/hir/nodes.hpp:282` |
| MIR | `MirStatement::Asm` | `src/mir/nodes.hpp:372` |
| LLVM | `InlineAsm` 生成 | `src/codegen/llvm/core/mir_to_llvm.cpp:1388` |

### 未実装

| 機能 | 説明 |
|------|------|
| `__builtin_asm()` | Cm → HirAsm 変換がない |
| HIR lowering | CallExprから HirAsm への変換がない |

---

## 必要な実装

### Phase 1: `__builtin_asm()` 組み込み関数

**ファイル**: `src/hir/lowering/stmt.cpp`

```cpp
// CallExpr を検出し、 __builtin_asm の場合は HirAsm に変換
if (call.func_name == "__builtin_asm") {
    auto asm_stmt = std::make_unique<HirAsm>();
    asm_stmt->code = get_string_literal(call.args[0]);
    asm_stmt->is_volatile = true;
    return HirStmt(std::move(asm_stmt));
}
```

### Phase 2: MIR lowering

**ファイル**: `src/mir/lowering/stmt.hpp`

```cpp
// HirAsm → MirStatement::Asm
case HirAsm: {
    auto& asm_data = std::get<std::unique_ptr<HirAsm>>(stmt.kind);
    return mir::MirStatement::createAsm(
        asm_data->code,
        {},  // inputs
        asm_data->clobbers,
        asm_data->is_volatile
    );
}
```

---

## 実装ロードマップ

```
Phase 1: __builtin_asm 基本実装
├── [ ] src/hir/lowering/stmt.cpp - __builtin_asm 検出
├── [ ] src/hir/lowering/expr.cpp - 文字列リテラル抽出
└── [ ] テスト: tests/test_programs/asm/builtin_asm.cm

Phase 2: MIR lowering 接続
├── [ ] src/mir/lowering/stmt.hpp - HirAsm → MirAsm
└── [ ] 既存のLLVM codegen確認

Phase 3: std::asm ライブラリ化
├── [ ] std/asm/mod.cm 書き換え
├── [ ] runtime_asm.c 削除
└── [ ] テスト更新
```

---

## 想定される変更後のstd::asm

```cm
// std/asm/mod.cm - 純粋Cm実装
module std.asm;

export void asm(string code) {
    __builtin_asm(code);
}

export void nop() {
    __builtin_asm("nop");
}

export void barrier() {
    #if x86
        __builtin_asm("mfence");
    #elif arm64
        __builtin_asm("dmb sy");
    #endif
}
```
