# Cm言語 ジェネリック型推論設計

## 概要

Cm言語では、明示的な `template` や `<T>` 宣言なしに、ジェネリック型パラメータを自動推論します。

## 推論ルール

### 1. 命名規則による推論

以下の条件を満たす型名は、ジェネリック型パラメータと推論されます：

```cm
// 単一大文字：最も一般的なジェネリック
T max(T a, T b) { ... }           // T はジェネリック
U convert(T input) { ... }        // T, U はジェネリック
K get_key(Map<K, V> map) { ... }  // K, V はジェネリック

// エラー型の慣例
E handle_error(Result<T, E> r) { ... }  // T, E はジェネリック
```

**ルール**：
- 単一の大文字（T, U, V, K, E, R など）
- 最大2文字の大文字（TV, KV など）も許容
- ただし、既知の型名（Int, String など）は除外

### 2. コンテキストによる推論

同じ関数内で一貫して使用される未知の型：

```cm
// MyType が未定義なら、ジェネリックと推論
MyType identity(MyType value) {
    return value;
}

// ただし警告を出す（明示的な宣言を推奨）
// Warning: 'MyType' is inferred as generic. Consider using single letter or explicit declaration.
```

### 3. 制約による明示

型制約が必要な場合は、`where` 句を使用：

```cm
// 制約付きジェネリック
T max(T a, T b) where T: Ord {
    return a > b ? a : b;
}

// 複数の制約
T process(T value) where T: Debug + Clone {
    debug_print(value);
    return value.clone();
}

// 複雑な制約
U convert(T from) where T: Into<U>, U: Default {
    return from.into();
}
```

### 4. 明示的な宣言（オプション）

曖昧さを避けるため、明示的に宣言することも可能：

```cm
// 明示的なジェネリック宣言（従来の方法も使用可能）
<T> T max(T a, T b) { ... }

// または関数名の後に
T max<T>(T a, T b) { ... }

// 複数パラメータ
<T, U> U convert(T from) { ... }
```

## 構造体での推論

```cm
// 構造体定義でも同じルール適用
struct Vec<T> {  // 明示的
    T* data;
    size_t size;
}

// または暗黙的（Tが自動推論）
struct List {
    T value;      // T はジェネリックと推論
    List<T>* next;
}

// impl ブロック
impl Vec<T> {  // T は Vec<T> から推論
    void push(T item) { ... }
    T pop() { ... }
}
```

## 型エイリアスでの推論

```cm
// typedefでも同様
typedef Result<T> = Ok(T) | Err(string);  // T はジェネリック
typedef Option<T> = Some(T) | None;       // T はジェネリック

// 使用時
Result<int> divide(int a, int b) { ... }
Option<string> find(string key) { ... }
```

## 推論の優先順位

1. **既知の型**：スコープ内で定義済み → 具体型
2. **単一大文字**：T, U, V など → ジェネリック型
3. **制約付き**：where句がある → ジェネリック型
4. **一貫した使用**：パラメータと戻り値で使用 → ジェネリック型（警告付き）
5. **その他**：エラー（未定義の型）

## エラーと警告

```cm
// OK：Tは明確にジェネリック
T identity(T value) { return value; }

// 警告：曖昧な名前
DataType process(DataType input) { ... }
// Warning: 'DataType' inferred as generic. Use single letter (e.g., 'T') or add explicit generic declaration.

// エラー：不一致な使用
T function(U value) { return value; }  // Error: Type mismatch

// エラー：既知の型との衝突
int max(int a, String b) { ... }  // Error: Type mismatch
```

## 利点

1. **簡潔性**：冗長な `template` 宣言が不要
2. **可読性**：コードがよりクリーン
3. **慣例準拠**：一般的な命名規則に従う
4. **段階的**：明示的な宣言も可能

## 実装例

```cm
// シンプルな関数
T min(T a, T b) {
    return a < b ? a : b;
}

// コレクション操作
R fold(Vec<T> items, R init, R reducer(R, T)) {
    R result = init;
    for (T item : items) {
        result = reducer(result, item);
    }
    return result;
}

// 型変換
U cast(T value) where T: Into<U> {
    return value.into();
}

// 複雑な例
struct Tree<T> {
    T value;
    Tree<T>* left;
    Tree<T>* right;
}

impl Tree<T> {
    // T は Tree<T> から自動推論
    void insert(T item) where T: Ord {
        if (item < value) {
            if (left == null) {
                left = new Tree<T>{item, null, null};
            } else {
                left->insert(item);
            }
        } else {
            if (right == null) {
                right = new Tree<T>{item, null, null};
            } else {
                right->insert(item);
            }
        }
    }

    bool contains(T item) where T: Eq {
        if (item == value) return true;
        if (left && left->contains(item)) return true;
        if (right && right->contains(item)) return true;
        return false;
    }

    // 別の型への変換
    U map(U mapper(T)) {
        Tree<U>* result = new Tree<U>{
            mapper(value),
            left ? left->map(mapper) : null,
            right ? right->map(mapper) : null
        };
        return result;
    }
}

// 使用例
int main() {
    // 型は使用時に具体化
    int max_int = max(10, 20);           // T = int
    string max_str = max("a", "b");      // T = string

    Tree<int> tree = {50, null, null};
    tree.insert(30);
    tree.insert(70);

    bool found = tree.contains(30);      // true

    // 型変換
    Tree<string>* str_tree = tree.map((int x) -> toString(x));

    return 0;
}
```

## コンパイラの実装

### パーサーの処理フロー

1. **型名の収集**：関数シグネチャから型名を抽出
2. **分類**：
   - 既知の型 → 具体型として処理
   - 単一大文字 → ジェネリックパラメータとして登録
   - 未知の型 → 一貫性チェック
3. **一貫性チェック**：
   - 同じ名前が一貫して使用されているか
   - 型制約と矛盾しないか
4. **ジェネリック関数として登録**

### 型チェッカーの処理

1. **インスタンス化**：使用時に具体型で置換
2. **制約チェック**：where句の条件を満たすか確認
3. **単一化**：型推論により具体型を決定

## 移行パス

既存のC++スタイルコードからの段階的移行：

```cm
// Phase 1: 従来のC++スタイル（サポート継続）
template<typename T>
T max(T a, T b) { return a > b ? a : b; }

// Phase 2: 簡略化（<T>を関数名の後に）
T max<T>(T a, T b) { return a > b ? a : b; }

// Phase 3: 完全な推論（推奨）
T max(T a, T b) { return a > b ? a : b; }
```

すべての形式をサポートし、段階的な移行を可能にします。