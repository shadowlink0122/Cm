[English](technical_challenges.en.html)

# 技術的課題と解決策

## 概要

Cmコンパイラ開発における技術的課題とその解決策をまとめます。

---

## 1. メモリ管理（GCなし）

### 課題
GCなしで安全なメモリ管理を実現する。

### 解決策

| 方式 | 用途 | 解放 |
|------|------|------|
| スタック | ローカル変数 | 自動 |
| `new`/`delete` | ヒープ確保 | 手動 |
| `shared<T>` | 共有所有権 | 参照カウント |
| `&T` | 一時借用 | なし |

```cpp
// スタック（推奨）
Point p = Point(10, 20);

// ヒープ（手動管理）
Point* p = new Point(10, 20);
delete p;

// 共有（自動解放）
shared<Point> p = make_shared<Point>(10, 20);
```

---

## 2. Rustバックエンドへの変換

### 課題
Cmの手動メモリ管理をRustに変換。

### 解決策

| Cm | Rust |
|----|------|
| `new T()` | `Box::new(T())` |
| `delete p` | `drop(p)` |
| `shared<T>` | `Rc<T>` / `Arc<T>` |
| `&T` | `&T` |

---

## 3. async/awaitのクロスプラットフォーム

### 課題
異なるランタイム間での互換性。

### 解決策
**Future<T>型** でラップし、各ランタイムに変換。

| Cm | Rust | TypeScript |
|----|------|------------|
| `Future<T>` | `impl Future<Output=T>` | `Promise<T>` |

詳細: [async.md](async.html)

---

## 4. ネイティブ/Web/Bare-metalの分離

### 課題
互換性のないプラットフォームの分離。

### 解決策
**ファイル拡張子** と **設定ファイル** で明示的に指定。

| 拡張子 | ターゲット |
|--------|-----------|
| `.cm` | 設定依存 |
| `.cm.native` | ネイティブ専用 |
| `.cm.web` | Web専用 |
| `.cm.baremetal` | Bare-metal専用 |

詳細: [platform.md](platform.html)

---

## 5. TSモジュールのimport（Web FFI）

### 課題
ReactなどのTSライブラリをCmから使用。

### 解決策
`extern "ts"` による **Web FFI**。

```cpp
extern "ts" {
    import "react" {
        fn useState<T>(initial: T) -> (T, fn(T));
    }
}
```

---

## 6. no_std相当（Bare-metal）

### 課題
OS/組み込みで標準ライブラリなしで動作。

### 解決策

```toml
[target.baremetal]
std = false
async = false
```

使用可能: `cm-core` のみ（型、基本関数）

---

## まとめ

| 機能 | native | web | baremetal |
|------|--------|-----|-----------|
| std | ✅ | ✅ | ❌ |
| async | ✅ | ✅ | ❌ |
| new/delete | ✅ | (無視) | ✅ |
| shared<T> | ✅ | (無視) | ❌ |
| FFI C/Rust | ✅ | ❌ | ✅ |
| FFI TS | ❌ | ✅ | ❌ |