# BNFパーサー統合実装まとめ

作成日: 2026-01-11
ステータス: 設計完了・ツール実装済み

## 実施内容まとめ

### 1. 作成したドキュメント

| ファイル | 内容 | 状態 |
|---------|------|------|
| `070_generic_constraint_separation.md` | ジェネリック制約分離設計 | ✅ 修正完了 |
| `071_bnf_parser_refactoring.md` | BNFパーサーリファクタリング設計 | ✅ 完了 |
| `072_bnf_parser_integration.md` | BNF統合詳細設計 | ✅ 完了 |
| `docs/design/cm_grammar.bnf` | Cm言語完全BNF文法定義 | ✅ 完了 |

### 2. 実装したツール

#### BNF検証・出力ツール
- **場所**: `scripts/validate_bnf.py`
- **機能**:
  - BNF文法の検証（エラー・警告検出）
  - 複数フォーマット出力（text, HTML, DOT）
  - 統計情報表示

### 3. 生成された出力ファイル

| ファイル | 形式 | 用途 |
|---------|------|------|
| `cm_grammar_output.txt` | テキスト | 文法リファレンス |
| `cm_grammar.html` | HTML | Web表示用文法ドキュメント |
| `cm_grammar.dot` | Graphviz DOT | 文法構造の可視化 |

## 主要な設計決定

### 1. 構文の明確化

#### ジェネリック制約の分離
```cm
// 型制約（ジェネリック宣言時）
<T: int | double>          // Tは具体型のリスト
<N: const int>             // Nはコンパイル時定数（現行構文維持）

// インターフェース境界（where句のみ）
<T> T max(T a, T b) where T: Comparable {
    return a > b ? a : b;
}
```

**重要な修正**:
- `<N: const int>` が正しい構文（変更なし）
- where句はブロック直前に配置

### 2. BNFパーサー統合アーキテクチャ

```
BNF文法ファイル (cm_grammar.bnf)
    ↓
BNFパーサージェネレーター
    ↓
生成されたパーサー基底クラス
    ↓
Cmパーサー実装（既存コード活用）
```

### 3. フォルダ構成

```
src/frontend/
├── lexer/           # レキサー（既存）
├── parser/          # パーサー（再構成）
│   ├── bnf/        # BNF関連（新規）
│   ├── generated/  # 生成コード
│   ├── cm/         # Cm固有実装
│   └── utils/      # ユーティリティ
├── ast/            # AST（既存）
├── semantic/       # 意味解析（分離）
└── tools/          # ツール（新規）
    ├── bnf2cpp/    # BNF→C++生成器
    └── validate_bnf.py
```

## BNF文法の概要

### 統計情報
- **開始記号**: program
- **プロダクション数**: 99
- **終端記号数**: 140
- **非終端記号数**: 99

### 主要な文法構造

```bnf
# プログラム構造
program ::= top_level_decl*

# ジェネリクス
generic_params ::= '<' generic_param_list '>'
generic_param ::= type_param | const_param

# where句（ブロック直前）
where_clause ::= 'where' where_bound (',' where_bound)*
where_bound ::= identifier ':' interface_bound

# 型システム
type ::= primitive_type
       | type '[' const_expr ']'    # 配列
       | type '*'                    # ポインタ
       | type '&'                    # 参照
       | union_type                  # int | double
```

## 実装計画

### Phase 1: BNF検証と改善（1週間）
- [x] BNF文法ファイル作成
- [x] 検証ツール実装
- [x] 文法出力生成
- [ ] エラー・警告の解決

### Phase 2: パーサージェネレーター（2週間）
- [ ] bnf2cppツール実装
- [ ] 生成コードのテンプレート作成
- [ ] 最適化（左再帰除去など）

### Phase 3: 統合実装（2週間）
- [ ] フォルダ構造の再編成
- [ ] BNFパーサー基底クラス実装
- [ ] 既存パーサーとの統合

### Phase 4: テストと最適化（1週間）
- [ ] 回帰テスト作成
- [ ] パフォーマンス測定
- [ ] 最適化実装

## 現在のBNF検証結果

### 検出された問題（要改善）
1. **左再帰**: 式の評価順序に左再帰が存在
   - 解決策: 左再帰除去変換を適用

2. **LL(1)競合**: 一部のルールで先読み競合
   - 解決策: 共通プレフィックスの因数分解

3. **EBNF記法**: バリデーターがEBNF演算子を正しく処理できていない
   - 解決策: バリデーターの改善

## 利点

### 1. 明確な文法定義
- 正式なBNF仕様書による曖昧性の排除
- 文法の可視化とドキュメント自動生成

### 2. 保守性の向上
- 文法変更が容易
- 影響範囲が明確
- テストの自動生成

### 3. ツールサポート
- IDE向け構文定義の自動生成
- 静的解析ツールの作成が容易
- 文法検証の自動化

## 使用方法

### BNF検証
```bash
python3 scripts/validate_bnf.py docs/design/cm_grammar.bnf --validate
```

### 文法出力生成
```bash
# テキスト形式
python3 scripts/validate_bnf.py docs/design/cm_grammar.bnf -f text -o output.txt

# HTML形式
python3 scripts/validate_bnf.py docs/design/cm_grammar.bnf -f html -o output.html

# DOT形式（Graphviz）
python3 scripts/validate_bnf.py docs/design/cm_grammar.bnf -f dot -o output.dot
```

### 統計情報表示
```bash
python3 scripts/validate_bnf.py docs/design/cm_grammar.bnf --stats
```

## 次のステップ

1. **EBNF記法の改善**: バリデーターをEBNF完全対応に
2. **左再帰の除去**: 文法変換ツールの実装
3. **bnf2cpp実装**: C++コード生成器の開発
4. **実装統合**: 既存パーサーとの段階的統合

## まとめ

BNF定義とパーサー統合の設計が完了し、基本的なツールも実装されました。これにより：

✅ **Cm言語の正式な文法定義が確立**
✅ **文法の検証と出力が可能に**
✅ **段階的な移行パスが明確化**
✅ **フォルダ構成と統合アーキテクチャが設計完了**

今後は実際のパーサージェネレーター実装と既存コードとの統合を進めます。

---

**作成者:** Claude Code
**レビュー**: 実装開始前に要確認
**優先度**: 高（v0.11.0対象）