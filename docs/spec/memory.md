[English](memory.en.html)

# Cm メモリモデル設計

## 概要

Cmは **手動メモリ管理** を採用します。GCはなく、開発者が明示的にメモリを管理します。

## 設計原則

| 機能 | 対応 | 備考 |
|------|------|------|
| GC | ❌ なし | パフォーマンス重視 |
| 手動管理 | ✅ あり | new/delete |
| 参照 | ✅ あり | 借用（所有権なし） |
| ポインタ | ✅ あり | 低レイヤー用 |
| shared | ✅ あり | 参照カウント共有 |

---

## メモリ管理の種類

### 1. スタック（値型）

```cpp
// Cm - スタック確保（自動解放）
void example() {
    int x = 42;
    Point p = Point(10, 20);
}  // スコープ終了で自動解放
```

### 2. ヒープ (new/delete)

```cpp
// Cm - 明示的なヒープ管理
void example() {
    Point* p = new Point(10, 20);  // ヒープ確保
    
    // ... 使用 ...
    
    delete p;  // 明示的解放（必須）
}
```

### 3. shared（参照カウント共有）

```cpp
// Cm - 共有所有権
void example() {
    shared<Point> p1 = make_shared<Point>(10, 20);
    shared<Point> p2 = p1;  // 参照カウント増加
    
    // p1, p2 両方がスコープを抜けると自動解放
}
```

---

## 参照・ポインタ・sharedの違い

| 種類 | 記法 | 所有権 | 解放 | 用途 |
|------|------|--------|------|------|
| **参照** | `&T` | ❌ なし | なし | 一時的な借用 |
| **ポインタ** | `*T` | ❌ なし | 手動 | 低レイヤー、FFI |
| **shared** | `shared<T>` | ✅ 共有 | 自動 | 複数箇所で共有 |
| **new** | `T*` | ✅ 単独 | 手動delete | 単一所有者 |

### 参照 (`&T`)

**所有権なし、解放責任なし**。既存オブジェクトへの一時的アクセス。

```cpp
// Cm
void print_point(&Point p) {
    println("(${p.x}, ${p.y})");
}

void main() {
    Point p = Point(10, 20);
    print_point(&p);  // pへの参照を渡す
}  // pはここで解放
```

### ポインタ (`*T`)

**低レイヤー用**。アドレス演算、FFI、null許容。

```cpp
// Cm
void example() {
    int* ptr = new int(42);
    *ptr = 100;           // 間接参照
    ptr = ptr + 1;        // アドレス演算（危険）
    delete ptr;
}

// FFI
extern "C" {
    void* malloc(size_t size);
    void free(void* ptr);
}
```

### shared (`shared<T>`)

**参照カウント共有**。複数箇所で同じオブジェクトを共有。

```cpp
// Cm
struct Node {
    int value;
    shared<Node> next;
};

void example() {
    shared<Node> head = make_shared<Node>(1, null);
    shared<Node> second = make_shared<Node>(2, null);
    head.next = second;  // secondの参照カウント: 2
    
    shared<Node> alias = head;  // headの参照カウント: 2
}  // 全てスコープを抜けると自動解放
```

---

## コンストラクタ / デストラクタ

```cpp
// Cm
struct File {
    int handle;
    
    // コンストラクタ
    File(String path) {
        this.handle = open(path);
    }
    
    // デストラクタ
    ~File() {
        close(this.handle);
    }
}

void example() {
    File* f = new File("data.txt");
    // ... 使用 ...
    delete f;  // ~File() が呼ばれる
}

// RAIIパターン（スタック）
void example2() {
    File f = File("data.txt");  // スタック
    // ... 使用 ...
}  // スコープ終了で ~File() 自動呼び出し
```

---

## Rustバックエンドへの変換

| Cm | Rust |
|----|------|
| スタック `T` | `T` |
| `new T()` | `Box::new(T())` |
| `delete p` | `drop(p)` |
| `shared<T>` | `Rc<T>` / `Arc<T>` |
| `&T` | `&T` |
| `*T` | `*mut T` / `*const T` |

```cpp
// Cm
shared<Point> p = make_shared<Point>(10, 20);

// → Rust
let p: Rc<Point> = Rc::new(Point::new(10, 20));
```

---

## TypeScript/WASMバックエンド

TSではGCがあるため、new/deleteは無視され自動管理。

```cpp
// Cm
Point* p = new Point(10, 20);
delete p;

// → TypeScript
const p = new Point(10, 20);
// deleteは無視（JSのGCに任せる）
```

---

## メモリ安全性

> [!CAUTION]
> Cmには借用チェッカーがありません。以下は開発者責任です：
> - Use-after-free の回避
> - ダングリングポインタの回避
> - メモリリークの回避

推奨パターン:
1. **スタック優先**: 可能な限りスタック変数を使用
2. **RAII**: デストラクタで自動クリーンアップ
3. **shared**: 複雑な所有権には `shared<T>` を使用