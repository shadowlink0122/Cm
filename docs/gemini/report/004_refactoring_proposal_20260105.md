# リファクタリング提案 - v0.11.0+

## 概要

v0.10.0完了を機に、コードベースの品質向上と将来の機能追加に備えたリファクタリングを提案します。
特に、型システムの堅牢化（推論アルゴリズムの改善）、既存実装のクリーンアップ、および最適化パスの拡充に焦点を当てます。

---

## 1. 型システムの改善

### 1.1 推論アルゴリズムの刷新 (Unification)

**現状の課題:**
- 現在の型推論は「早い者勝ち (First-come-first-served)」の単純なロジックです。
- `func foo<T>(a: T, b: T)` に対して `foo(1, "string")` を渡すと、第一引数で `T=int` と決定され、整合性チェックが不十分です。
- 戻り値からの推論（双方向推論）が限定的です。

**提案: Hindley-Milnerベースの単一化 (Unification)**
型変数と制約ソルバーを導入し、大域的な整合性を保証します。

```cpp
// 型変数の導入
struct TypeVar {
    int id;
    mutable TypePtr instance; // 代入された型（Unificationで更新）
};

// 単一化ロジック
bool unify(TypePtr t1, TypePtr t2) {
    t1 = prune(t1);
    t2 = prune(t2);
    if (t1 == t2) return true;
    if (is_type_var(t1)) {
        t1->instance = t2; // 型変数を具体的な型に束縛
        return true;
    }
    // ... 構造体や関数の再帰的Unification
}
```

### 1.2 Union型とパターンマッチの完全サポート

**現状:**
- ASTとHIRのメモリレイアウト計算（サイズ・アラインメント）は実装済み。
- しかし、フロントエンドでの型チェック（バリアントの安全性、網羅性チェック）や、MIRでのタグ操作コード生成が不十分な可能性があります。

**提案:**
- `TypeChecker` にUnion型の代入互換性チェックを追加。
- `match` 式での網羅性チェック（Exhaustiveness Check）の実装。

---

## 2. コードベースのクリーンアップ

### 2.1 Monomorphization（単相化）の整理

**現状:**
- `src/mir/lowering/monomorphization_impl.cpp` は実装されていますが、1600行を超える巨大なファイルとなっており、保守性が低いです。
- CMakeの設定に「コンパイルエラー修正待ち」のコメントが残るなど、安定性に不安があります。

**提案:**
- ファイル分割: `monomorphization_func.cpp`（関数特殊化）と `monomorphization_struct.cpp`（構造体特殊化）へ分割。
- スタブの完全削除: `monomorphization_stub.cpp` は削除済みですが、関連する不要コードがないか再点検。

---

## 3. 最適化パスの拡充

### 現状の課題
- 基本的な定数畳み込みやデッドコード削除はありますが、高度な最適化が不足しています。

### 提案: 新規最適化パスの追加

1.  **SROA (Scalar Replacement of Aggregates / 構造体のスカラー置換)**
    - 構造体を個別のフィールド（変数）に分解し、レジスタ割り当てや定数畳み込みの効果を高めます。
    - 優先度: **高**

2.  **共通部分式削除 (CSE - Common Subexpression Elimination)**
    - MIRレベルで、重複する計算（例: 同じフィールドへのアクセスや算術演算）を検出し、一時変数にキャッシュします。

3.  **ループ不変量移動 (LICM - Loop Invariant Code Motion)**
    - ループ内で変化しない計算をループの外（前）に移動し、実行回数を削減します。

4.  **末尾呼び出し最適化 (TCO - Tail Call Optimization)**
    - 再帰呼び出しをジャンプに変換し、スタック消費を抑制します。

5.  **配列境界チェック削除 (BCE - Bounds Check Elimination)**
    - 静的に安全性が証明できる配列アクセスの境界チェックを削除します。

6.  **ループ展開 (Loop Unrolling)**
    - 短いループを展開して分岐オーバーヘッドを削減します。

---

## 4. エラー報告の統一（構造化診断）

### 提案：構造化診断システム

```cpp
struct Diagnostic {
    Severity level;      // Error, Warning, Note, Hint
    Span primary;        // 主要位置
    std::string message;
    std::vector<Span> related;  // 関連位置
    std::optional<Fix> fix;     // 自動修正提案
};
```

---

## 実装優先度

| 項目 | 優先度 | 工数 | 依存 |
|------|--------|------|------|
| Monomorphization整理 & 動作確認 | 最高 | 3日 | なし |
| 型推論 (Unification) | 高 | 2週間 | なし |
| Union型チェック強化 | 中 | 1週間 | 型推論 |
| 最適化 (CSE/LICM) | 低 | 2週間 | なし |

---

## 今後のバージョン計画

### v0.11.0
- **C ABI互換性の修正 (最優先)**:
    - **現象**: 現在、小さな構造体を含むすべての構造体が引数としてポインタ渡しされています。これにより、値渡しを期待する標準的なCライブラリ（FFI）との連携時にクラッシュやデータ破損が発生します。
    - **修正案**: ターゲットプラットフォーム（x64 System V, ARM64等）のABI規約に従い、小さな構造体をレジスタまたはスタックでの値渡しに切り替えます。
    - **アプローチ**: LLVMの `byval` 属性の適切な付与、および `llvm::TargetLowering` 相当の情報を参照した型変換ロジックの導入。
- 型システム警告強化（const推奨、初期化前使用チェック）
- エラー報告統一
- Lint基盤

### v0.12.0
- インラインアセンブリ
- JavaScript改善
- ベアメタル基盤

### v0.13.0
- パッケージマネージャ（`cm pkg`）
- Formatter（`cm fmt`）
- LSP対応

---

*作成: 2026-01-05*
