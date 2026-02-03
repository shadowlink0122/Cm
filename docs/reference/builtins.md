# Cm ビルトイン関数・演算子リファレンス

## 型情報

### `typeof(expr)` - 型名取得
式の型名を文字列で取得します。

```cm
int x = 42;
string t = typeof(x);  // "int"

struct Point { int x; int y; }
Point p;
string tp = typeof(p); // "Point"
```

### `__sizeof__(T)` / `sizeof(T)` - 型サイズ取得
型のバイトサイズを取得します。

```cm
int s_int = __sizeof__(int);      // 4
int s_double = __sizeof__(double); // 8
int s_Point = __sizeof__(Point);   // 8 (4 + 4)

// ジェネリクスでも使用可能
<T> T* allocate() {
    return malloc(__sizeof__(T) as long) as T*;
}
```

## 配列・スライス

### `arr.len` / `slice.len` - 長さ取得
配列またはスライスの要素数を取得。

```cm
int[5] arr;
int len = arr.len;  // 5

int[] slice = arr[1:3];
int slen = slice.len;  // 2
```

### `slice.cap` - 容量取得
スライスの容量を取得。

```cm
int[] slice;
int cap = slice.cap;
```

### `slice.push(value)` - 末尾追加
スライスに要素を追加。

```cm
int[] nums;
nums.push(10);
nums.push(20);
```

### `slice.pop()` - 末尾削除
スライスから末尾要素を削除して返す。

```cm
int last = nums.pop();
```

## 文字列

### `str.len` - 文字列長
```cm
string s = "hello";
int len = s.len;  // 5
```

### `str.charAt(index)` - 文字取得
```cm
char c = s.charAt(0);  // 'h'
```

### `str.substring(start, end)` - 部分文字列
```cm
string sub = s.substring(0, 3);  // "hel"
```

### `str.indexOf(search)` - 検索
```cm
int idx = s.indexOf("ll");  // 2
```

### `str.toUpperCase()` / `str.toLowerCase()` - 大文字小文字変換
```cm
string upper = s.toUpperCase();  // "HELLO"
string lower = s.toLowerCase();  // "hello"
```

### `str.trim()` - 空白除去
```cm
string trimmed = "  hello  ".trim();  // "hello"
```

### `str.startsWith(prefix)` / `str.endsWith(suffix)` - 接頭辞・接尾辞チェック
```cm
bool starts = s.startsWith("he");  // true
bool ends = s.endsWith("lo");      // true
```

### `str.includes(search)` - 含有チェック
```cm
bool has = s.includes("ell");  // true
```

### `str.repeat(count)` - 繰り返し
```cm
string repeated = "ab".repeat(3);  // "ababab"
```

### `str.replace(old, new)` - 置換
```cm
string replaced = s.replace("l", "L");  // "heLLo"
```

### `str.slice(start, end)` - スライス（配列風）
```cm
string sliced = s.slice(1, 4);  // "ell"
```

### `str.first` / `str.last` - 最初・最後の文字
```cm
char first = s.first;  // 'h'
char last = s.last;    // 'o'
```

## 配列操作

### `arr.forEach(fn)` - 各要素に関数適用
```cm
int[3] nums = [1, 2, 3];
nums.forEach(|n| => println("{n}"));
```

### `arr.map(fn)` - 変換
```cm
int[3] doubled = nums.map(|n| => n * 2);  // [2, 4, 6]
```

### `arr.filter(pred)` - フィルタ
```cm
int[] evens = nums.filter(|n| => n % 2 == 0);
```

### `arr.reduce(init, fn)` - 畳み込み
```cm
int sum = nums.reduce(0, |acc, n| => acc + n);
```

### `arr.some(pred)` / `arr.every(pred)` - 条件チェック
```cm
bool hasEven = nums.some(|n| => n % 2 == 0);
bool allPositive = nums.every(|n| => n > 0);
```

### `arr.indexOf(value)` - 検索
```cm
int idx = nums.indexOf(2);  // 1
```

### `arr.includes(value)` - 含有チェック
```cm
bool has = nums.includes(2);  // true
```

### `arr.findIndex(pred)` - 条件で検索
```cm
int idx = nums.findIndex(|n| => n > 1);  // 1
```

## メモリ

### FFI経由 (`use libc`)
```cm
use libc {
    void* malloc(long size);
    void* realloc(void* ptr, long size);
    void free(void* ptr);
    void* memcpy(void* dest, void* src, long n);
    void* memset(void* ptr, int value, long n);
}
```

### 使用例
```cm
// 型サイズを使ったメモリ確保
int* arr = malloc(__sizeof__(int) * 10 as long) as int*;
// 解放
free(arr as void*);
```

## キャスト

### `as T` - 型変換
```cm
int i = 42;
long l = i as long;      // int -> long
double d = i as double;  // int -> double
int* p = malloc(4 as long) as int*;  // void* -> int*
```

## デバッグ

### `println(msg)` / `print(msg)` - 出力
文字列補間対応。

```cm
int x = 42;
println("x = {x}");  // "x = 42"
```
