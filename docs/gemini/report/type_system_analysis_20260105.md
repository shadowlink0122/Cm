# 型システム現状分析レポート (改訂版)

**日付**: 2026年1月5日
**作成者**: Gemini Agent
**対象**: Cm プロジェクト型システム

## 1. 概要

Cmプロジェクトの型システムを調査した結果、当初の想定とは異なり、バックエンドの重要コンポーネントである「単相化（Monomorphization）」は実装され、ビルドプロセスに組み込まれていることが確認されました。ただし、型推論アルゴリズムの単純さや、ロードマップ上の高度な機能の未実装など、いくつかの課題は残されています。

## 2. 調査結果詳細

### 2.1 Monomorphization（単相化）の実装状況
**ステータス**: 実装済み (Active)

初期調査では `src/mir/lowering/monomorphization_stub.cpp` (スタブ) の存在により未実装と判断しましたが、詳細調査の結果、実体は **`src/mir/lowering/monomorphization_impl.cpp`** にあり、正常にビルド対象に含まれていることが判明しました。

- **実装ファイル**: `src/mir/lowering/monomorphization_impl.cpp` (約1600行規模)
- **機能**:
    - ジェネリック関数呼び出しの検出 (`scan_generic_calls`)
    - 型引数の推論 (`infer_type_args`)
    - 特殊化された関数・構造体の生成 (`generate_generic_specializations`, `monomorphize_structs`)
    - 呼び出し箇所の書き換え (`rewrite_generic_calls`)
- **統合**: `src/mir/lowering/lowering.hpp` 内の `perform_monomorphization()` を通じて、MIR Loweringのパス4として組み込まれています。
- **懸念点**: `CMakeLists.txt` に `TODO: Fix compilation errors and re-enable` というコメントが残っており、コードベースが不安定な状態であるか、あるいはコメントが古いまま放置されている可能性があります。

### 2.2 型推論アルゴリズムの課題
**深刻度**: 中 (Medium)

型推論の実装は依然として局所的かつ単純なアプローチを採用しています。

- **ファイル**: `src/frontend/types/checking/generic.cpp`
- **ロジック**:
    - 「早い者勝ち」の推論ロジックであり、最初の引数から推論された型を採用します。
    - 全体的な整合性をチェックする単一化（Unification）アルゴリズム（Hindley-Milnerなど）は実装されていません。
    - 複雑なジェネリクスのネストや、双方向推論（戻り値の型から引数を推論するなど）には対応できない可能性があります。

### 2.3 ロードマップとの乖離
**深刻度**: 低 (Low) - 開発段階として妥当

`docs/design/robust_type_system.md` で定義されているPhase 2以降の機能は未実装です。

- **未実装機能**:
    - Union Types (合併型)
    - Literal Types (リテラル型)
    - Null Safety (Null安全性)
    - 型ガード (Type Guards)
- **現状**: 基本的な構造体、プリミティブ型、およびジェネリクス（単相化含む）が実装されている段階です。

## 3. 結論と推奨事項

1.  **Monomorphizationの動作検証**:
    実装は存在しますが、CMakeのコメントが示唆する「コンパイルエラー」や不安定な挙動がないか、実コードでの動作検証を推奨します。特にジェネリック構造体を含む複雑なケースでのテストが必要です。

2.  **不要ファイルの削除**:
    混乱を避けるため、使用されていない `src/mir/lowering/monomorphization_stub.cpp` は削除するか、明示的にアーカイブすることを推奨します。

3.  **型推論の強化**:
    長期的には、より堅牢な推論を行うために、Unificationベースのアルゴリズムへの移行を検討すべきです。

4.  **ドキュメントの更新**:
    `CMakeLists.txt` のTODOコメントの現状確認と更新を行ってください。

## 4. 訂正

本レポートは、以前作成した「Monomorphization欠如」とする誤ったレポートを訂正するものです。Monomorphizationは実装され、有効化されています。