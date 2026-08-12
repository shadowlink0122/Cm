---
title: StringBuilder
---

# std::strings::builder - StringBuilder（可変文字列バッファ）

`StringBuilder` は文字列を償却O(1)で追記できる可変バッファです。
ループ内の `s = s + "..."` は毎回全体をコピーするためO(n²)になりますが、`StringBuilder` はO(n)で同じ結果を構築できます。

> **対応バックエンド:** JIT / Native / WASM / JS / TS（SVは対象外）

---

## 基本的な使い方

```cm
import std::strings::StringBuilder;
import std::io::println;

int main() {
    StringBuilder sb();      // コンストラクタ呼び出し
    sb.append("hello");
    sb.append(", ");
    sb.append("world");
    println(sb.to_string()); // hello, world
    println(sb.len());       // 12（現在のバイト長、O(1)）
    return 0;
}
```

## ループでの構築（O(n²)の回避）

```cm
import std::strings::StringBuilder;
import std::io::println;

int main() {
    StringBuilder sb();
    int i = 0;
    while (i < 50000) {
        sb.append("{i},");   // 補間で数値も追記できる
        i++;
    }
    string result = sb.to_string();
    println(result.len());
    return 0;
}
```

同じ処理を `s = s + "{i},"` で書くとNに対して二次時間になります。
大量の追記には `StringBuilder` を使ってください。

## API

| メソッド | 説明 |
|---------|------|
| `self()` | 空のバッファを作成 |
| `void append(string s)` | 末尾へ追記（償却O(1)） |
| `string to_string()` | 現在の内容を新しい文字列で返す（builderは継続使用可能） |
| `long len()` | 現在のバイト長（O(1)） |
| `void clear()` | 内容を空にする（内部容量は維持） |

バッファはスコープ終了時にデストラクタで自動解放されます。
`to_string()` が返す文字列は呼び出し側が所有します。
