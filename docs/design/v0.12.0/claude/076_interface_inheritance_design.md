# Cm言語 インターフェース継承設計

作成日: 2026-01-11
ステータス: 提案
対象バージョン: v0.12.0以降

## 1. 現在の仕様（確認事項）

### 1.1 impl単独ブロックの制約

```cm
// impl単独ブロックはコンストラクタ・デストラクタのみ定義可能
impl MyStruct {
    // ✓ コンストラクタ（許可）
    self() {
        self.value = 0;
    }

    // ✓ 引数付きコンストラクタ（許可）
    self(int val) {
        self.value = val;
    }

    // ✓ デストラクタ（許可）
    ~self() {
        // クリーンアップ処理
    }

    // ✗ 通常メソッド（エラー）
    void method() {  // コンパイルエラー
        // メソッドはinterfaceとimpl forで定義
    }
}
```

### 1.2 メソッド定義の正しい方法

```cm
// Step 1: インターフェース宣言
interface Stackable {
    void push(int value);
    int pop();
    bool is_empty();
}

// Step 2: 構造体定義
struct Stack {
    int* data;
    int size;
    int capacity;
}

// Step 3: コンストラクタ・デストラクタ
impl Stack {
    self() {
        self.capacity = 16;
        self.size = 0;
        self.data = alloc<int>(self.capacity);
    }

    ~self() {
        free(self.data);
    }
}

// Step 4: インターフェース実装
impl Stack for Stackable {
    void push(int value) {
        if (self.size >= self.capacity) {
            // 拡張処理
        }
        self.data[self.size++] = value;
    }

    int pop() {
        if (self.size == 0) panic("Stack underflow");
        return self.data[--self.size];
    }

    bool is_empty() {
        return self.size == 0;
    }
}
```

## 2. 提案：インターフェース継承（with句）

### 2.1 基本構文

```cm
// 基本インターフェース
interface Container {
    int size();
    bool is_empty();
    void clear();
}

// 継承インターフェース（Containerの全メソッドを含む）
interface Stack with Container {
    void push(int value);
    int pop();
    int peek();
}

// 多重継承
interface Deque with Stack, Queue {
    void push_front(int value);
    int pop_back();
}
```

### 2.2 展開後の等価コード

```cm
// `interface Stack with Container`は以下と等価：
interface Stack {
    // Containerから継承
    int size();
    bool is_empty();
    void clear();

    // Stack固有
    void push(int value);
    int pop();
    int peek();
}
```

## 3. メリット・デメリット分析

### 3.1 メリット

#### 3.1.1 実装忘れ防止
```cm
// 現在の方法：実装忘れが発生しやすい
struct MyStack { /* ... */ }

impl MyStack for Container {
    int size() { return self.len; }
    // is_emptyとclearを忘れた！
}

impl MyStack for Stackable {
    void push(int v) { /* ... */ }
    int pop() { /* ... */ }
}

// 提案方法：Stackを実装すればContainerも自動的に必要
impl MyStack for Stack {  // StackはContainerを含む
    // Container分のメソッド
    int size() { return self.len; }
    bool is_empty() { return self.len == 0; }
    void clear() { self.len = 0; }

    // Stack分のメソッド
    void push(int v) { /* ... */ }
    int pop() { /* ... */ }
    int peek() { /* ... */ }
}
```

#### 3.1.2 意味的な階層の明確化
```cm
interface Collection {
    int size();
    void clear();
}

interface Iterable with Collection {
    Iterator iterator();
}

interface List with Iterable {
    void add(int value);
    int get(int index);
    void remove(int index);
}

// 階層関係が明確：List ⊃ Iterable ⊃ Collection
```

#### 3.1.3 型制約の簡潔化
```cm
// 現在：複数の制約が必要
<T> void process(T data)
    where T: Container, T: Iterable {
    // ...
}

// 提案：単一の制約で十分
<T> void process(T data)
    where T: Iterable {  // IterableはContainerを含む
    // ...
}
```

### 3.2 デメリット

#### 3.2.1 実装部分の肥大化
```cm
// 深い継承階層の場合
interface A { /* 5メソッド */ }
interface B with A { /* +3メソッド = 8メソッド */ }
interface C with B { /* +4メソッド = 12メソッド */ }
interface D with C { /* +2メソッド = 14メソッド */ }

// Dを実装する際、14メソッド全てを実装する必要がある
impl MyType for D {
    // 14個のメソッド実装...
}
```

#### 3.2.2 柔軟性の低下
```cm
// 現在：部分的な実装が可能
struct FlexStack { /* ... */ }

impl FlexStack for Container {
    // Containerだけ実装
}

// 必要に応じて後から追加
impl FlexStack for Stackable {
    // Stackableを追加実装
}

// 提案：all-or-nothing
impl FlexStack for Stack {
    // ContainerとStack両方を同時に実装必須
}
```

#### 3.2.3 名前衝突の可能性
```cm
interface Drawable {
    void draw();
}

interface Card {
    void draw();  // カードを引く
}

// エラー：drawメソッドが衝突
interface GameCard with Drawable, Card {
    // どちらのdraw()？
}
```

## 4. 実装設計

### 4.1 パーサー拡張

```cpp
// src/parser/interface_parser.hpp
struct InterfaceDecl {
    std::string name;
    std::vector<std::string> base_interfaces;  // with句のインターフェース
    std::vector<MethodDecl> methods;

    // 展開後の全メソッド（継承含む）
    std::vector<MethodDecl> all_methods;
};

// BNF拡張
// interface_decl ::= 'interface' identifier interface_inheritance? '{' method_decl* '}'
// interface_inheritance ::= 'with' identifier (',' identifier)*
```

### 4.2 セマンティック解析

```cpp
class InterfaceResolver {
    // インターフェース継承の解決
    void resolve_inheritance(InterfaceDecl& iface) {
        for (const auto& base_name : iface.base_interfaces) {
            auto base = find_interface(base_name);
            if (!base) {
                error("Unknown interface: " + base_name);
            }

            // 再帰的に基底インターフェースを解決
            resolve_inheritance(*base);

            // メソッドをマージ
            for (const auto& method : base->all_methods) {
                // 名前衝突チェック
                if (has_method(iface, method.name)) {
                    error("Method name conflict: " + method.name);
                }
                iface.all_methods.push_back(method);
            }
        }

        // 自身のメソッドを追加
        for (const auto& method : iface.methods) {
            iface.all_methods.push_back(method);
        }
    }
};
```

### 4.3 実装チェック

```cpp
class ImplementationChecker {
    // impl for の完全性チェック
    void check_implementation(const ImplForDecl& impl) {
        auto iface = find_interface(impl.interface_name);

        // 継承を含む全メソッドをチェック
        for (const auto& required : iface->all_methods) {
            if (!has_implementation(impl, required)) {
                error("Missing implementation for: " + required.name);
            }
        }
    }
};
```

## 5. 代替案

### 5.1 トレイトエイリアス（Rust風）
```cm
// エイリアスで複数インターフェースを束ねる
trait StackLike = Container + Stackable;

impl MyStack for StackLike {
    // ContainerとStackable両方を実装
}
```

### 5.2 デフォルト実装
```cm
interface Container {
    int size();

    // デフォルト実装付き
    bool is_empty() default {
        return self.size() == 0;
    }
}

// is_empty()は実装しなくても良い（オーバーライド可能）
impl MyType for Container {
    int size() { return self.len; }
    // is_empty()はデフォルト実装を使用
}
```

### 5.3 明示的な継承マーカー
```cm
// extendsキーワードで継承を明示
interface Stack extends Container {
    void push(int value);
    int pop();
}

// implementsで実装を明示
impl MyStack implements Stack, Container {
    // 両方のメソッドを実装
}
```

## 6. 推奨事項

### 6.1 採用する場合の制約

1. **継承は浅く保つ**：最大3階層まで
2. **名前衝突の解決策**：
   - エラーとする（安全）
   - 明示的な解決構文を導入
   - エイリアスで回避

### 6.2 段階的実装

1. **Phase 1**: 基本的なwith句サポート（単一継承）
2. **Phase 2**: 多重継承サポート
3. **Phase 3**: デフォルト実装
4. **Phase 4**: 名前衝突解決機構

## 7. 結論と提案

### 推奨度: ⭐⭐⭐☆☆（中程度）

**採用すべき理由**：
- 実装忘れ防止は実際の開発で重要
- 型階層の明確化はコードの理解性向上
- 他の現代言語（Rust, Swift）でも類似機能あり

**慎重になるべき理由**：
- 実装の肥大化は保守性を低下させる
- 柔軟性の低下は段階的な開発を困難にする
- 名前衝突の処理が複雑

### 折衷案

```cm
// オプション1: 明示的な継承表明
interface Stack with Container {
    // Containerのメソッドは明示的に列挙
    using Container::*;  // 全メソッドを継承

    // または選択的に
    using Container::size;
    using Container::is_empty;

    // Stack固有のメソッド
    void push(int value);
    int pop();
}

// オプション2: 実装時の選択
impl MyStack for Stack {
    // 自動的にContainerも実装（デフォルト）
}

impl MyStack for Stack without Container {
    // Stackだけ実装（Containerは別途実装）
}
```

## 8. テストケース

```cm
// test/interface_inheritance.cm
interface Base {
    int base_method();
}

interface Derived with Base {
    int derived_method();
}

struct TestStruct {
    int value;
}

impl TestStruct for Derived {
    int base_method() {
        return self.value;
    }

    int derived_method() {
        return self.value * 2;
    }
}

void test_interface_inheritance() {
    TestStruct t = {value: 10};

    // Derivedとして使用
    assert(t.derived_method() == 20);

    // Baseとしても使用可能
    Base* b = &t;
    assert(b->base_method() == 10);
}
```

## 9. 移行パス

既存コードへの影響を最小化：

1. **後方互換性維持**：with句は新機能として追加
2. **段階的移行**：既存のインターフェースは変更不要
3. **ツール支援**：自動リファクタリングツール提供

---

**作成者**: Claude Code
**レビュー状態**: 未レビュー
**次のステップ**: チーム討議とプロトタイプ実装