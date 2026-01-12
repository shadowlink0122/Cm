# Cm言語 Pin/マクロ設計改訂サマリー

作成日: 2026-01-11
ステータス: 設計改訂完了

## 主要な改訂内容

### 1. Unpinトレイトの完全廃止 ✅

**改訂前（Rust風）:**
- Unpinトレイトで移動可能性を表現
- !Unpinで移動不可を表現
- PhantomPinnedマーカー型が必要
- 理解が困難で直感的でない

**改訂後（Cm独自）:**
```cm
// シンプル：デフォルトで移動可能、Pinで明示的に固定
int value = 42;
Pin<int> pinned(value);  // これだけで固定

// Unpinトレイトは存在しない - 不要！
```

### 2. Cm構文への完全準拠 ✅

**改訂前（Rust風）:**
```rust
fn add(a: i32, b: i32) -> i32 { a + b }
let pinned: Pin<&mut T> = pin!(value);
macro_rules! vec { ... }
```

**改訂後（Cm/C++風）:**
```cm
int add(int a, int b) { return a + b; }
Pin<T> pinned(value);  // C++風の変数宣言
macro vec { ... }      // macro_rules!を廃止
```

### 3. 直感的なPin API ✅

**シンプルな使用法:**
```cm
// 1. 値を固定
SelfReferential obj;
Pin<SelfReferential> pinned(obj);

// 2. アクセス（C++風）
pinned->method();        // -> 演算子
(*pinned).field = 42;   // * 演算子

// 3. 移動は自動的に禁止
// SelfReferential moved = obj;  // コンパイルエラー
```

### 4. マクロ構文の簡略化 ✅

**改訂前（Rust風）:**
```rust
macro_rules! vec {
    () => { Vec::new() };
    ($($x:expr),*) => {{
        let mut v = Vec::new();
        $(v.push($x);)*
        v
    }};
}
```

**改訂後（Cm風）:**
```cm
macro vec {
    () => { Vector<int>() };
    ($first:expr, $rest:expr...) => {{
        Vector<typeof($first)> v;
        v.push($first);
        $(v.push($rest);)...
        v
    }};
}
```

## 設計改善のポイント

### 1. 複雑性の削減

| 要素 | Rust | Cm（改訂版） |
|------|------|------------|
| Unpinトレイト | 必須 | **廃止** |
| PhantomPinned | 必須 | **不要** |
| Pin<&mut T> | 複雑 | **Pin<T>のみ** |
| 学習曲線 | 急 | **緩やか** |

### 2. 言語一貫性

- **変数宣言**: `Type name;` または `Type name = value;`
- **関数定義**: `ReturnType function(Type arg) { ... }`
- **テンプレート**: `template<typename T>` または `<T>`
- **マクロ**: `macro name { ... }` （macro_rules!を廃止）

### 3. 実用性の向上

```cm
// Before: Rust風の複雑な型
Pin<Box<dyn Future<Output = T>>>

// After: Cmのシンプルな型
Pin<Future<T>>
```

## 実装への影響

### コード変更が必要な箇所

1. **マクロパーサー**
   - `macro_rules!` → `macro` キーワード
   - トークン認識の調整

2. **Pin実装**
   - Unpinトレイト関連コードの削除
   - よりシンプルなAPIの実装

3. **ドキュメント**
   - すべての例をCm構文に修正
   - Rust固有の概念を削除

## 利点

1. **学習容易性**: C++プログラマーが即座に理解可能
2. **保守性**: コードベースがシンプルに
3. **一貫性**: Cm言語全体の構文スタイルを維持
4. **実装効率**: 不要な複雑性を排除

## 移行ガイド

### 開発者向け

```cm
// Old（Rust風）
fn process(pin: Pin<&mut Self>) -> Result<(), Error>

// New（Cm風）
Error process(Pin<Self>& pin)
```

### ユーザー向け

```cm
// Old（複雑）
let mut value = SomeStruct::new();
let mut pinned = Box::pin(value);
let ref_pin = pinned.as_mut();

// New（シンプル）
SomeStruct value;
Pin<SomeStruct> pinned(value);
```

## まとめ

この改訂により：

✅ **Unpinの複雑性を完全に排除**
✅ **Cm言語の構文スタイルに100%準拠**
✅ **直感的で理解しやすいAPI**
✅ **C++プログラマーにも親しみやすい設計**

Rustの歴史的な複雑性を繰り返さず、よりシンプルで実用的な設計を実現しました。

---

**作成者:** Claude Code
**承認:** 設計改訂完了
**次ステップ:** 実装準備（必要時）