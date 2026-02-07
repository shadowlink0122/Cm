# Cm言語 診断カタログ

**文書番号:** 080
**作成日:** 2026-01-13
**バージョン:** v0.12.0
**ステータス:** カタログ

## 1. 診断番号体系

```
E001-E999: エラー（コンパイル停止）
W001-W999: 警告（コンパイル継続）
L001-L999: Lintルール（スタイル・最適化）
```

## 2. 重要な診断（必須実装）

### コンパイラエラー（v0.11.0必須）

| ID | 名称 | メッセージ |
|----|------|-----------|
| **E016** | missing-const | Variables require 'const' or 'mut' |
| **E100** | type-mismatch | Type mismatch: expected '{0}', got '{1}' |
| **E101** | undefined-variable | Variable '{0}' is not defined |
| **E200** | use-after-move | Variable '{0}' used after move |
| **E201** | modify-while-borrowed | Cannot modify '{0}' while borrowed |

### スタイルルール（デフォルト設定）

| ID | 設定項目 | デフォルト値 |
|----|----------|-------------|
| **L001** | indent-size | **4 spaces** |
| **L100** | function-naming | **snake_case** |
| **L101** | variable-naming | **snake_case** |
| **L102** | constant-naming | **UPPER_SNAKE_CASE** |
| **L103** | type-naming | **PascalCase** |

## 3. 診断カテゴリ一覧

### エラー (E)
- **E001-E099**: 構文エラー
- **E100-E199**: 型エラー
- **E200-E299**: 所有権エラー
- **E300-E399**: ポインタエラー（v0.13.0）
- **E400-E499**: ジェネリクスエラー（v0.13.0）
- **E500-E599**: enum/matchエラー（v0.13.0）
- **E600-E699**: リテラル/定数エラー（v0.13.0）

### 警告 (W)
- **W001-W099**: 未使用検出
- **W100-W199**: ポインタ警告（v0.13.0）
- **W200-W299**: ジェネリクス警告（v0.13.0）
- **W300-W399**: enum/match警告（v0.13.0）
- **W400-W499**: リテラル/定数警告（v0.13.0）

### Lintルール (L)
- **L001-L099**: スタイル（インデント、空白、ブレース）
- **L100-L199**: パフォーマンスルール
- **L200-L299**: ジェネリクスルール（v0.13.0）
- **L300-L399**: ポインタルール（v0.13.0）
- **L400-L499**: enum/matchルール（v0.13.0）

## 4. 主要診断の詳細

### 4.1 構文エラー (E001-E016)

```cpp
// E001: unexpected-token
int x = ;  // エラー: 予期しないトークン

// E002: missing-semicolon
int x = 10  // エラー: セミコロンが必要

// E016: missing-const (v0.11.0必須)
int x = 10;  // エラー: const/mutが必要
const int x = 10;  // 正しい
```

### 4.2 命名規則 (L100-L114)

| 対象 | ルール | 正しい例 | 間違った例 |
|------|-------|---------|------------|
| 関数 | snake_case | `calculate_sum` | `CalculateSum` |
| 変数 | snake_case | `user_count` | `userCount` |
| 定数 | UPPER_SNAKE_CASE | `MAX_SIZE` | `maxSize` |
| 型 | PascalCase | `UserAccount` | `user_account` |
| Enum | PascalCase | `ErrorCode` | `error_code` |
| ジェネリック | PascalCase/1文字 | `T`, `Element` | `element` |

### 4.3 複雑度制限 (L150-L159)

| ID | 項目 | デフォルト制限 |
|----|------|---------------|
| L150 | 循環的複雑度 | 10 |
| L151 | ネスト深度 | 4 |
| L152 | パラメータ数 | 5 |
| L153 | 関数行数 | 50 |

## 5. 設定例

### 5.1 標準設定 (.cmlint.yml)

```yaml
# インデント必須
rules:
  L001:
    enabled: true
    level: error
    indent_size: 4

  # 命名規則
  L100: { pattern: snake_case }
  L102: { pattern: UPPER_SNAKE_CASE }
  L103: { pattern: PascalCase }

  # 未使用検出
  W001: warning
```

### 5.2 厳格モード

```yaml
severity:
  default: error

rules:
  # すべてをエラーレベルに
  W001: error  # 未使用変数はエラー
  L001: error  # インデント違反はエラー
  L100: error  # 命名規則違反はエラー

  L150:
    level: error
    max_complexity: 8  # より厳しい制限
```

## 6. 実行モード

```bash
# コンパイル時: E/Wのみ
cm build main.cm

# フルチェック: E/W/L全て
cm check main.cm

# 自動修正
cm check main.cm --fix

# 特定ルールのみ
cm check main.cm --rules=L001,L100-L103
```

## 7. 診断出力例

```
src/main.cm:10:5: error: Variables require 'const' or 'mut' [E016]
    int x = 10;
    ^~~

src/utils.cm:25:1: warning: Function name should be snake_case [L100]
    int CalculateSum() {
        ^~~~~~~~~~~~
    Fix available: calculate_sum

src/main.cm:50:1: info: Indentation should be 4 spaces [L001]
  if (condition) {
  ^
  Expected: 4 spaces, Found: 2 spaces

Found 1 error, 1 warning, 1 hint.
```

## 8. 実装優先度

### Phase 1（必須）- 1週間
- E001-E016, E100-E115: 基本エラー
- E200-E209: 所有権エラー
- W001-W009: 未使用警告
- L001, L100-L103: 基本スタイル

### Phase 2（重要）- 1週間
- W100-W299: 各種警告
- L150-L159: 複雑度
- 自動修正機能

### Phase 3（推奨）- 2週間
- L250-L399: ベストプラクティス・最適化
- インクリメンタル解析
- VSCode/LSP統合

## 9. 関連ドキュメント

- [079_unified_diagnostic_system.md](./079_unified_diagnostic_system.md) - メイン設計
- [081_implementation_guide.md](./081_implementation_guide.md) - 実装ガイド
- [078_linter_formatter_design.md](./078_linter_formatter_design.md) - 初期設計