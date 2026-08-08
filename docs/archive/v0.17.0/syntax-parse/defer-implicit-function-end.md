---
title: B9 暗黙の関数終端でdeferが発火しない
parent: v0.17.0 Design
---

# B9: 暗黙の関数終端でdeferが発火しない

## 対象バグ

| # | 領域 | 概要 | 重大度 | 状態 |
|---|------|------|--------|------|
| B9 | defer | 暗黙の関数終端（return省略）でdeferが発火しない | Medium | 修正済み |

## 再現コード

```cm
void f() {
    defer println("cleanup");
    println("body");
}  // 明示returnが無いとcleanupが出力されない

int main() {
    f();
    return 0;
}
```

## 現象

deferの展開がreturn/break/continueのlowering地点でのみ行われ、void関数でreturnを省略した暗黙の関数終端に複製されない。
構文→LLVM IR対訳リファレンスのcontrol-flow検証で検出した（明示return形では正常に逆順実行される）。

## 根因（確定）

lower_function（src/internal/mir/lowering/impl.cpp）の暗黙return生成がemit_destructors→retを直接発行し、明示returnにあるdefer展開（get_defer_stmts()）を通していなかった。

## 実装内容

- 終端未設定ブロックでデフォルトreturnを発行する前に、明示returnと同一のdefer逆順展開を挿入（src/internal/mir/lowering/impl.cpp）
- defer逆順→dtor逆順の既存順序規約を維持

## テスト

- tests/common/defer/defer_implicit_return.cm — return省略void関数の単一defer発火
- tests/common/defer/defer_implicit_multi.cm — 複数deferの逆順発火と明示return関数との出力一致
- 検証済み: jit O0/O2で新規テストPASS、defer/types/structs等の既存回帰スイート全PASS
