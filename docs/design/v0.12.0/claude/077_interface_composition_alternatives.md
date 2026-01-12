# インターフェース合成の代替案分析

作成日: 2026-01-11
ステータス: 研究
優先度: 低
関連: 076_interface_inheritance_design.md

## 1. 問題の本質

### 1.1 シグネチャ一致の制約

```cm
// 問題例1: 戻り値の型が固定される
interface Container {
    int size();  // intで固定
}

interface Stack with Container {
    // size()はintを返さなければならない
    // size_tやulongを返したくてもできない
}

// 問題例2: ジェネリクスの不整合
interface GenericContainer<T> {
    T get(int index);
}

interface SpecificStack {
    int get(int index);  // intに特化したい
}

// これは不可能：
interface SpecificStack with GenericContainer<int> {
    // GenericContainer<int>::getと衝突
}
```

### 1.2 根本的な課題

1. **型の硬直性**: 継承すると型が完全に固定される
2. **進化の困難**: 基底インターフェースの変更が派生全体に影響
3. **最適化の制限**: 特定の実装に最適な型を選べない

## 2. 代替案の検討

### 2.1 案A: 関連型（Associated Types）

```cm
// 型をパラメータ化する
interface Container {
    typedef SizeType;  // 関連型
    typedef ElementType;

    SizeType size();
    ElementType get(SizeType index);
}

// 実装時に具体的な型を指定
impl Vector for Container {
    typedef SizeType = size_t;
    typedef ElementType = int;

    size_t size() { return self.len; }
    int get(size_t index) { return self.data[index]; }
}
```

**評価**:
- ✅ 柔軟な型選択
- ✅ 実装ごとの最適化可能
- ❌ 複雑性が増す
- ❌ 型推論が困難

### 2.2 案B: プロトコル拡張（Swift風）

```cm
// 基本プロトコル
interface Container {
    int count();
}

// 拡張でデフォルト実装を提供
extension Container {
    bool is_empty() {
        return self.count() == 0;
    }

    // デフォルト実装だが、オーバーライド可能
    int size() {
        return self.count();
    }
}

impl MyStack for Container {
    int count() { return self.len; }
    // is_empty()とsize()は自動的に利用可能

    // 必要ならオーバーライド
    bool is_empty() {
        // 特殊な最適化実装
        return self.top == null;
    }
}
```

**評価**:
- ✅ 実装の再利用
- ✅ 選択的オーバーライド
- ✅ 後方互換性
- ❌ 新しい言語機能が必要

### 2.3 案C: トレイト要求の明示化（現状維持の改良）

```cm
// 複数の小さなインターフェース
interface Sized {
    int size();
}

interface Indexed<T> {
    T get(int index);
}

interface Clearable {
    void clear();
}

// 必要な組み合わせを要求
<T> void process(T container)
    where T: Sized, T: Indexed<int>, T: Clearable {
    // 3つのインターフェースを満たす任意の型
}

// マクロで簡潔に
#define Container Sized + Indexed<int> + Clearable

<T: Container> void process2(T container) {
    // マクロで簡潔に記述
}
```

**評価**:
- ✅ 既存システムで実現可能
- ✅ 柔軟な組み合わせ
- ✅ 実装の独立性
- ❌ 冗長な記述

### 2.4 案D: ミックスイン実装

```cm
// 実装を含むミックスイン
mixin ContainerMixin {
    // フィールドも持てる
    private int cached_size = -1;

    // デフォルト実装
    bool is_empty() {
        return self.size() == 0;
    }

    void validate_index(int idx) {
        if (idx < 0 || idx >= self.size()) {
            panic("Index out of bounds");
        }
    }
}

struct MyVector {
    include ContainerMixin;  // ミックスインを取り込む

    int* data;
    int len;

    // size()を実装すればis_empty()が使える
    int size() { return self.len; }
}
```

**評価**:
- ✅ コード再利用
- ✅ 実装の共有
- ❌ 多重継承の複雑性
- ❌ ダイヤモンド問題

## 3. 実用的な解決策

### 3.1 短期的推奨（現実的）

```cm
// 1. 小さなインターフェースに分割
interface HasSize {
    int size();
}

interface HasCapacity {
    int capacity();
}

// 2. エイリアスで組み合わせ（マクロまたは型エイリアス）
typedef Container = HasSize + HasCapacity;

// 3. 実装ヘルパー関数の提供
module std::containers::helpers {
    // 共通ロジックを関数として提供
    <T: HasSize> bool is_empty(T& container) {
        return container.size() == 0;
    }

    <T: HasSize + HasCapacity> float load_factor(T& container) {
        return (float)container.size() / container.capacity();
    }
}
```

### 3.2 中期的推奨（言語拡張）

```cm
// インターフェースにデフォルト実装を許可
interface Container {
    int size();

    // デフォルト実装（オーバーライド可能）
    default bool is_empty() {
        return self.size() == 0;
    }
}

// 条件付きデフォルト実装
interface Optimizable {
    default void optimize()
        where Self: HasCapacity {
        // HasCapacityを実装している場合のみ
        if (self.size() < self.capacity() / 2) {
            self.shrink_to_fit();
        }
    }
}
```

### 3.3 長期的方向性

```cm
// コンセプト（C++20風）による制約
concept ContainerLike<T> = requires(T t) {
    { t.size() } -> ConvertibleTo<int>;
    { t.is_empty() } -> bool;
};

// より柔軟な型制約
<T> void process(T container)
    requires ContainerLike<T> {
    // size()の戻り値はintに変換可能なら何でもOK
}
```

## 4. 段階的移行計画

### Phase 1: 現状維持と文書化
- 小さなインターフェースの推奨
- ベストプラクティスガイド作成
- ヘルパー関数ライブラリの充実

### Phase 2: 簡単な拡張
- インターフェースのエイリアス機能
- マクロによる簡潔な記述

### Phase 3: デフォルト実装
- `default`キーワード導入
- 選択的オーバーライド

### Phase 4: 高度な型システム
- 関連型
- コンセプト
- 型制約の柔軟化

## 5. 結論

### 5.1 with句継承の問題点

1. **型の完全一致要求が非現実的**
2. **進化性の欠如**
3. **最適化の妨げ**

### 5.2 推奨アプローチ

**短期的には現状維持が最善**:
- 小さなインターフェースの組み合わせ
- ヘルパー関数による実装共有
- 明示的な要求の記述

**将来的にはデフォルト実装**:
- オーバーライド可能なデフォルトメソッド
- プロトコル拡張的なアプローチ
- 実装の共有と柔軟性の両立

### 5.3 優先度

**低優先度が妥当な理由**:
1. 現状でも回避策が存在
2. 他の機能（マクロ、インライン）の方が影響大
3. 型システムの成熟を待つべき

## 6. 実装例：現実的な解決

```cm
// === 現在可能な最善のアプローチ ===

// Step 1: 原子的インターフェース
interface Countable {
    int count();
}

interface Clearable {
    void clear();
}

interface Pushable<T> {
    void push(T item);
}

interface Poppable<T> {
    T pop();
}

// Step 2: 実装
struct Stack<T> {
    T* data;
    int len;
    int cap;
}

impl<T> Stack<T> for Countable {
    int count() { return self.len; }
}

impl<T> Stack<T> for Clearable {
    void clear() { self.len = 0; }
}

impl<T> Stack<T> for Pushable<T> {
    void push(T item) {
        // 実装
    }
}

impl<T> Stack<T> for Poppable<T> {
    T pop() {
        // 実装
    }
}

// Step 3: 使用時の組み合わせ
<T, C> void process_stack(C& container)
    where C: Countable + Clearable + Pushable<T> + Poppable<T> {
    // 4つのインターフェースを使用
    if (container.count() > 100) {
        container.clear();
    }
}

// Step 4: ヘルパー関数で共通処理
module stack_helpers {
    <C: Countable> bool is_empty(C& c) {
        return c.count() == 0;
    }

    <T, C> T pop_or_default(C& c, T default_val)
        where C: Poppable<T> + Countable {
        if (c.count() == 0) {
            return default_val;
        }
        return c.pop();
    }
}
```

## 7. まとめ

インターフェース継承（with句）は一見便利だが、型の硬直性という本質的な問題を抱えている。現時点では：

1. **小さな原子的インターフェースの組み合わせ**
2. **ヘルパー関数による実装共有**
3. **明示的な型制約の記述**

が最も実用的なアプローチである。将来的にはデフォルト実装やプロトコル拡張などの機能を検討すべきだが、優先度は低く設定することが妥当。

---

**作成者**: Claude Code
**ステータス**: 研究段階
**次のアクション**: v1.0以降で再検討