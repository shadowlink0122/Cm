[English](README.en.html)

# JavaScript Codegen for Cm Language

## 概要

Cm言語からJavaScriptへのコード生成バックエンドの設計と実装ガイド。

## ファイル構成

```
src/codegen/js/
├── codegen.hpp          # メインヘッダー（JSCodeGenクラス）
├── codegen.cpp          # メイン実装（compile, emitFunction等）
├── emit_statements.cpp  # 文と終端命令の出力
├── emit_expressions.cpp # 式と演算子の出力
├── emitter.hpp          # JSEmitterユーティリティクラス
├── runtime.hpp          # ランタイムヘルパー関数（__cm_format等）
├── builtins.hpp         # 組み込み関数マッピング（println等）
└── types.hpp            # 型変換・識別子サニタイズ
```

## 設計方針

### 1. MIRからの直接変換

LLVMバックエンドと同様に、MIR（中間表現）からJavaScriptコードを直接生成します：

```
Cm Source → Lexer → Parser → AST → HIR → MIR → JavaScript
```

### 2. ターゲットの種類

| ターゲット | 説明 | 用途 |
|-----------|------|------|
| `js` | 純粋なJavaScript (Node.js向け) | CLI、サーバーサイド |
| `web` | ブラウザ向けJS + HTML | Webアプリケーション |

### 3. WASMとの関係

- **WASM**: 高パフォーマンスが必要な計算集約型処理向け
- **JS Codegen**: 軽量な処理、DOM操作、Web API統合向け

WASMとJSの連携も可能にする予定（Phase 2）。

## マイルストーン

### Phase 1: 基本機能 (v0.12.0) - 完了

| 機能 | 状態 | 説明 |
|------|------|------|
| 基本型変換 | ✅ | int, double, bool, string → JS |
| 関数生成 | ✅ | 関数定義とエクスポート |
| 制御フロー | ✅ | if, while, for, switch |
| 構造体 | ✅ | JSオブジェクトとして表現（深いコピー対応） |
| 配列 | ✅ | JSの Array として表現（構造体配列含む） |
| スライス | ✅ | 基本スライス操作（find, some, every, reduce等） |
| println → console.log | ✅ | 基本I/O |
| フォーマット文字列 | ✅ | `{}`置換、フォーマット指定子対応 |
| 再帰関数 | ✅ | 動作確認済み |
| ジェネリクス | ✅ | モノモーフィゼーション後 |
| interface/impl | ✅ | 基本動作（with Debug/Display/Clone等） |
| enum | ✅ | 整数として表現 |
| ラムダ式 | ✅ | クロージャキャプチャ完全対応 |
| defer文 | ✅ | 遅延実行 |
| match式 | ✅ | パターンマッチング |
| static変数 | ✅ | グローバルスコープで保持 |

**テスト結果: 198/259 テストがパス (約76%)**

### スキップされるカテゴリ (JSで再現不可能)

| カテゴリ | テスト数 | 理由 |
|---------|----------|------|
| `pointer/` | 15 | ポインタ演算、メモリアドレス操作 |
| `asm/` | 10 | インラインアセンブリ |
| `ffi/` | 4 | ネイティブFFI |
| `allocator/` | 2 | メモリアロケータ |
| `memory/` | 2 | 直接メモリ操作 |
| `dynamic_array/` | 2 | 動的配列（mallocベース） |

### 部分スキップ（技術的制限）

| カテゴリ | スキップ数 | 理由 |
|---------|------------|------|
| `array/` | 6 | ポインタ関連、スライス構文 |
| `array_higher_order/` | 4 | 動的スライス使用 |
| `slice/` | 3 | 動的スライス操作 |
| `casting/` | 3 | ポインタキャスト |
| `iterator/` | 2 | 動的スライス関連 |
| `types/` | 2 | static変数、char型出力 |

### JS用expectファイル

出力フォーマットが異なる場合、`.expect.js` ファイルを使用：
- `types/float_types.expect.js`
- `types/ufloat_udouble.expect.js`

### Phase 2: 高度な機能 (v0.13.0)

| 機能 | 状態 | 説明 |
|------|------|------|
| 関数ポインタ | ⬜ | JS関数参照として表現 |
| ジェネリクス | ⬜ | 型消去アプローチ |
| ラムダ式 | ⬜ | アロー関数 |
| インターフェース | ⬜ | ダックタイピング |
| extern "js" FFI | ⬜ | JS API呼び出し |

### Phase 3: Web統合 (v0.14.0)

| 機能 | 状態 | 説明 |
|------|------|------|
| DOM操作API | ⬜ | document, element操作 |
| イベントリスナー | ⬜ | addEventListener |
| HTMLテンプレート生成 | ⬜ | --target=web |
| Canvas API | ⬜ | グラフィカル描画 |
| WASM連携 | ⬜ | JSからWASM呼び出し |

## 型変換マッピング

| Cm型 | JavaScript型 | 備考 |
|------|-------------|------|
| int, i8-i64 | number | 整数はnumberとして扱う |
| uint, u8-u64 | number | 符号なし整数もnumber |
| float, double | number | 浮動小数点 |
| bool | boolean | |
| char | string | 1文字の文字列 |
| string | string | |
| T[] (固定配列) | Array | |
| T[] (スライス) | Array | |
| T* (ポインタ) | object reference | 参照として扱う |
| struct | object | プレーンオブジェクト |
| enum | number / object | 値または名前付きオブジェクト |

## コード生成例

### 基本的な関数

**Cm:**
```cm
int add(int a, int b) {
    return a + b;
}

int main() {
    int result = add(10, 20);
    println("{result}");
    return 0;
}
```

**生成されるJavaScript:**
```javascript
function add(a, b) {
    return a + b;
}

function main() {
    const result = add(10, 20);
    console.log(result);
    return 0;
}

// エントリーポイント
main();
```

### 構造体

**Cm:**
```cm
struct Point {
    int x;
    int y;
}

void print_point(Point p) {
    println("({p.x}, {p.y})");
}
```

**生成されるJavaScript:**
```javascript
// Point構造体のコンストラクタ
function Point() {
    return { x: 0, y: 0 };
}

function print_point(p) {
    console.log(`(${p.x}, ${p.y})`);
}
```

### 配列とスライス

**Cm:**
```cm
int[5] arr = [1, 2, 3, 4, 5];
int[] slice = arr[1:4];
int sum = 0;
for (int x in slice) {
    sum = sum + x;
}
```

**生成されるJavaScript:**
```javascript
const arr = [1, 2, 3, 4, 5];
const slice = arr.slice(1, 4);
let sum = 0;
for (const x of slice) {
    sum = sum + x;
}
```

## CLI使用方法

```bash
# JavaScript生成 (Node.js向け)
cm compile --target=js app.cm -o app.js

# 生成後に実行
cm compile --target=js app.cm -o app.js --run

# Web向け（HTML付き）
cm compile --target=web app.cm -o dist/
# 生成: dist/app.js, dist/index.html
```

## FFI (外部関数インターフェース)

### extern "js" ブロック

```cm
// JavaScript組み込み関数の宣言
extern "js" {
    void console_log(string message);
    int parseInt(string s);
    double Math_sqrt(double x);
}

int main() {
    console_log("Hello from Cm!");
    double result = Math_sqrt(2.0);
    println("{result}");
    return 0;
}
```

生成されるJS:
```javascript
// 外部関数バインディング
const console_log = (msg) => console.log(msg);
const Math_sqrt = (x) => Math.sqrt(x);

function main() {
    console_log("Hello from Cm!");
    const result = Math_sqrt(2.0);
    console.log(result);
    return 0;
}
```

## テスト

### テストコマンド

```bash
make tj        # JS codegen テスト（順次実行）
make tjp       # JS codegen テスト（並列実行）
```

### スキップ対象

以下の機能はJS codegenでスキップ：

1. **FFI関連** (`tests/test_programs/ffi/`) - WASMベースのFFIは対象外
2. **インラインアセンブリ** (`tests/test_programs/asm/`) - ネイティブ専用
3. **メモリ操作** (`tests/test_programs/memory/`) - 低レベル操作はJS非対応

## 実装ファイル構成

```
src/codegen/
├── common/        # 共通ユーティリティ
├── interpreter/   # インタプリタ
├── llvm/          # LLVM バックエンド
│   ├── core/      # コア機能
│   ├── native/    # ネイティブ生成
│   └── wasm/      # WASM生成
└── js/            # JavaScript バックエンド (新規)
    ├── codegen.cpp
    ├── codegen.hpp
    ├── emitter.cpp    # JS文字列生成
    ├── emitter.hpp
    └── runtime/       # 必要なランタイムヘルパー
        └── cm_runtime.js
```

## 注意事項

### 数値精度

- JavaScriptの数値はすべて64bit浮動小数点
- 大きな整数は精度が失われる可能性がある
- 必要に応じてBigIntを使用（将来対応）

### null/undefined

- Cmのポインタnullは `null` にマップ
- Cmには `undefined` の概念がない

### メモリ管理

- JSはGC管理のため、明示的なメモリ管理は不要
- `delete` 文は空の操作に変換

## 関連ドキュメント

- [ROADMAP.md](../../ROADMAP.html) - 全体のロードマップ
- [LLVM Backend](../llvm/README.html) - LLVM バックエンドの実装