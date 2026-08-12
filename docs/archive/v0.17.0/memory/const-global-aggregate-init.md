---
title: B1 const集約グローバルのrodata書き込み
parent: v0.17.0 Design
---

# B1: const集約グローバルのrodata書き込み

## 対象バグ

| # | 領域 | 概要 | 重大度 | 状態 |
|---|------|------|--------|------|
| B1 | グローバル | const集約グローバルがrodata定数へのstoreという不正IRになり、O0でSIGBUSクラッシュ・最適化時に誤値 | Critical | 修正済み |

## 再現コード

```cm
const int[3] W = [2, 4, 6];

int main() {
    return W[0];
}
```

## 現象

トップレベルのconst集約が `internal constant ... zeroinitializer` として発行され、初期化がmain先頭のstore列で行われる。
読み取り専用セクションへの書き込みになるためO0はSIGBUSでクラッシュし、O3はstoreが読み出しと順序保証されず初期化前の0を返す。
構文→LLVM IR対訳リファレンス（docs/architecture/codegen/declarations/global-decl.md）の実機検証で検出した。

## 根因（確定）

MIRの`MirGlobalVar.init_value`はスカラ定数しか保持できず、配列・構造体リテラルは`init_expr`（HIR式）のまま残る。
MIR loweringは`init_expr`を持つ全グローバルをmainエントリのstore列で初期化する一方、LLVMコード生成はconstを`internal constant zeroinitializer`として発行するため、「rodata定数+mainからのstore」という矛盾したIRになる。

## 実装内容

- `foldConstInitExpr`（src/internal/codegen/llvm/core/translate/program.cpp）を追加し、宣言型主導でリテラル・単項マイナス・配列リテラル（多次元・不足分ゼロ埋め）・構造体リテラル（フィールド名→定義順写像・省略ゼロ埋め・文字列はヘッダ付きリテラル）をLLVM定数へ畳み込む
- 単一プログラム経路・モジュール分割の定義側/extern宣言側の3経路で、畳み込み成功時はconstantのinitializerへ直接埋め込み、畳み込めない`init_expr`付きconstは可変グローバルへ降格して従来のmainエントリ初期化を維持する
- 畳み込み済みconstグローバルを宛先とするmainエントリの初期化storeはスキップする（src/internal/codegen/llvm/core/statement/assign.cpp）
- constスカラ（init_value経由）と非constグローバルの経路は不変更

## テスト

- tests/common/global_var/const_aggregate_global.cm — const int配列・double配列・2次元配列・構造体（負数/文字列フィールド）の読み出しと関数越しループ和
- 検証済み: 修正前O0のSIGBUS（exit 138）が解消しnative O0/O2/O3・jitとも正値、`W`が`__TEXT,__const`へ定数配置されstore列が消滅、関数呼び出し初期化子とスライスは可変降格で正値、global_var/const系11件O0/O2・arrays+basic 35件・interpreter/jit各8件PASS
