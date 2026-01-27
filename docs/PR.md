[English](PR.en.html)

# v0.12.0 Release - Cm言語コンパイラ

## 概要

v0.12.0は**const強化、多次元配列最適化、Lint/Formatter、CI整理**に焦点を当てた大規模アップデートです。配列サイズにconst変数を使用可能にし、多次元配列のフラット化最適化を実装しました。また、CI/CDからWindowsを一時削除し、Linux/macOSに集中しています。

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

### 2. 多次元配列フラット化最適化

多次元配列を1次元配列に自動変換し、キャッシュ効率を大幅に改善しました。

```cm
// コンパイラが自動的に最適化
int arr[100][100];  // → 内部的に int arr[10000] として扱う
```

### 3. Linter実装

コードの静的解析ツールを実装しました。

```bash
./cm lint src/main.cm
```

機能:
- 命名規則チェック（snake_case、PascalCase）
- 未使用変数の検出
- const推奨警告

### 4. Formatter実装

コードフォーマッターを実装しました。

```bash
./cm fmt src/main.cm
```

### 5. 末尾呼び出し最適化（TCE）

LLVMレベルでの末尾呼び出し最適化を実装しました。再帰関数のスタックオーバーフローを防止します。

## 🔧 CI/CD改善

### Windows対応の一時停止
- `test-interpreter.yml`からWindowsのbuild/JITテストを完全削除
- 理由: LLVM公式Windows版にCMakeファイル（LLVMConfig.cmake）が含まれていないため

### Nix flake試行（後にrevert）
- Nix flakeでLinux/macOS CI環境の統一を試みたが、複雑さから従来のHomebrew/aptに戻す

### 不要なテストバイナリの削除
- `test_1d_native`、`test_2d_native`、`test_ptr_native`を削除

## 📁 変更ファイル一覧（主要）

### コンパイラ本体（const強化）
| ファイル | 変更内容 |
|---------|---------|
| `src/frontend/types/scope.hpp` | `Symbol`に`const_int_value`フィールド追加 |
| `src/frontend/types/checking/checker.hpp` | `evaluate_const_expr`, `resolve_array_size`宣言追加 |
| `src/frontend/types/checking/utils.cpp` | コンパイル時評価・配列サイズ解決の実装 |
| `src/frontend/types/checking/stmt.cpp` | `check_let`にconst値評価を追加 |
| `src/frontend/types/checking/decl.cpp` | `GlobalVarDecl`の登録処理追加 |

### 多次元配列最適化
| ファイル | 変更内容 |
|---------|---------|
| `src/mir/optimization/` | 配列フラット化最適化パス追加 |

### Linter/Formatter
| ファイル | 変更内容 |
|---------|---------|
| `src/frontend/types/checking/` | Lint警告機能追加 |
| `src/main.cpp` | `lint`/`fmt`コマンド追加 |

### CI/CD
| ファイル | 変更内容 |
|---------|---------|
| `.github/workflows/test-interpreter.yml` | Windowsパイプラインを削除 |
| `.github/workflows/ci.yml` | 整理・簡素化 |

### ドキュメント
| ファイル | 変更内容 |
|---------|---------|
| `docs/FEATURES.md` | const強化機能を追加 |
| `docs/design/v0.12.0/const_enhancement.md` | 設計ドキュメント（新規） |
| `docs/UNIMPLEMENTED_FEATURES.md` | 未実装機能リスト（新規） |
| `docs/flyer/` | Cm言語のチラシを追加 |

### テスト
| ファイル | 変更内容 |
|---------|---------|
| `tests/test_programs/const/const_array_size.cm` | const配列サイズテスト（新規） |
| `tests/test_tce_fib.cm` | 末尾呼び出し最適化テスト（新規） |
| `tests/bench_marks/cpp/` | C++ベンチマーク追加 |

### ベンチマーク
| ファイル | 変更内容 |
|---------|---------|
| `tests/bench_marks/cpp/05b_matrix_multiply_2d.cpp` | 2D行列乗算 |
| `tests/bench_marks/cpp/06_4d_array.cpp` | 4次元配列 |
| `tests/bench_marks/cpp/07_struct_array.cpp` | 構造体配列 |

## 🧪 テスト状況

| カテゴリ | 通過 | 失敗 | スキップ |
|---------|-----|------|---------|
| LLVM Native (O0) | 281 | 0 | 31 |
| LLVM Native (O3) | 281 | 0 | 31 |
| インタプリタ (O0) | 283 | 0 | 29 |
| インタプリタ (O3) | 283 | 0 | 29 |

## 📊 統計

- **コミット数**: 48
- **変更ファイル数**: 408
- **追加行数**: 19,918
- **削除行数**: 1,608

## ⚠️ 注意事項

### Windows対応について
現在Windows対応は一時停止中です。将来的にvcpkgやconanなどのパッケージマネージャー経由での対応を検討中です。

## 🚀 今後の予定

- const generics（`fn foo<const N: int>()`)の実装
- constexpr関数のサポート
- Windows対応の再検討

---

**リリース日**: 2026年1月28日
**バージョン**: v0.12.0
**コードネーム**: "Const Enhancement"