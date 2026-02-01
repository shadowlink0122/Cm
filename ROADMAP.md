# Cm言語 開発ロードマップ

> **過去のバージョン履歴**: [docs/ROADMAP_HISTORY.md](docs/ROADMAP_HISTORY.md) を参照
> **バージョン計画**: [docs/VERSION_PLAN.md](docs/VERSION_PLAN.md) を参照

## 現在のバージョン: v0.13.0（開発中）

## Version 0.13.0 - イテレータと参照

### 目標
イテレータの完全実装（index/item両方の取得）と参照型の基礎実装。

### イテレータ（優先度：低）
| 機能 | パーサー | 型チェック | HIR/MIR | インタプリタ | LLVM | WASM | テスト |
|------|----------|-----------|---------|-------------|------|------|------|
| for-in index取得 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| for-in (index, item) | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| Iterator インターフェース | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| カスタムイテレータ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |

#### 現在の状態
- ✅ `for (item in arr)` - 要素のみ取得（実装済み）
- ⬜ `for ((index, item) in arr)` - インデックスと要素の両方取得
- ⬜ `for (index in 0..n)` - 範囲イテレータ

#### 構文例（計画）
```cm
int[5] arr = [10, 20, 30, 40, 50];

// 現在: 要素のみ
for (int x in arr) {
    println("{x}");
}

// 計画: インデックスと要素
for ((int i, int x) in arr) {
    println("{i}: {x}");
}

// 計画: 範囲イテレータ
for (int i in 0..5) {
    println("{i}");
}
```

### 参照型
| 機能 | パーサー | 型チェック | HIR/MIR | インタプリタ | LLVM | WASM | テスト |
|------|----------|-----------|---------|-------------|------|------|------|
| &T 参照型 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| move キーワード | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| 借用チェッカー | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| ライフタイム基礎 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| Drop trait | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |

### 設計方針
- **const以外は全て可変**（`mut`キーワードは不要）
- **ポインタ（T*）**: C互換、手動管理、unsafe
- **参照（&T）**: 借用チェック付き、安全、読み取り専用
- **move**: 所有権の明示的移動

### 構文例
```cm
// 参照型（読み取り専用）
void print_point(&Point p) {
    println("({}, {})", p.x, p.y);
}

// ポインタ経由で変更（明示的）
void increment(int* x) {
    *x = *x + 1;
}

// 所有権移動
struct Buffer { int* data; int size; }

void consume(move Buffer buf) {
    // bufの所有権を取得
    // 関数終了時にbufは破棄される
}

int main() {
    Buffer b;
    consume(move b);  // 所有権を移動
    // b はここで使用不可（コンパイルエラー）
    return 0;
}
```

---

## Version 0.14.0 - constexprとマクロシステム

### 目標
コンパイル時計算とマクロによるメタプログラミング

### 実装項目
| 機能 | パーサー | 型チェック | HIR/MIR | インタプリタ | LLVM | WASM | テスト |
|------|----------|-----------|---------|-------------|------|------|------|
| constexpr | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| #macro定義 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| マクロ展開 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| 衛生的マクロ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| genパッケージマネージャ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| 依存関係管理 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |

---

## Version 0.15.0 - インラインアセンブリとベアメタル拡張

### 目標
インラインアセンブリによる低レベルハードウェアアクセスとベアメタルサポートの強化

### 備考
FFIの基本機能はv0.10.0で実装済み。このバージョンではインラインアセンブリとプラットフォーム固有コードに焦点。

### 実装項目
| 機能 | パーサー | 型チェック | HIR/MIR | インタプリタ | LLVM | WASM | テスト |
|------|----------|-----------|---------|-------------|------|------|------|
| asm("code") 基本構文 | ⬜ | ⬜ | ⬜ | - | ⬜ | - | ⬜ |
| 変数補間 {var} | ⬜ | ⬜ | ⬜ | - | ⬜ | - | ⬜ |
| 制約文字列（"=r", "+m"等） | ⬜ | ⬜ | ⬜ | - | ⬜ | - | ⬜ |
| クロバーリスト | ⬜ | ⬜ | ⬜ | - | ⬜ | - | ⬜ |
| Intel/AT&T構文サポート | ⬜ | ⬜ | ⬜ | - | ⬜ | - | ⬜ |
| プラットフォーム固有コード | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |

### 構文例
```cm
// 基本的なインラインアセンブリ
int add_asm(int a, int b) {
    int result;
    asm("mov {a}, %eax\n\t"
        "add {b}, %eax\n\t"
        "mov %eax, {result}"
        : "=m"(result)
        : "m"(a), "m"(b)
        : "eax");
    return result;
}

// CPUID取得
void get_cpuid() {
    int eax, ebx, ecx, edx;
    asm("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0));
}

// システムコール（Linux x86_64）
isize sys_write(int fd, *void buf, usize count) {
    isize ret;
    asm("syscall"
        : "=a"(ret)
        : "a"(1), "D"(fd), "S"(buf), "d"(count)
        : "rcx", "r11", "memory");
    return ret;
```

---

## Version 0.16.0 - WASM Webフロントエンド

### 目標
WASM + JavaScriptローダーによるブラウザ環境でのCmアプリケーション実行基盤

### v0.12.0のJSトランスパイラとの違い
| アプローチ | v0.12.0 JSトランスパイラ | v0.16.0 WASM Webフロントエンド |
|-----------|-------------------------|------------------------------|
| 出力形式 | 純粋なJavaScript | WASM + JSローダー |
| パフォーマンス | JS実行速度 | ネイティブに近い速度 |
| ファイルサイズ | 小さい | やや大きい |
| デバッグ | 容易（ソースマップ） | やや困難 |
| ブラウザ互換性 | 広い | WASM対応ブラウザのみ |
| 用途 | 軽量スクリプト、UI | 計算集約型アプリ |

### 実装項目
| 機能 | パーサー | 型チェック | HIR/MIR | ビルドツール | ランタイム | テスト |
|------|----------|-----------|---------|-------------|-----------|------|
| JavaScript FFI | ✅ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| DOM操作API | ✅ | ⬜ | ⬜ | - | ⬜ | ⬜ |
| WASMローダー生成 | - | - | - | ⬜ | ⬜ | ⬜ |
| HTMLテンプレート生成 | - | - | - | ⬜ | - | ⬜ |
| Web専用ランタイム | - | - | - | - | ⬜ | ⬜ |
| イベントリスナー | ✅ | ⬜ | ⬜ | - | ⬜ | ⬜ |
| 文字列変換（JS↔WASM） | - | - | - | - | ⬜ | ⬜ |

### 構文例
```cm
// JavaScript FFI
extern "js" {
    void console_log(string message);
    void alert(string message);
}

// DOM操作
interface Document {
    Element getElementById(string id);
}

interface Element {
    void setText(string text);
    void addEventListener(string event, void*(void*) callback);
}

int main() {
    console_log("Hello from Cm!");
    
    Document doc = get_document();
    Element btn = doc.getElementById("myButton");
    btn.addEventListener("click", |event| {
        alert("Button clicked!");
    });
    
    return 0;
}
```

### ビルドコマンド
```bash
# WASM + JavaScript + HTML を生成
cm build --target=wasm-web app.cm -o dist/

# 生成されるファイル
# - dist/app.wasm
# - dist/app.js (WASMローダー)
# - dist/index.html
```

### 開発サーバー
```bash
cm serve --target=wasm-web
# → http://localhost:3000 でアプリケーションを起動
```

---

## Version 0.17.0 - 非同期処理

### 目標
async/awaitとFuture型の実装

### 実装項目
| 機能 | パーサー | 型チェック | HIR/MIR | インタプリタ | LLVM | WASM | テスト |
|------|----------|-----------|---------|-------------|------|------|------|
| async関数 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| await式 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| Future型 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| ランタイム統合 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| 並行処理 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ |
| Promise統合（WASM） | ⬜ | ⬜ | ⬜ | - | - | ⬜ | ⬜ |

---

## Version 0.18.0 - ベアメタル・OSサポート

### 目標
UEFI/ベアメタル環境でのOS開発サポート

### 前提条件（v0.10.0で実装済み）

ランタイムライブラリのno_std対応が完了しています：

| 機能 | 状態 | 備考 |
|------|------|------|
| カスタムアロケータ | ✅ | `__cm_heap_alloc`等をユーザーが提供可能 |
| 自前文字列関数 | ✅ | libc非依存の`cm_strcmp`等 |
| 自前メモリ操作 | ✅ | libc非依存の`cm_memcpy`等 |
| 自前数値→文字列 | ✅ | snprintf不使用 |
| CM_NO_STD条件分岐 | ✅ | 出力関数の無効化 |

### 実装項目
| 機能 | パーサー | 型チェック | HIR/MIR | インタプリタ | LLVM | WASM | テスト |
|------|----------|-----------|---------|-------------|------|------|------|
| #[no_std] 属性 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | - | ⬜ |
| #[no_main] 属性 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | - | ⬜ |
| volatile read/write | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | - | ⬜ |
| packed構造体 | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | - | ⬜ |
| カスタムリンカスクリプト | - | - | - | - | ⬜ | - | ⬜ |
| UEFIターゲット | - | - | - | - | ⬜ | - | ⬜ |
| freestanding環境 | - | - | - | - | ⬜ | - | ⬜ |

### UEFI Hello World に必要な機能

#### 1. 言語機能
- ✅ ポインタ型（`void*`, `T*`）
- ✅ 構造体（アライメント制御）
- ⬜ packed構造体（`#[repr(packed)]`）
- ⬜ volatile操作
- ⬜ カスタムエントリポイント

#### 2. コンパイル機能
- ⬜ `#[no_std]` - 標準ライブラリなしコンパイル
- ⬜ `#[no_main]` - mainなしコンパイル
- ⬜ カスタムターゲット（PE32+/COFF for UEFI）
- ⬜ リンカスクリプト指定

#### 3. FFI機能
- ⬜ `extern "efiapi"` 呼び出し規約（UEFI ABI）
- ⬜ UEFI型定義（EFI_STATUS, EFI_HANDLE等）

### 構文例（UEFI Hello World）
```cm
#[no_std]
#[no_main]

// UEFI型定義
typedef EFI_STATUS = uint64;
typedef EFI_HANDLE = void*;
typedef CHAR16 = uint16;

// UEFI構造体
#[repr(C)]
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    void* Reset;
    void* OutputString;
    // ...
}

#[repr(C)]
struct EFI_SYSTEM_TABLE {
    // ...
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut;
    // ...
}

// UEFIエントリポイント
extern "efiapi"
EFI_STATUS efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* system_table) {
    // "Hello, World!\r\n" in UTF-16
    CHAR16[16] message = [0x0048, 0x0065, 0x006C, 0x006C, 0x006F, 0x002C, 
                          0x0020, 0x0057, 0x006F, 0x0072, 0x006C, 0x0064,
                          0x0021, 0x000D, 0x000A, 0x0000];
    
    // 関数ポインタを通じてOutputStringを呼び出し
    system_table->ConOut->OutputString(system_table->ConOut, &message[0]);
    
    return 0;  // EFI_SUCCESS
}
```

### ビルドフロー
```bash
# 1. Cmソースをコンパイル（UEFIターゲット）
cm compile --target=x86_64-unknown-uefi hello.cm -o hello.o

# 2. PE32+実行ファイルにリンク
lld-link /subsystem:efi_application /entry:efi_main /out:hello.efi hello.o

# 3. UEFIシェルまたはQEMUで実行
qemu-system-x86_64 -bios OVMF.fd -drive file=fat:rw:esp/,format=raw
```

### OS非依存の原則
- 標準ライブラリを**OS依存部分**と**OS非依存部分**に分離
- `#[no_std]`環境では`println`等のOS依存機能は使用不可
- 代わりに`core`ライブラリ（OS非依存）の機能のみ使用可能

---

## Version 1.0.0 - 安定版リリース

### 達成条件
- ✅ すべてのコア機能の実装完了
- ✅ インタプリタ・LLVM・WASMバックエンドの動作一致
- ✅ 包括的なテストスイート（>90%カバレッジ）
- ✅ ドキュメント完備
- ✅ genパッケージマネージャ完成
- ✅ エディタサポート（VSCode, Vim）
- ✅ 標準ライブラリ安定版
- ✅ 参照型と所有権システム
- ✅ FFIとインラインアセンブリ
- ✅ ベアメタル・OSサポート

### 最終チェックリスト
- [ ] 言語仕様書の完成
- [ ] APIドキュメント
- [ ] チュートリアル
- [ ] サンプルプロジェクト
- [ ] パフォーマンスベンチマーク
- [ ] セキュリティ監査

## 並行開発の原則

### 1. 機能実装フロー
```
1. 言語機能の仕様策定
2. テストケース作成（期待される動作の定義）
3. インタプリタ実装
4. LLVMバックエンド実装
5. 両実装の出力比較テスト
6. 不一致があれば修正
7. ドキュメント更新
```

### 2. テスト戦略
- **ゴールデンテスト**: 同一入力に対する出力の完全一致確認
- **プロパティベーステスト**: ランダム入力での動作一致
- **境界値テスト**: エッジケースでの動作確認
- **パフォーマンステスト**: 実行速度の比較

### 3. CI/CDパイプライン
```yaml
test:
  - stage: unit_test
    - interpreter_tests
    - llvm_tests
  - stage: integration_test
    - compare_outputs
    - golden_tests
  - stage: benchmark
    - performance_comparison
```

## リスクと緩和策

| リスク | 影響度 | 緩和策 |
|--------|-------|--------|
| インタプリタ/LLVMの動作不一致 | 高 | 各機能実装時に徹底的な比較テスト |
| パフォーマンス差が大きい | 中 | 最適化は別フェーズで実施 |
| 仕様変更による手戻り | 中 | 早期のプロトタイプと検証 |
| 依存関係の複雑化 | 低 | モジュール間の疎結合設計 |

## 次のアクション（v0.7.0エラーハンドリングに向けて）

1. **Result<T, E>型の実装** - 1週間
   - ジェネリック構造体Resultの標準ライブラリ定義
   - Ok/Errコンストラクタの実装
   - パターンマッチングとの連携準備

2. **Option<T>型の実装** - 1週間
   - Some/Noneコンストラクタの実装
   - null安全性の保証

3. **?演算子（エラー伝播）** - 2週間
   - パーサー拡張
   - HIR/MIRでの伝播ロジック
   - 各バックエンドでの実装

4. **パニックハンドリング** - 1週間
   - panic!マクロ
   - スタックトレース

**目標リリース日**: 2025年2月中旬

---

## 廃止された機能

以下の機能は2025年12月に廃止されました：

- **Rustトランスパイラ** (`--emit-rust`): LLVMバックエンドに置き換え
- **TypeScriptトランスパイラ** (`--emit-ts`): LLVMバックエンドに置き換え
- **C++トランスパイラ** (`--emit-cpp`): LLVMバックエンドに置き換え

これらの機能のソースコードは削除されました。

---

## 設計原則（重要）

### メソッドは必ずinterfaceを通じて定義
```cm
// ❌ 間違い：構造体に直接メソッドを定義
struct Point {
    double x;
    double y;

    void print() { }  // これは許可されない
}

// ✅ 正しい：interfaceを通じて定義
interface Printable {
    void print();
}

struct Point {
    double x;
    double y;
}

impl Point for Printable {
    void print() {
        println("({}, {})", self.x, self.y);
    }
}
```

### self と this の使い分け
- `self`: implブロック内でのインスタンス参照
- `this`: コンストラクタ/デストラクタ内でのインスタンス参照

```cm
impl<T> Vec<T> {
    self() {
        this.data = null;  // コンストラクタ内では this
    }
}

impl<T> Vec<T> for Container<T> {
    void push(T item) {
        self.data[self.size++] = item;  // メソッド内では self
    }
}
```
---

## Version 1.1.0 - パフォーマンス最適化

### 目標
コンパイル時最適化とランタイム性能の向上

### MIR最適化パス実装状況（全プラットフォーム共通）
| 最適化パス | 実装状態 | 設計文書 | 問題点 | 備考 |
|-----------|---------|----------|--------|------|
| 定数畳み込み (Constant Folding) | ✅ 実装済み | [設計](docs/optimization/mir_optimization_roadmap.md) | なし | 2026-01-04修正完了 |
| デッドコード除去 (DCE) | ✅ 実装済み | [設計](docs/optimization/mir_optimization_roadmap.md) | なし | クロージャ対応済み |
| 定数伝播 (SCCP) | ✅ 実装済み | - | なし | 条件分岐対応 |
| コピー伝播 | ✅ 実装済み | - | なし | |
| 共通部分式除去 (CSE/GVN) | ✅ 実装済み | - | なし | Local GVN実装 |
| デッドストア除去 (DSE) | ✅ 実装済み | - | なし | |
| 制御フロー簡約化 | ✅ 実装済み | - | なし | |
| インライン化 | ✅ 実装済み | [設計](docs/optimization/infinite_loop_detection.md) | なし | 回数制限実装済み |
| ループ不変式移動 (LICM) | ✅ 実装済み | - | なし | |
| プログラムレベルDCE | ✅ 実装済み | - | なし | 未使用関数削除 |

### プラットフォーム別最適化実装状況
| プラットフォーム | 最適化層 | 実装状態 | 最適化レベル | 備考 |
|----------------|---------|---------|------------|------|
| **インタプリタ** | MIR直接実行 | ✅ 完了 | O0-O3 (MIR最適化のみ) | デバッグ用途 |
| **LLVM Native** | MIR + LLVM PassBuilder | ✅ 完了 | O0-O3 (フル最適化) | x86_64, ARM64対応 |
| **WASM** | MIR + LLVM (Oz) | ✅ 完了 | Oz (サイズ優先) | wasm32-unknown-wasi |
| **JavaScript** | MIR + JS固有最適化 | ⚠️ 部分実装 | 基本最適化のみ | インライン化、変数追跡 |
| **Baremetal** | MIR + LLVM (Os) | ✅ 完了 | Os (サイズ優先) | ARM Cortex-M |

**詳細**: [プラットフォーム別最適化分析](docs/optimization/platform_specific_optimization_analysis.md)

### 最適化レベル設定
| レベル | オプション | 反復回数 | 有効な最適化 |
|-------|------------|----------|-------------|
| O0 | `--no-opt` | 0 | なし（デバッグビルド） |
| O1 | `-O1` | 最大3回 | 定数畳み込み、DCE、基本インライン化 |
| O2 | `-O2` | 最大10回 | O1 + CSE、LICM、積極的インライン化 |
| O3 | `-O3` | 最大10回 | O2 + ループアンローリング、ベクトル化 |

### 無限ループ防止機構
| 機構 | 実装状態 | 説明 |
|------|----------|------|
| 反復回数制限 | ✅ 実装済み | 最大10回の反復で打ち切り |
| 個別パス実行回数制限 | ✅ 実装済み | 各パス最大30回まで |
| 循環検出 | ✅ 実装済み | ハッシュベース検出（履歴8個） |
| インライン化制限 | ✅ 実装済み | 関数ごと2回、全体20回まで |
| タイムアウト | ⬜ 未実装 | コンパイル時間制限（低優先度） |

### JavaScript/WASM追加最適化（今後の計画）
| 機能 | 優先度 | 複雑度 | 状態 |
|------|--------|--------|------|
| 文字列リテラルのlen()定数化 | 高 | 低 | ⬜ |
| 文字列メソッドのインライン展開 | 中 | 中 | ⬜ |
| 文字列プーリング | 中 | 中 | ⬜ |
| 配列境界チェック最適化 | 中 | 中 | ⬜ |
| ループアンローリング | 低 | 高 | ⬜ |
| SIMD文字列操作 | 低 | 高 | ⬜ |

### 最適化ターゲット

#### 1. コンパイル時定数化
```cm
// 現在: ランタイム関数呼び出し
string s = "Hello";
uint len = s.len();  // __builtin_string_len("Hello") を呼び出し

// 最適化後: コンパイル時に解決
string s = "Hello";
uint len = 5;  // 定数に置き換え
```

#### 2. 文字列インターニング
同一文字列リテラルを共有してメモリ使用量を削減：
```cm
string a = "Hello";
string b = "Hello";
// 最適化後: a と b は同じメモリ位置を指す
```

#### 3. 境界チェック最適化
ループ内の配列アクセスでの冗長な境界チェックを除去：
```cm
int[100] arr;
for (int i = 0; i < 100; i++) {
    arr[i] = i;  // 境界チェックは1回だけ
}
```

### パフォーマンス目標
| 操作 | 現在 | 目標 |
|------|------|------|
| len() (リテラル) | ~50ns | 0ns (定数) |
| substring() | ~1μs | ~500ns |
| indexOf() | ~2μs | ~1μs |

### 備考
- 現在の実装は機能的には十分で、一般的なアプリケーションでは問題なく使用可能
- 最適化は後方互換性を保ちながら段階的に実装
- プロファイリングに基づいて優先順位を調整
