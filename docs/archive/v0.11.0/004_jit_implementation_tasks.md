# JITコンパイラ実装タスク一覧

## 概要
本ドキュメントはJITコンパイラ実装のタスク一覧と進捗管理を行います。

---

## Phase 1: 基礎インフラ（1日目）

### 1.1 ディレクトリ・ファイル作成
- [ ] `src/codegen/jit/` ディレクトリ作成
- [ ] `jit_engine.hpp` 作成
- [ ] `jit_engine.cpp` 作成
- [ ] `jit_runtime.hpp` 作成（オプション）
- [ ] `CMakeLists.txt` 更新（OrcJIT追加）

### 1.2 JITエンジン基本実装
- [ ] `JITEngine`クラス定義
- [ ] LLVM ORC初期化
- [ ] `LLJIT`ビルダー設定
- [ ] Thread-safe context設定
- [ ] 基本エラーハンドリング

### 1.3 CLIオプション追加
- [ ] `--jit`/`-j`フラグ追加
- [ ] `main.cpp`修正
- [ ] JITモード分岐実装

### 1.4 検証
- [ ] `hello.cm` でビルド確認
- [ ] 単純な`println`テスト

---

## Phase 2: MIR変換統合（1日目）

### 2.1 MIRToLLVM再利用
- [ ] `MIRToLLVM`をJITエンジンから呼び出し
- [ ] LLVM Moduleの取得
- [ ] ThreadSafeModule変換
- [ ] JITへのModule追加

### 2.2 ホストシンボル解決
- [ ] `DynamicLibrarySearchGenerator`設定
- [ ] libcシンボル自動解決確認
- [ ] 未解決シンボルエラーハンドリング

### 2.3 検証
- [ ] printfを使うコードテスト
- [ ] 変数宣言・計算テスト

---

## Phase 3: ランタイム関数登録（1-2日目）

### 3.1 Print関数群
- [ ] `cm_print_*` 全18関数登録
- [ ] `cm_println_*` 全18関数登録

### 3.2 Format関数群
- [ ] `cm_format_*` 変換関数登録
- [ ] `cm_format_replace_*` 置換関数登録
- [ ] エスケープ関数登録

### 3.3 String関数群
- [ ] 基本操作関数登録
- [ ] `__builtin_string_*` 登録

### 3.4 Slice関数群
- [ ] ライフサイクル関数登録
- [ ] push/pop関数登録
- [ ] get/set関数登録
- [ ] 変換・操作関数登録

### 3.5 Higher-Order関数群
- [ ] `__builtin_array_map*` 登録
- [ ] `__builtin_array_filter*` 登録

### 3.6 Memory関数群
- [ ] `cm_alloc/dealloc/realloc` 登録
- [ ] `cm_memcpy/memmove/memset` 登録

### 3.7 Panic関数群
- [ ] `cm_panic*` 登録

### 3.8 検証
- [ ] 文字列補間テスト
- [ ] スライス操作テスト

---

## Phase 4: テストインフラ統合（0.5日）

### 4.1 テストランナー修正
- [ ] `unified_test_runner.sh`に`jit`バックエンド追加
- [ ] バックエンド検証条件修正
- [ ] JIT実行ケース追加

### 4.2 Makefileコマンド追加
- [ ] `test-jit` ターゲット
- [ ] `test-jit-parallel` ターゲット
- [ ] `test-jit-o0/o1/o2/o3-parallel` ターゲット
- [ ] 短縮形 `tji`, `tjip`, `tjip0`-`tjip3`

### 4.3 検証
- [ ] `make tjip` 実行
- [ ] パス数確認

---

## Phase 5: 全テスト通過（1-2日）

### 5.1 失敗テスト分析
- [ ] 失敗リスト取得
- [ ] 原因分類
  - [ ] 未登録シンボル
  - [ ] 型ミスマッチ
  - [ ] 最適化問題

### 5.2 個別修正
- [ ] 不足シンボル追加
- [ ] 型変換問題修正
- [ ] その他バグ修正

### 5.3 最終検証
- [ ] `make tjip` = 280 PASS
- [ ] `make tlp`/`make tip`と同等達成

---

## 成功基準

| 項目 | 達成条件 |
|------|---------|
| ビルド | `make build` 成功 |
| 基本テスト | `./cm run --jit hello.cm` 動作 |
| 全テスト | `make tjip` = `make tlp`と同等パス |
| パフォーマンス | インタプリタ比10x以上高速 |

---

## ファイル変更サマリー

| ファイル | 変更種別 |
|----------|---------|
| `src/codegen/jit/jit_engine.hpp` | 新規 |
| `src/codegen/jit/jit_engine.cpp` | 新規 |
| `src/main.cpp` | 修正 |
| `CMakeLists.txt` | 修正 |
| `tests/unified_test_runner.sh` | 修正 |
| `Makefile` | 修正 |

---

## 関連ドキュメント

- [001_jit_compiler_overview.md](./001_jit_compiler_overview.md) - 概要設計
- [002_jit_compiler_detailed_design.md](./002_jit_compiler_detailed_design.md) - 詳細設計
- [003_jit_runtime_functions.md](./003_jit_runtime_functions.md) - ランタイム関数一覧
