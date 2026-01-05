# ムーブセマンティクスを活用した効率的な値渡しパターン

## コピーコンストラクタが不要な理由

Cmでは**コピーコンストラクタを一切使わず**、ムーブセマンティクスと借用で効率的なコードを実現します。

---

## パターン1: 借用で読み取り専用アクセス

### ❌ コピーコンストラクタ方式（C++）

```cpp
// C++: コピーが発生
void print_data(std::vector<int> v) {  // コピーコンストラクタ呼び出し
    for (int x : v) {
        std::cout << x << std::endl;
    }
}  // v が破棄される

std::vector<int> data = {1, 2, 3, 4, 5};
print_data(data);  // 重い！全要素をコピー
```

### ✅ Cm方式：借用で効率化

```cm
fn print_data(v: Vec<int>&) {  // 借用：ポインタ1つのみ
    for x in v {
        println("{x}");
    }
}

Vec<int> data = [1, 2, 3, 4, 5];
print_data(data);  // 速い！ポインタ渡しのみ
```

**効率化の理由:**
- コピーなし（ポインタのみ渡す）
- メモリアロケーションなし
- 元データはそのまま有効

---

## パターン2: ムーブで所有権転送

### ❌ コピー + デストラクタ方式（C++）

```cpp
// C++: コピーと破棄の両方が発生
std::unique_ptr<LargeObject> transfer_ownership(
    std::unique_ptr<LargeObject> obj) {  // ムーブはあるが...
    // 処理
    return obj;  // ムーブ
}

auto data = std::make_unique<LargeObject>();
data = transfer_ownership(std::move(data));  // OK
```

### ✅ Cm方式：シンプルなムーブ

```cm
fn transfer_ownership(obj: LargeObject) -> LargeObject {
    // 処理
    return obj;  // ムーブ（ポインタのコピーのみ）
}

LargeObject data;
data = transfer_ownership(move data);  // 速い！
```

**効率化の理由:**
- コピーコンストラクタ不要
- デストラクタ呼び出し最小化
- 所有権の移動のみ

---

## パターン3: インプレース変更（最効率）

### ❌ 新オブジェクト作成方式

```cm
// 非効率：毎回新しいオブジェクト
fn add_element(v: Vec<int>, elem: int) -> Vec<int> {
    v.push(elem);
    return v;  // ムーブでも所有権の移動がある
}

Vec<int> numbers = [1, 2, 3];
numbers = add_element(move numbers, 4);
numbers = add_element(move numbers, 5);  // ムーブの連鎖
```

### ✅ Cm方式：借用で直接変更

```cm
// 効率的：同じオブジェクトを変更
fn add_element(v: Vec<int>&, elem: int) {
    v.push(elem);  // 直接変更
}

Vec<int> numbers = [1, 2, 3];
add_element(numbers, 4);
add_element(numbers, 5);  // 借用の連鎖（最速）
```

**効率化の理由:**
- 所有権の移動すら不要
- スタック操作のみ
- メモリレイアウトそのまま

---

## パターン4: ファクトリパターン

### ✅ Cm方式：ムーブで返す

```cm
fn create_large_data(size: int) -> Vec<int> {
    Vec<int> result;
    for i in 0..size {
        result.push(i);
    }
    return result;  // ムーブ（コピーなし）
}

Vec<int> data = create_large_data(10000);  // 効率的！
```

**効率化の理由:**
- Return Value Optimization (RVO) 的な動作
- ヒープポインタのムーブのみ
- データ本体はコピーされない

---

## パターン5: ビルダーパターン

### ✅ Cm方式：ムーブで連鎖

```cm
struct Config {
    String name;
    int value;
    bool enabled;
}

impl Config {
    // self をムーブして返す
    Config set_name(self, name: String) -> Config {
        self.name = move name;
        return self;
    }
    
    Config set_value(self, value: int) -> Config {
        self.value = value;
        return self;
    }
    
    Config enable(self) -> Config {
        self.enabled = true;
        return self;
    }
}

// メソッドチェーン
Config cfg;
cfg = cfg.set_name("test")
         .set_value(42)
         .enable();  // ムーブの連鎖（コピーなし）
```

**効率化の理由:**
- 各メソッドでムーブのみ
- 中間オブジェクトのコピーなし
- 最終的に1つのオブジェクトのみ

---

## パターン6: コンテナの初期化

### ❌ コピーで初期化

```cpp
// C++: 要素のコピー
std::vector<std::string> vec;
std::string s = "hello";
vec.push_back(s);  // コピー
```

### ✅ Cm方式：ムーブで初期化

```cm
Vec<String> vec;
String s = "hello";
vec.push(move s);  // ムーブ（コピーなし）
// s はもう使えない
```

**効率化の理由:**
- 文字列のコピーなし
- メモリアロケーション1回のみ

---

## パターン7: 構造体フィールドへの代入

### ✅ Cm方式：ムーブで効率化

```cm
struct Container {
    Vec<int> data;
    String name;
}

Vec<int> numbers = [1, 2, 3, 4, 5];
String title = "My Container";

Container c;
c.data = move numbers;  // ムーブ（コピーなし）
c.name = move title;    // ムーブ（コピーなし）
```

**効率化の理由:**
- ポインタの移動のみ
- データ本体はコピーされない

---

## パターン8: スワップ操作

### ❌ コピーを使ったスワップ

```cpp
// C++: 3回のコピー/ムーブ
std::string a = "hello";
std::string b = "world";
std::string temp = a;  // コピー
a = b;                 // コピー
b = temp;              // コピー
```

### ✅ Cm方式：ムーブでスワップ

```cm
String a = "hello";
String b = "world";

// 標準ライブラリのswap（内部でムーブ使用）
swap(a, b);  // ポインタの交換のみ

// または明示的に
String temp = move a;
a = move b;
b = move temp;
```

**効率化の理由:**
- ポインタの交換のみ
- 文字列データは移動しない

---

## パターン9: オプショナル型の処理

### ✅ Cm方式：ムーブで取り出す

```cm
Option<Vec<int>> maybe_data = Some([1, 2, 3]);

// ムーブで値を取り出す
if let Some(data) = move maybe_data {
    println("Got data with {data.len()} elements");
    // data の所有権を取得
}
// maybe_data はもう使えない
```

**効率化の理由:**
- Optionからのコピーなし
- 所有権の移動のみ

---

## パターン10: エラー処理でのムーブ

### ✅ Cm方式：Result型でムーブ

```cm
fn read_file(path: String) -> Result<String, Error> {
    // ファイル読み込み
    if success {
        return Ok(move contents);  // ムーブ
    } else {
        return Err(error);
    }
}

// ムーブで結果を受け取る
match read_file("data.txt") {
    Ok(data) => println("{data}"),
    Err(e) => println("Error: {e}"),
}
```

**効率化の理由:**
- 成功時も失敗時もムーブのみ
- コピー一切なし

---

## 比較：Copy vs Move vs Borrow

### メモリ効率の比較

```cm
// 前提：1MB のデータ
struct LargeData {
    int[250000] array;  // 1MB
}

// ❌ コピー（C++的）: 1MB × コピー回数
void process_copy(LargeData data) {  // 1MBコピー
    // 処理
}  // 1MB破棄

// ✅ ムーブ（Cm）: ポインタのみ（8バイト）
void process_move(LargeData data) {  // 8バイトのムーブ
    // 処理
}  // 破棄

// ✅ 借用（Cm）: ポインタのみ（8バイト）、破棄なし
void process_borrow(LargeData& data) {  // 8バイトの参照
    // 処理
}  // 破棄なし
```

### パフォーマンス比較表

| 操作 | メモリコピー | オーバーヘッド | 元データ | 速度 |
|------|-------------|---------------|---------|------|
| **Copy（C++）** | データ全体 | 大 | 有効 | ⭐ |
| **Move（Cm）** | ポインタのみ | 小 | 無効 | ⭐⭐⭐⭐ |
| **Borrow（Cm）** | なし | 最小 | 有効 | ⭐⭐⭐⭐⭐ |

---

## まとめ：コピーコンストラクタ不要の哲学

### Cmの効率化戦略

1. **デフォルトは借用** - 読み取りのみなら借用
2. **必要ならムーブ** - 所有権が必要ならムーブ
3. **コピーは最小限** - プリミティブ型のみ
4. **明示的な制御** - `move`で意図を明確に

### 効率的なコードの指針

```cm
// 基本方針
// 1. 読み取り → 借用（最速）
fn read(data: T&) { }

// 2. 変更 → 借用（最速）
fn modify(data: T&) { }

// 3. 消費 → ムーブ（速い）
fn consume(data: T) { }
consume(move data);

// 4. 生成 → ムーブで返す（速い）
fn create() -> T { return data; }

// 5. プリミティブ → 値渡し（Copy型）
fn add(x: int, y: int) -> int { }
```

### パフォーマンスのベストプラクティス

```cm
// ✅ 良い例：借用とムーブの組み合わせ
fn process_pipeline() {
    Vec<int> data = create_data();       // ムーブで生成
    
    validate(data);                      // 借用で検証
    transform(data);                     // 借用で変換
    
    store(move data);                    // ムーブで消費
}

// validate, transform は借用なので高速
// 最後だけムーブで所有権移動
```

---

**Cmでは、コピーコンストラクタなしでも、ムーブと借用だけで効率的なコードが書けます！**
