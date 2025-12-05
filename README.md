# Cm (シーマイナー) プログラミング言語

**ステータス**: 🚧 設計・開発中

## 概要

Cm（シーマイナー）は、[Cb言語](https://github.com/shadowlink0122/Cb)の設計レベルからのリニューアルプロジェクトです。

Cb言語では、HIR（High-level Intermediate Representation）などの中間表現が考慮されていない設計だったため、本プロジェクトでは現代的なコンパイラアーキテクチャを採用し、より拡張性・保守性の高い言語処理系を目指します。

## 設計目標

### Cbからの改善点

1. **HIR (High-level Intermediate Representation) の導入**
   - ソースコードとバックエンドの間に位置する中間表現
   - 型情報を保持した高レベルな最適化が可能
   - より良いエラーメッセージの生成

2. **段階的なコンパイルパイプライン**
   ```
   Source Code → Lexer → Parser → AST → HIR → Codegen/Interpreter
   ```
   ※ 将来的にはMIR（Mid-level IR）の追加も検討

3. **モジュール化されたアーキテクチャ**
   - 各フェーズが明確に分離された設計
   - テスト容易性の向上
   - 将来的なバックエンド追加の容易さ

### 継承する機能（Cb言語より）

- 静的型付け
- ジェネリクス
- async/await
- Interface/Implシステム
- パターンマッチング
- 文字列補間
- モジュールシステム

## プロジェクト構造（予定）

```
Cm/
├── src/
│   ├── frontend/           # フロントエンド
│   │   ├── lexer/         # 字句解析
│   │   ├── parser/        # 構文解析
│   │   └── ast/           # 抽象構文木
│   ├── middle/            # 中間層
│   │   ├── hir/           # High-level IR
│   │   ├── type_check/    # 型検査
│   │   └── lowering/      # HIR → 下位表現への変換
│   ├── backend/           # バックエンド
│   │   └── interpreter/   # インタープリター（初期実装）
│   └── common/            # 共通ユーティリティ
├── docs/                  # ドキュメント
│   ├── design/           # 設計ドキュメント
│   └── spec/             # 言語仕様
├── tests/                 # テスト
└── examples/              # サンプルコード
```

## ビルド方法

（開発中）

## 関連プロジェクト

- [Cb言語](https://github.com/shadowlink0122/Cb) - 本プロジェクトの前身

## ライセンス

検討中