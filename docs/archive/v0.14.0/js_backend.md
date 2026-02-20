# Cm → JavaScript バックエンド設計ドキュメント (v0.14.0)

## 1. 概要

Cm言語からJavaScriptへのコンパイルを本格的に整備する。
MIRベースの既存JSコードジェネレータを改善し、テスト通過率を55% → 90%以上に引き上げる。

### 現状 (v0.13.1)

| 項目 | 値 |
|------|-----|
| テスト総数 | 372 |
| パス | 206 (55%) |
| 失敗 | 119 (32%) |
| スキップ | 47 (13%) |

### 失敗カテゴリ分析

| カテゴリ | 失敗数 | 主因 |
|---------|--------|------|
| array | 16 | インライン変数未定義、ポインタ配列 |
| asm | 11 | JSでは__asm__非対応（構文エラーにすべき） |
| iterator | 9 | ポインタベースのイテレータ非対応 |
| collections | 9 | Vector/HashMap等のポインタ操作非対応 |
| types | 5 | union配列、型キャスト |
| sync | 5 | スレッド/atomic非対応（スキップすべき） |
| enum | 5 | matchのコード生成不備 |
| control_flow | 5 | 制御フロー復元の不備 |
| thread | 4 | JSではスレッド非対応（スキップすべき） |
| structs | 4 | 構造体のフィールドアクセス |
| io | 4 | ファイルI/O非対応 |
| interface | 4 | VTableディスパッチ |
| casting | 4 | 型キャスト |
| その他 | 28 | 各種 |

## 2. 設計方針

### 2.1 JS非対応機能の構文エラー化

以下の機能はJSターゲット時にコンパイルエラーとする:

```
- ポインタ型 (int*, void*, T*)
- ポインタ演算 (&, *, ->)
- インラインアセンブリ (__asm__)
- スレッド (spawn, join, sleep_ms)
- Atomic操作 (fetch_add, load, store)
- Mutex/Channel
- GPU計算 (Metal API)
- 低レベルメモリ操作 (alloc, dealloc, memcpy)
- TCP/HTTPソケット (std::net)
```

> [!IMPORTANT]
> これらは **MIR生成前のフロントエンド段階** でエラーを出すのが理想。
> MIR段階でのチェックでも可。

### 2.2 修正優先度

#### Phase 1: コア修正 (インライン変数バグ)
- `ReferenceError: _tNNNN_NN is not defined` の修正
- MIRのインライン最適化(`collectInlineCandidates`, `precomputeInlineValues`)のバグ修正
- これだけで **大量のテストが一気に通る** 可能性が高い

#### Phase 2: 制御フローとenum/match
- enum match文のコード生成修正
- 制御フロー復元（if-else, loop）の改善
- switch文のフォールスルー対応

#### Phase 3: 型変換とキャスト
- 数値型間のキャスト改善
- 文字列←→数値変換
- union型の正しいタグ処理

#### Phase 4: 非対応機能のエラーメッセージ
- ポインタ系構文のコンパイルエラー化
- `.skip`ファイルの整理（asm, sync, thread, gpu, net → jsスキップ）

## 3. アーキテクチャ

```
Cm Source → Parser → AST → HIR → MIR → JSCodeGen → output.js
                                          ↓
                                    ┌────────────┐
                                    │ codegen.cpp │ メインコンパイラ
                                    │ emit_*.cpp  │ 式・文の出力
                                    │ types.hpp   │ 型マッピング
                                    │ runtime.hpp │ ランタイムヘルパー
                                    │ builtins.hpp│ ビルトイン関数
                                    └────────────┘
```

### 3.1 既存コードジェンの構造

| ファイル | 行数 | 役割 |
|---------|------|------|
| `codegen.cpp` | 1852 | メインコンパイラ、関数/構造体出力 |
| `emit_expressions.cpp` | 452 | Rvalue、オペランド、Place出力 |
| `emit_statements.cpp` | 482 | 基本ブロック、文、終端命令出力 |
| `control_flow.cpp` | ~300 | MIR→構造化制御フロー変換 |
| `types.hpp` | 151 | 型名/デフォルト値マッピング |
| `runtime.hpp` | 274 | ランタイムヘルパー（format, clone等） |
| `builtins.hpp` | ~300 | ビルトイン関数マッピング |
| `emitter.hpp` | ~50 | コード出力ユーティリティ |

## 4. 検証計画

### 自動テスト
```bash
# JSバックエンドテスト実行
make test-js

# 特定カテゴリのみ
OPT_LEVEL=0 tests/unified_test_runner.sh -b js -c "array,enum,control_flow"
```

### マイルストーン
| Phase | 目標パス率 | 失敗数目標 |
|-------|-----------|-----------|
| Phase 1 完了 | 75% (280/372) | ~45 |
| Phase 2 完了 | 85% (316/372) | ~20 |
| Phase 3 完了 | 90% (335/372) | ~10 |
| Phase 4 完了 | 90%+ | 残りはスキップ |
