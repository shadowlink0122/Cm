# Cm言語 開発ロードマップ

> **過去のバージョン履歴**: [docs/archive/legacy/root_docs/ROADMAP_HISTORY.md](docs/archive/legacy/root_docs/ROADMAP_HISTORY.md)

---

## 現在: v0.13.0（開発中）

### 目標
**LLVM分割コンパイルとネイティブI/Oの実現**

- OS/ベアメタル〜Webアプリを単一言語で実現する基盤強化
- インタプリタ依存からLLVMネイティブへの完全移行

---

## v0.13.0 優先項目

### Phase 1: LLVM分割コンパイル（最優先）

複数ファイルからなるプロジェクトのLLVMネイティブビルド

| 機能 | 状態 | 説明 |
|------|------|------|
| オブジェクトファイル出力 | ⬜ | `-c`オプションで`.o`生成 |
| シンボル解決 | ⬜ | 複数`.o`のリンク |
| ヘッダー/モジュール認識 | ⬜ | `import`文での依存解決 |
| インクリメンタルビルド | ⬜ | 変更ファイルのみ再コンパイル |

**目標**: `cm build`で複数ファイルプロジェクトをネイティブビルド

### Phase 2: LLVM I/O（高優先度）

ネイティブ環境での標準入出力

| 機能 | 状態 | 説明 |
|------|------|------|
| stdin読み取り | ⬜ | `input()`関数 |
| ファイル読み取り | ⬜ | `std::fs::read()` |
| ファイル書き込み | ⬜ | `std::fs::write()` |
| バイナリI/O | ⬜ | `read_bytes()`, `write_bytes()` |

**目標**: インタプリタと同等のI/O機能をLLVMで実現

### Phase 3: Tagged Union改善

match式でのデータ抽出

| 機能 | 状態 | 説明 |
|------|------|------|
| enum with data | ✅ | `enum Option { Some(T), None }` |
| match destructure | ✅ | `Some(x) => ...` |
| 複合型抽出 | ⬜ | 複数フィールド抽出 |

### Phase 4: マクロシステム

コンパイル時コード生成

| 機能 | 状態 | 説明 |
|------|------|------|
| 定数マクロ | ✅ | `macro int VERSION = 13;` |
| 関数マクロ | ⬜ | `macro foo(x) { ... }` |
| 条件マクロ | ⬜ | `#if`, `#else` |

---

## 将来バージョン概要

| バージョン | テーマ | 主要機能 |
|------------|--------|----------|
| v0.14.0 | インラインアセンブリ | `asm!`拡張、LLVM組み込み関数 |
| v0.15.0 | ベアメタル | `#[no_std]`, UEFI対応 |
| v0.16.0 | WASM Web | DOM操作、JSブリッジ強化 |
| v0.17.0 | 非同期処理 | async/await, Future型 |
| v1.0.0 | 安定版 | 言語仕様確定、ドキュメント完備 |

---

## 設計原則

### 構文スタイル: C++風
```cm
// ✓ 正しい
int add(int a, int b) { return a + b; }

// ✗ Rust風禁止
fn add(a: int, b: int) -> int { }
```

### メソッドはinterfaceを通じて定義
```cm
interface Printable {
    void print();
}

impl Point for Printable {
    void print() { println("{self.x}, {self.y}"); }
}
```

---

## 最適化パス（実装済み）

| パス | 状態 |
|------|------|
| 定数畳み込み | ✅ |
| デッドコード除去 | ✅ |
| 定数伝播 (SCCP) | ✅ |
| コピー伝播 | ✅ |
| 共通部分式除去 | ✅ |
| インライン化 | ✅ |
| ループ不変式移動 | ✅ |

---

## 廃止機能

- Rust/TypeScript/C++トランスパイラ（2025年12月廃止）
- LLVMバックエンドに統一
