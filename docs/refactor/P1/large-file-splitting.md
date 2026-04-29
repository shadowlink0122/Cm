# 巨大ファイルの分割

**優先度**: 中  
**影響範囲**: ビルド時間、保守性  
**対象ファイル**: 複数  
**必要テスト**: 分割後の各モジュールに対するユニットテスト

---

## 問題

2000行を超える巨大ファイルが複数存在する。

| ファイル | 行数 | 責務 |
|---------|------|------|
| `mir_to_llvm.cpp` | 5,026 | MIR→LLVM IR変換 |
| `lowering.cpp` | 2,874 | AST→MIR lowering |
| `expr_call.cpp` | 2,821 | 関数呼び出し式のlowering |
| `import.cpp` | 2,803 | モジュールインポート |
| `monomorphization_impl.cpp` | 2,768 | ジェネリクス特殊化 |
| `sv/codegen.cpp` | 2,607 | SVコード生成 |
| `hir/expr.cpp` | 2,600 | 式のHIR lowering |

---

## 修正案

### mir_to_llvm.cpp (5,026行)

分割候補:
- `mir_to_llvm_types.cpp` - 型変換
- `mir_to_llvm_operators.cpp` - 演算子変換
- `mir_to_llvm_control.cpp` - 制御フロー
- `mir_to_llvm_memory.cpp` - メモリ操作
- `mir_to_llvm_call.cpp` - 関数呼び出し

### sv/codegen.cpp (2,607行)

分割候補:
- `sv_module.cpp` - モジュール生成
- `sv_expressions.cpp` - 式生成
- `sv_statements.cpp` - 文生成
- `sv_optimizations.cpp` - 最適化（インライン展開、三項演算子変換）

### expr_call.cpp (2,821行)

分割候補:
- `expr_call_method.cpp` - メソッド呼び出し
- `expr_call_generic.cpp` - ジェネリック関数呼び出し
- `expr_call_builtin.cpp` - ビルトイン関数

---

## 目標

- 1ファイル1000行以下
- 単一責任の原則
- テスト可能な単位への分割

---

## 影響

- インクリメンタルビルドの高速化
- コードナビゲーションの改善
- レビューの容易化

