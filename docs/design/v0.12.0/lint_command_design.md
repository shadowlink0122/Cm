# cm lint コマンド設計

## 概要
`cm lint` は Cm 言語の静的解析コマンドです。コードの品質向上のための警告・提案を提供します。

## 機能一覧

### Phase 1: 基本 Lint ルール（v0.12.0）
| ルール | 説明 | 
|--------|------|
| unused-variable | 未使用変数の検出 |
| unused-import | 未使用 import の検出 |
| const-recommend | const 推奨警告（変更されない変数） |
| dead-code | 到達不能コードの検出 |

### Phase 2: 拡張ルール（v0.13.0+）
| ルール | 説明 |
|--------|------|
| redundant-clone | 不要な clone 呼び出し |
| implicit-return | 暗黙の戻り値警告 |
| naming-convention | 命名規則チェック |

## CLI インターフェース

```bash
cm lint <file>                    # 基本lint
cm lint --fix <file>              # 自動修正（将来）
cm lint --rule=unused-variable <file>  # 特定ルールのみ
cm lint --config=.cmlint.json <file>   # 設定ファイル使用
```

## 実装構造

```
src/
├── lint/
│   ├── lint_pass.hpp         # 基底クラス
│   ├── unused_variable.hpp   # 未使用変数検出
│   ├── unused_import.hpp     # 未使用import検出
│   ├── const_recommend.hpp   # const推奨
│   ├── dead_code.hpp         # デッドコード検出
│   └── lint_runner.hpp       # lint実行エンジン
```

## アーキテクチャ

```mermaid
graph TD
    A[Source Code] --> B[Lexer]
    B --> C[Parser]
    C --> D[Type Checker]
    D --> E[Lint Runner]
    E --> F1[UnusedVariable Pass]
    E --> F2[UnusedImport Pass]
    E --> F3[ConstRecommend Pass]
    E --> F4[DeadCode Pass]
    F1 --> G[Diagnostics]
    F2 --> G
    F3 --> G
    F4 --> G
