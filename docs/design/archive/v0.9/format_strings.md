[English](format_strings.en.html)

# Format Strings Design

## 概要

Cm言語のprintln関数は、Rust風のフォーマット文字列をサポートします。

## フォーマット構文

### 基本的なプレースホルダー

1. **位置引数**: `{}`
   ```cm
   println("Hello, {}!", "world");  // "Hello, world!"
   println("{} + {} = {}", 1, 2, 3);  // "1 + 2 = 3"
   ```

2. **名前付き引数**: `{name}`
   ```cm
   int x = 42;
   println("The answer is {x}");  // "The answer is 42"
   ```

3. **インデックス指定**: `{0}`, `{1}` など
   ```cm
   println("{1} {0}", "world", "Hello");  // "Hello world"
   ```

### フォーマット指定子

構文: `{[arg]:[spec]}`

**数値フォーマット**:
- `:b` - 2進数
- `:o` - 8進数
- `:x` - 16進数（小文字）
- `:X` - 16進数（大文字）
- `:e` - 指数表記（小文字）
- `:E` - 指数表記（大文字）

**幅と精度**:
- `{:5}` - 最小幅5文字
- `{:.2}` - 小数点以下2桁
- `{:5.2}` - 幅5文字、小数点以下2桁

**アライメント**:
- `{:<10}` - 左寄せ
- `{:>10}` - 右寄せ
- `{:^10}` - 中央寄せ
- `{:0>5}` - ゼロパディング

### エスケープ

- `{% raw %}{{` → `{`{% endraw %}
- `{% raw %}}}` → `}`{% endraw %}

## 実装アプローチ

### Phase 1: 基本実装
1. 位置引数 `{}` のサポート
2. すべての数値型の対応
3. 文字列と数値の混在

### Phase 2: 拡張機能
1. 名前付き引数
2. フォーマット指定子
3. アライメント・パディング

### Phase 3: 最適化
1. コンパイル時フォーマット検証
2. 効率的な文字列構築

## 型システムとの統合

printlnは可変長引数を受け取る特別な関数として扱われます：

```cm
// std/io.cm
module std::io;

// 可変長引数を受け取る
export void println(string format, ...);
```

型チェッカーでは、printlnを特別扱いし：
- 第1引数は必ずstring型
- 残りの引数は任意の型を許可
- フォーマット文字列のプレースホルダー数と引数の数の一致を検証（可能な場合）

## MIRでの表現

MIRレベルでは、println呼び出しは以下のように変換されます：

```mir
// println("x = {}, y = {}", x, y);
// ↓
_tmp0 = format_string("x = {}, y = {}", x, y);
call __builtin_println(_tmp0);
```

## インタプリタでの実装

MIRインタプリタは、フォーマット処理を内部で実装します：

```cpp
// フォーマット処理の擬似コード
std::string format_string(const std::string& fmt, const std::vector<Value>& args) {
    std::string result;
    size_t arg_index = 0;

    for (size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] == '{' && i + 1 < fmt.size()) {
            if (fmt[i + 1] == '{') {
                // エスケープされた '{'
                result += '{';
                i++;
            } else if (fmt[i + 1] == '}') {
                // 位置引数
                if (arg_index < args.size()) {
                    result += to_string(args[arg_index++]);
                }
                i++;
            } else {
                // 名前付き引数またはフォーマット指定子
                // TODO: 実装
            }
        } else if (fmt[i] == '}' && i + 1 < fmt.size() && fmt[i + 1] == '}') {
            // エスケープされた '}'
            result += '}';
            i++;
        } else {
            result += fmt[i];
        }
    }

    return result;
}
```

## テストケース

```cm
// tests/test_programs/p1_formatting/p1_basic_format.cm
import std::io::println;

int main() {
    // 基本的な位置引数
    println("Hello, {}!", "World");

    // 複数の引数
    println("{} + {} = {}", 10, 20, 30);

    // 異なる型の混在
    println("Name: {}, Age: {}, Height: {}", "Alice", 25, 175.5);

    // 数値フォーマット
    int n = 255;
    println("Dec: {}, Hex: {:x}, Bin: {:b}", n, n, n);

    return 0;
}
```

期待される出力:
```
Hello, World!
10 + 20 = 30
Name: Alice, Age: 25, Height: 175.5
Dec: 255, Hex: ff, Bin: 11111111
```