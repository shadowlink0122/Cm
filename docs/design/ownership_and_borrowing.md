# Cmの所有権・借用システム設計（改訂版）

## 概要

Cmは**安全性を最優先**とし、デフォルトはコピー、効率化が必要な時に明示的にムーブまたは借用を使用します。
C++の安全性（デフォルトコピー）とRustの効率性（借用システム）を組み合わせた、予測可能で実用的な設計です。

---

## 基本原則

### 1. デフォルトはコピー（安全性最優先）

```cm
// すべての型がデフォルトでコピー可能
int a = 10;
int b = a;           // コピー（a は有効）

String s1 = "hello";
String s2 = s1;      // コピー（s1 は有効）

Vec<int> v1 = [1, 2, 3];
Vec<int> v2 = v1;    // コピー（v1 は有効）
```

**理由:** 暗黙のムーブはバグの温床。予測可能性が重要。

### 2. 明示的なムーブ（効率化が必要な時のみ）

```cm
Vec<int> large_data = create_large_vector();
Vec<int> moved = move large_data;   // 明示的ムーブ
// println("{large_data}");         // ❌ コンパイルエラー
```

**重要:** `move`キーワードは必須。暗黙のムーブは禁止。

### 3. 借用（参照）で最効率化

```cm
fn process(v: Vec<int>&) {  // 借用：コピーもムーブもなし
    for x in v {
        println("{x}");
    }
}

Vec<int> data = [1, 2, 3];
process(data);              // data は有効のまま（最速）
```

---

## 型システム

### 型の種類

```cm
// 1. 値型（デフォルトでコピー可能）
int a;
String s;
Vec<int> v;

// 2. 借用型（参照）
int& ref;
String& str_ref;
Vec<int>& vec_ref;

// 3. 生ポインタ型（unsafe）
int* ptr;
String* str_ptr;
```

### Copy型（デフォルト）

```cm
// すべての型がデフォルトでCopy
// - 自動的にコピーコンストラクタが生成される

struct Point {
    int x;
    int y;
}

Point p1 = {10, 20};
Point p2 = p1;       // コピー（p1 は有効）
```

### Move型（明示的）

```cm
// Moveインターフェースを実装した型のみmove可能

struct Buffer with Move {
    int* data;
    int size;
}

Buffer b1;
Buffer b2 = move b1;     // ✅ OK: Move実装済み
// Buffer b3 = b1;       // ❌ エラー: b1 は moved

// Move未実装の型はmoveできない
struct NoMove {
    int value;
}

NoMove x;
// NoMove y = move x;    // ❌ コンパイルエラー: Move未実装
NoMove y = x;            // ✅ OK: コピー
```

### NoCopy型（コピー禁止）

```cm
// コピーを禁止、ムーブのみ許可

struct UniquePtr with Move, NoCopy {
    int* ptr;
}

UniquePtr p1;
// UniquePtr p2 = p1;     // ❌ エラー: NoCopy
UniquePtr p2 = move p1;   // ✅ OK: Move
```

---

## 暗黙のムーブが許可されるケース

### ❌ 通常の代入では暗黙のムーブは禁止

```cm
Vec<int> v1 = [1, 2, 3];
Vec<int> v2 = v1;        // コピー（明確）
// Vec<int> v3 = v1;     // ムーブではない！コピー！
```

### ✅ return文でのRVO（Return Value Optimization）

```cm
fn create_vector() -> Vec<int> {
    Vec<int> result = [1, 2, 3];
    return result;       // 暗黙のムーブ（安全）
}

Vec<int> v = create_vector();  // RVO: コピーなし
```

**RVOが安全な理由:**
- ローカル変数は関数終了後に破棄される
- 呼び出し元で元の変数は存在しない
- バグの可能性がゼロ

---

## 値の受け渡し戦略

### 戦略1: 借用を使う（最優先）

```cm
// ❌ コピーが発生（非効率）
fn print_vector(v: Vec<int>) {  // コピーコンストラクタ呼び出し
    for x in v {
        println("{x}");
    }
}

Vec<int> numbers = [1, 2, 3];
print_vector(numbers);          // コピー（遅い）

// ✅ 借用を使う（効率的）
fn print_vector(v: Vec<int>&) {  // 借用：コピーなし
    for x in v {
        println("{x}");
    }
}

Vec<int> numbers = [1, 2, 3];
print_vector(numbers);           // 借用（速い）
```

### 戦略2: ムーブで所有権転送

```cm
fn consume(data: Vec<int>) {     // 所有権を取得
    // data を消費
}

Vec<int> v = [1, 2, 3];
consume(move v);                 // 明示的ムーブ
// consume(v);                   // ❌ エラー: v は moved
```

### 戦略3: コピーは最終手段

```cm
// 本当にコピーが必要な場合のみ
fn need_copy(data: Vec<int>) {   // コピーコンストラクタ
    // data の独立したコピーが必要
}

Vec<int> v = [1, 2, 3];
need_copy(v);                    // コピー（v は有効）
need_copy(v);                    // OK: 何度でも呼べる
```

---

## インターフェースシステム

### 標準インターフェース

```cm
// Copy - コピー可能（デフォルトで全型に自動実装）
interface Copy {
    Self(Self other);  // コピーコンストラクタ
}

// Move - ムーブ可能（明示的に実装が必要）
interface Move {
    Self(move Self other);  // ムーブコンストラクタ
}

// NoCopy - コピー禁止
interface NoCopy {
    // コピーコンストラクタを削除
}
```

### 使用例

```cm
// 普通の構造体（Copy可能）
struct Point {
    int x;
    int y;
}
// 自動的に Copy 実装
Point p1 = {10, 20};
Point p2 = p1;       // コピー

// Moveのみ可能
struct UniquePtr with Move, NoCopy {
    int* ptr;
}
UniquePtr u1;
// UniquePtr u2 = u1;     // ❌ エラー: NoCopy
UniquePtr u2 = move u1;   // ✅ OK: Move

// CopyもMoveも可能
struct FlexibleData with Move {
    Vec<int> data;
}
FlexibleData d1;
FlexibleData d2 = d1;       // Copy
FlexibleData d3 = move d1;  // Move
```

---

## コンストラクタシステム

### コピーコンストラクタ（自動生成）

```cm
struct Point {
    int x;
    int y;
}

// コピーコンストラクタが自動生成される
// Point(Point other) {
//     self.x = other.x;
//     self.y = other.y;
// }

Point p1 = {10, 20};
Point p2 = p1;       // 自動生成されたコピーコンストラクタ
```

### ムーブコンストラクタ（明示的定義）

```cm
struct Buffer {
    int* data;
    int size;
    
    // ムーブコンストラクタを定義
    Buffer(move Buffer other) {
        self.data = other.data;
        self.size = other.size;
        other.data = null;      // 元を無効化
        other.size = 0;
    }
}

Buffer b1;
b1.data = allocate(100);
Buffer b2 = move b1;     // ムーブコンストラクタ呼び出し
```

---

## 関数パラメータのガイドライン

### 選択フローチャート

```
データを読むだけ？
  YES → 借用 (T&)
  NO  ↓

データを変更する？
  YES → 借用 (T&)
  NO  ↓

所有権を取得する？
  YES → 値渡し (T) + move
  NO  → 借用 (T&)
```

### 具体例

```cm
// 読み取り専用 → 借用
fn calculate_sum(numbers: Vec<int>&) -> int {
    int sum = 0;
    for n in numbers {
        sum = sum + n;
    }
    return sum;
}

// 変更する → 借用
fn append_item(v: Vec<int>&, item: int) {
    v.push(item);
}

// 所有権を取得 → 値渡し + move
fn take_ownership(data: Vec<int>) {
    // data を消費
}
take_ownership(move data);  // 明示的ムーブ必須

// 所有権を返す → 値返し（RVO）
fn create_data() -> Vec<int> {
    Vec<int> result = [1, 2, 3];
    return result;    // 暗黙のムーブ（安全）
}
```

---

## 比較：Rust vs C++ vs Cm

### Rust
```rust
// デフォルトがムーブ（学習コスト高、バグりやすい）
let v1 = vec![1, 2, 3];
let v2 = v1;             // ムーブ！
// println!("{:?}", v1); // ❌ エラー（予期しない）
```

### C++
```cpp
// デフォルトはコピー（安全だが非効率になりがち）
std::vector<int> v1 = {1, 2, 3};
auto v2 = v1;                    // コピー
auto v3 = std::move(v1);         // 明示的ムーブ
```

### Cm
```cm
// デフォルトはコピー（C++と同じ、安全）
Vec<int> v1 = [1, 2, 3];
Vec<int> v2 = v1;                // コピー（安全）
Vec<int> v3 = move v1;           // 明示的ムーブ（効率）

// 借用で最効率（Rust風）
fn use_data(v: Vec<int>&) { }
use_data(v2);                    // 借用（最速）
```

**Cmの利点:**
- ✅ C++の安全性（デフォルトはコピー）
- ✅ Rustの効率性（借用システム）
- ✅ 明示的な制御（moveキーワード必須）
- ✅ 予測可能性（暗黙の動作を最小化）

---

## 実装ロードマップ

### Phase 1（現在実装中）: selfポインタ化

```cm
impl<T> Container<T> {
    void set(T val) {
        self.value = val;    // 内部でポインタ経由
    }
}
```

**目的**: O0でのジェネリックメソッドを修正

### Phase 2（次）: 借用型の導入

```cm
int b = 10;
int& a = b;              // 借用型
a = 20;                  // b も変更される

fn process(x: int&) {
    x = 30;
}
```

**目的**: 効率的な参照渡しの基盤

### Phase 3（その後）: moveキーワード + 検証

```cm
Vec<int> v1 = [1, 2, 3];
Vec<int> v2 = move v1;   // 明示的ムーブ
// println("{v1}");      // コンパイルエラー
```

**目的**: 所有権移動の明示化と検証

### Phase 4（将来）: インターフェースシステム

```cm
struct Data with Move, NoCopy {
    // Moveのみ可能
}
```

**目的**: Copy/Move/NoCopyの制御

### Phase 5（将来）: コンストラクタシステム

```cm
struct Buffer {
    Buffer(Buffer other) { }        // コピーコンストラクタ
    Buffer(move Buffer other) { }   // ムーブコンストラクタ
}
```

**目的**: カスタムコンストラクタのサポート

### Phase 6（将来）: 借用チェッカー

```cm
int& a = x;
int& b = x;              // OK: 複数の不変借用
// x = 10;               // NG: 借用中は変更不可
```

**目的**: コンパイル時の安全性保証

---

## まとめ

### Cmの所有権システムの特徴

1. ✅ **デフォルトはコピー** - 安全性最優先
2. ✅ **明示的なmove** - バグ防止、予測可能
3. ✅ **借用で最効率化** - C++風の`int&`構文
4. ✅ **段階的実装** - 既存コードと共存可能
5. ✅ **予測可能性** - 暗黙の動作を最小化

### 効率的なコードの書き方

```cm
// ✅ 基本方針
// 1. 読み取りのみ → 借用（最速）
fn read(data: Data&) { }

// 2. 変更 → 借用（最速）
fn modify(data: Data&) { }

// 3. 消費 → 値渡し + move（明示的）
fn consume(data: Data) { }
consume(move data);

// 4. 生成 → 値返し（RVO）
fn create() -> Data { return data; }

// 5. 小さいデータ → コピー（シンプル）
fn add(x: int, y: int) -> int { return x + y; }
```

### 設計哲学

```cm
// 初心者に優しい（C++と同じ）
String s1 = "hello";
String s2 = s1;          // コピー（安全）

// 上級者にも最適（効率化可能）
String s3 = move s1;     // ムーブ（効率）

// 借用で最高効率
fn use_string(s: String&) { }
use_string(s2);          // 最速
```

---

**Cmは、安全性と効率性を両立した、予測可能で実用的な言語です。**
