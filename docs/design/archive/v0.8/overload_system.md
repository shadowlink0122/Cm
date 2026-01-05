# Cm言語 オーバーロードシステム設計

## 基本原則

**デフォルトではオーバーロード禁止**。同じ名前の関数が存在する場合はエラー。
`overload` 修飾子を明示的に付けることでオーバーロードを許可。

## overload 修飾子

### 通常関数

```cm
// エラー：オーバーロード禁止（デフォルト）
int add(int a, int b) { return a + b; }
int add(int a, int b, int c) { return a + b + c; }  // エラー！

// OK：overload修飾子で明示的に許可
overload int add(int a, int b) { return a + b; }
overload int add(int a, int b, int c) { return a + b + c; }  // OK
overload double add(double a, double b) { return a + b; }     // OK

// 最初の宣言からoverloadが必要
overload string concat(string a, string b) { return a + b; }
overload string concat(string a, string b, string c) { return a + b + c; }
```

### コンストラクタ

```cm
impl<T> Vec<T> {
    // デフォルトコンストラクタ（overload不要）
    self() {
        this.data = null;
        this.size = 0;
        this.capacity = 0;
    }

    // オーバーロードコンストラクタ（overload必須）
    overload self(size_t initial_cap) {
        this.data = new T[initial_cap];
        this.size = 0;
        this.capacity = initial_cap;
    }

    overload self(T fill_value, size_t count = 10) {
        this.data = new T[count];
        this.size = count;
        this.capacity = count;
        for (size_t i = 0; i < count; i++) {
            this.data[i] = fill_value;
        }
    }

    overload self(const Vec<T>& other) {  // コピーコンストラクタ
        // ...
    }

    overload self(Vec<T>&& other) {  // ムーブコンストラクタ
        // ...
    }

    // デストラクタ（オーバーロード不可）
    ~self() {
        if (this.data != null) {
            delete[] this.data;
        }
    }
}
```

### impl for でのメソッド

```cm
// インターフェース定義
interface Container<T> {
    void push(T item);
    T pop();
    bool is_empty();
}

// impl for での実装
impl<T> Vec<T> for Container<T> {
    // インターフェースメソッドの実装（overload不要）
    void push(T item) {
        if (size >= capacity) {
            grow();
        }
        data[size++] = item;
    }

    T pop() {
        if (size == 0) {
            panic("Pop from empty vector");
        }
        return data[--size];
    }

    bool is_empty() {
        return size == 0;
    }

    // 追加メソッド（オーバーロードする場合）
    overload void push(T item, size_t times) {
        for (size_t i = 0; i < times; i++) {
            push(item);
        }
    }

    // プライベートヘルパー
    private void grow() {
        size_t new_cap = capacity * 2;
        // ...
    }
}
```

## ジェネリック関数とオーバーロード

```cm
// ジェネリック関数もoverloadが必要
<T> T identity(T value) {
    return value;
}

// 特殊化にはoverloadが必要
overload string identity(string value) {
    return "String: " + value;
}

overload int identity(int value) {
    return value * 2;  // 特別な処理
}

// 異なる型パラメータ数
<T> T max(T a, T b) {
    return a > b ? a : b;
}

overload <T> T max(T a, T b, T c) {
    return max(max(a, b), c);
}

overload <T> T max(T a, T b, T c, T d) {
    return max(max(a, b), max(c, d));
}
```

## 演算子オーバーロード

```cm
struct Complex {
    double real;
    double imag;
}

// 演算子は暗黙的にoverload可能
Complex operator+(const Complex& a, const Complex& b) {
    return Complex{a.real + b.real, a.imag + b.imag};
}

// 異なる型との演算
overload Complex operator+(const Complex& a, double b) {
    return Complex{a.real + b, a.imag};
}

overload Complex operator+(double a, const Complex& b) {
    return Complex{a + b.real, b.imag};
}
```

## オーバーロード解決規則

### 優先順位

1. **完全一致**: 型が完全に一致
2. **修飾子変換**: const/volatile の追加
3. **プロモーション**: int→long、float→double
4. **標準変換**: int→double、ポインタ変換
5. **ユーザー定義変換**: コンストラクタ、変換演算子
6. **可変長引数**: 最も低い優先度

### 曖昧性エラー

```cm
overload void func(int a, double b) { }
overload void func(double a, int b) { }

func(1, 2);  // エラー：曖昧（両方とも変換が必要）
func(1, 2.0);  // OK：最初の関数が完全一致
func(1.0, 2);  // OK：2番目の関数が完全一致
```

## エラー例

```cm
// エラー：overloadなしで同名関数
void process(int x) { }
void process(int y) { }  // エラー！同じシグネチャ

// エラー：overloadなしで異なるシグネチャ
void process(int x) { }
void process(double x) { }  // エラー！overloadが必要

// エラー：一部だけoverload
void calculate(int x) { }
overload void calculate(double x) { }  // エラー！最初もoverload必要

// OK：全てにoverload
overload void calculate(int x) { }
overload void calculate(double x) { }  // OK
```

## ベストプラクティス

### 1. 最小限のオーバーロード

```cm
// 良い例：必要最小限
overload <T> void print(T value);
overload void print(const string& str);  // 文字列は特別扱い

// 悪い例：過剰なオーバーロード
overload void process(int x);
overload void process(long x);
overload void process(short x);
overload void process(char x);
// → ジェネリックを使うべき
```

### 2. 明確な意図

```cm
// 良い例：意図が明確
struct File {
    // 異なる開き方を提供
    overload self(const string& path);
    overload self(const string& path, const string& mode);
    overload self(int fd);
}

// 悪い例：紛らわしい
overload int calculate(int x);
overload double calculate(int x);  // 戻り値だけ違う→エラー
```

### 3. デフォルト引数 vs オーバーロード

```cm
// デフォルト引数で十分な場合
void connect(string host, int port = 80) { }

// オーバーロードが適切な場合
overload void write(int value);
overload void write(double value);
overload void write(const string& value);
```

## コンパイラ実装

### 名前マングリング

```
// 元の関数
overload int add(int a, int b);
overload int add(int a, int b, int c);
overload double add(double a, double b);

// マングル後
_Z3addii      // add(int, int)
_Z3addiii     // add(int, int, int)
_Z3adddd      // add(double, double)
```

### シンボルテーブル

```cpp
struct FunctionSymbol {
    string name;
    bool is_overload;  // overload修飾子の有無
    vector<Signature> signatures;  // オーバーロードされたシグネチャ
};

// チェック時
void register_function(FunctionSymbol func) {
    if (exists(func.name)) {
        if (!func.is_overload || !existing.is_overload) {
            error("Function '%s' already defined. Use 'overload' modifier", func.name);
        }
    }
}
```

## まとめ

`overload` 修飾子により：

1. **明示的**: オーバーロードの意図が明確
2. **安全**: 意図しない名前衝突を防止
3. **予測可能**: デフォルトは単一定義
4. **互換性**: C++のオーバーロード解決規則を踏襲