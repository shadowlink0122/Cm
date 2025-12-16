# Cm言語機能リファレンス

このディレクトリには、Cm言語の各機能の詳細なドキュメントが含まれます。

## 📚 機能一覧

### v0.9.0 実装済み

| 機能 | チュートリアル | 状態 |
|------|------------|------|
| **配列** | [../tutorials/arrays.md](../tutorials/arrays.md) | ✅ 完全実装 |
| **ポインタ** | [../tutorials/pointers.md](../tutorials/pointers.md) | ✅ 完全実装 |
| **with自動実装** | [../tutorials/with-keyword.md](../tutorials/with-keyword.md) | ✅ LLVM完了 |
| **match式** | match式（作成予定） | ✅ 完全実装 |
| **Enum型** | Enum型（作成予定） | ✅ 完全実装 |
| **typedef** | typedef（作成予定） | ✅ 完全実装 |
| **文字列操作** | 文字列操作（作成予定） | ✅ 完全実装 |
| **関数ポインタ** | 関数ポインタ（作成予定） | ✅ 完全実装 |
| **デッドコード削除** | - | ✅ 完全実装 |
| **型制約** | 型制約（作成予定） | ✅ AND/OR境界 |

### v0.10.0 予定

| 機能 | 優先度 | 説明 |
|------|--------|------|
| typedef型ポインタ（LLVM/WASM） | 🔴 高 | LLVMバックエンド対応 |
| with自動実装（WASM） | 🔴 高 | WASMバックエンド対応 |
| ラムダ式 | 🟡 中 | `\|args\| expr` 構文 |
| 動的メモリ確保 | 🟡 中 | `new`/`delete` |
| 配列スライス | 🟡 中 | `arr[a:b]` |
| Debug/Display | 🟢 低 | 自動文字列変換 |

## 🎯 機能別インデックス

### 基本機能

- **[変数と型](../guides/type-system.md)** - プリミティブ型、構造体
- **[制御構文](../guides/control-flow.md)** - if/while/for/switch
- **[関数](../guides/functions.md)** - 関数定義、オーバーロード

### 型システム

- **[ジェネリクス](../guides/generics.md)** - 型パラメータ、制約
- **[インターフェース](../guides/interfaces.md)** - interface/impl
- **[Enum型](../tutorials/enums.md)** - 列挙型（作成予定）
- **[typedef](../tutorials/typedef.md)** - 型エイリアス（作成予定）
- **[型制約](../tutorials/type-constraints.md)** - AND/OR境界（作成予定）

### メモリ管理

- **[配列](../tutorials/arrays.md)** - 固定長配列
- **[ポインタ](../tutorials/pointers.md)** - メモリアドレス操作
- **[所有権](../guides/ownership.md)** - 所有権システム（v0.11.0予定）

### 高度な機能

- **[match式](../tutorials/match-expression.md)** - パターンマッチング（作成予定）
- **[with自動実装](../tutorials/with-keyword.md)** - トレイト自動導出
- **[演算子オーバーロード](../guides/operators.md)** - カスタム演算子

### バックエンド

- **[LLVMバックエンド](../guides/llvm-backend.md)** - ネイティブコンパイル
- **[WASMバックエンド](../guides/wasm-backend.md)** - WebAssembly出力
- **[最適化](../guides/optimization.md)** - デッドコード削除

## 📖 クイックリファレンス

### 配列

```cm
int[5] arr = {1, 2, 3, 4, 5};
int size = arr.len();
int first = arr[0];
```

### ポインタ

```cm
int x = 42;
int* p = &x;
*p = 100;
```

### with自動実装

```cm
struct Point with Eq + Clone {
    int x;
    int y;
}
```

### match式

```cm
match (value) {
    0 => println("zero"),
    1 => println("one"),
    _ => println("other"),
}
```

### Enum

```cm
enum Status {
    Ok = 0,
    Error = -1
}
```

## 🔍 機能の検索

### データ型

- プリミティブ型 → [type-system.md](../guides/type-system.md)
- 配列 → [arrays.md](../tutorials/arrays.md)
- ポインタ → [pointers.md](../tutorials/pointers.md)
- 構造体 → [type-system.md](../guides/type-system.md#構造体)
- Enum → [enums.md](../tutorials/enums.md)（作成予定）
- ジェネリクス → [generics.md](../guides/generics.md)

### 制御構文

- if/else → [control-flow.md](../guides/control-flow.md)
- while/for → [control-flow.md](../guides/control-flow.md)
- match → [match-expression.md](../tutorials/match-expression.md)（作成予定）
- switch → [control-flow.md](../guides/control-flow.md#switch文)

### 関数

- 関数定義 → [functions.md](../guides/functions.md)
- オーバーロード → [functions.md](../guides/functions.md#オーバーロード)
- 関数ポインタ → [function-pointers.md](../tutorials/function-pointers.md)（作成予定）
- ジェネリック関数 → [generics.md](../guides/generics.md)

### メソッド

- インターフェース → [interfaces.md](../guides/interfaces.md)
- impl構文 → [interfaces.md](../guides/interfaces.md#impl構文)
- operator → [operators.md](../guides/operators.md)
- with自動実装 → [with-keyword.md](../tutorials/with-keyword.md)

## 📝 各機能の実装状況

凡例: ✅ 完全実装 | 🟡 部分実装 | ⚠️ 次バージョン | ⬜ 未実装

| 機能 | インタプリタ | LLVM | WASM |
|------|------------|------|------|
| 配列 | ✅ | ✅ | ✅ |
| ポインタ | ✅ | ✅ | ✅ |
| with Eq/Ord | ✅ | ✅ | ⚠️ |
| match式 | ✅ | ✅ | ✅ |
| Enum | ✅ | ✅ | ✅ |
| typedef | ✅ | ✅ | ✅ |
| 文字列メソッド | ✅ | ✅ | ✅ |
| 関数ポインタ | ✅ | ✅ | ✅ |
| for-in | ✅ | ✅ | ✅ |
| デッドコード削除 | - | ✅ | ✅ |

## 🔗 関連リンク

- [言語ガイド](../guides/) - 包括的なガイド
- [チュートリアル](../tutorials/) - ステップバイステップ
- [正式言語仕様](../design/CANONICAL_SPEC.md) - 完全な仕様
- [サンプルコード](../../examples/) - 実用例

---

**更新日:** 2024-12-16
