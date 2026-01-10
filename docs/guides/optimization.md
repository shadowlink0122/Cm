[English](optimization.en.html)

# 言語ガイド - 最適化

Cm言語コンパイラは複数の最適化パスを備えています。

## 実装済みパス

- 定数畳み込み (Constant Folding)
- デッドコード削除 (DCE)
- インライン化 (Inlining)
- ループ不変量移動 (LICM)
- SCCP