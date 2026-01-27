[English](PR.en.html)

# v0.12.0 Release - Cm言語コンパイラ

## 概要

v0.12.0は**const強化とCI整理**に焦点を当てたアップデートです。配列サイズにconst変数を使用可能にし、コンパイル時定数評価を実装しました。また、CI/CDパイプラインを整理し、Windows対応を一時停止しました。

## 🎯 主要な新機能

### 1. const強化（配列サイズにconst変数を使用可能）

コンパイル時定数評価を実装し、配列サイズの宣言にconst変数や定数式を使用できるようになりました。

```cm
const int SIZE = 10;
const int DOUBLE_SIZE = SIZE * 2;  // 定数式も評価可能

int main() {
    int[SIZE] arr1;           // int[10]
    int[DOUBLE_SIZE] arr2;    // int[20]
    
    println(arr1.size());     // 10
    println(arr2.size());     // 20
    
    return 0;
}
```

#### 実装詳細
- `Symbol`構造体に`const_int_value`フィールドを追加
- `evaluate_const_expr`関数でコンパイル時定数評価
  - 整数リテラル、bool値
  - 算術演算（+, -, *, /, %）
  - ビット演算（&, |, ^, <<, >>）
  - 比較演算、論理演算
  - 三項演算子
- `resolve_array_size`関数で配列サイズを解決
- グローバルconst変数の型チェッカー登録を追加

### 2. CI/CDパイプライン整理

#### Windowsパイプラインの削除
- `test-interpreter.yml`からWindowsのbuild/JITテストを完全削除
- 理由: LLVM公式Windows版にCMakeファイル（LLVMConfig.cmake）が含まれていないため
- Linux/macOSのみでテストを実行

#### 変更されたワークフロー
- `test-interpreter.yml`: Windowsパイプラインを削除（build、JIT O3テスト）

## 🔧 改善・修正

### 型チェッカーの改善
- `GlobalVarDecl`（グローバル変数/定数宣言）の型チェック処理を追加
- const変数の値をスコープに保存し、後続の型チェックで参照可能に

### ドキュメント更新
- `docs/FEATURES.md`: const強化機能を追加
- `docs/design/v0.12.0/const_enhancement.md`: 設計ドキュメント

## 📁 変更ファイル一覧

### コンパイラ本体
| ファイル | 変更内容 |
|---------|---------|
| `src/frontend/types/scope.hpp` | `Symbol`に`const_int_value`フィールド追加 |
| `src/frontend/types/checking/checker.hpp` | `evaluate_const_expr`, `resolve_array_size`宣言追加 |
| `src/frontend/types/checking/utils.cpp` | コンパイル時評価・配列サイズ解決の実装 |
| `src/frontend/types/checking/stmt.cpp` | `check_let`にconst値評価を追加 |
| `src/frontend/types/checking/decl.cpp` | `GlobalVarDecl`の登録処理追加 |

### CI/CD
| ファイル | 変更内容 |
|---------|---------|
| `.github/workflows/test-interpreter.yml` | Windowsビルド・JITテスト削除 |

### ドキュメント
| ファイル | 変更内容 |
|---------|---------|
| `docs/FEATURES.md` | const強化機能を追加 |
| `docs/design/v0.12.0/const_enhancement.md` | 設計ドキュメント（新規） |

### テスト
| ファイル | 変更内容 |
|---------|---------|
| `tests/test_programs/const/const_array_size.cm` | テストケース（新規） |

## 🧪 テスト状況

| カテゴリ | 通過 | 失敗 | スキップ |
|---------|-----|------|---------|
| LLVM Native (O0) | 281 | 0 | 31 |
| LLVM Native (O3) | 281 | 0 | 31 |
| インタプリタ (O0) | 283 | 0 | 29 |
| インタプリタ (O3) | 283 | 0 | 29 |

## ⚠️ 注意事項

### Windows対応について
現在Windows対応は一時停止中です。LLVM公式Windows版に開発用ファイル（CMake設定）が含まれていないため、ソースからLLVMをビルドする必要があります。将来的にvcpkgやconanなどのパッケージマネージャー経由での対応を検討中です。

## 🚀 今後の予定

- const generics（`fn foo<const N: int>()`)の実装
- constexpr関数のサポート
- Windows対応の再検討

---

**リリース日**: 2026年1月28日
**バージョン**: v0.12.0
**コードネーム**: "Const Enhancement"