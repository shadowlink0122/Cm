---
title: std::json
---

[English](../../en/stdlib/json.html)

# std::json — JSONパーサ

JSON文字列のパース・アクセス・直列化を純粋なCmで実装したモジュールです（v0.17.0で追加）。
JSONツリーをノード配列＋インデックスのアリーナで表現し、再帰型やポインタを使わないため全バックエンドで同一動作します。

> **対応バックエンド:** JIT / Native / WASM / JS / TS

---

## 基本的な使い方

```cm
import std::json::*;
import std::io::println;

int main() {
    string text = "{\"name\": \"cm\", \"version\": 17, \"tags\": [\"fast\", \"typed\"]}";

    int root = json_parse(text);
    if (root < 0) {
        println("parse error");
        return 1;
    }

    // オブジェクトのフィールドアクセス（戻り値はノードindex、不在は負値）
    int name = json_object_get(root, "name");
    println("name: {json_string(name)}");        // cm

    int ver = json_object_get(root, "version");
    println("version: {json_int(ver)}");         // 17

    // 配列の列挙
    int tags = json_object_get(root, "tags");
    for (int i = 0; i < json_array_len(tags); i++) {
        int tag = json_array_get(tags, i);
        println("tag[{i}]: {json_string(tag)}");
    }

    // コンパクト直列化
    println(json_stringify(root));
    return 0;
}
```

---

## API一覧

| 関数 | 戻り値 | 説明 |
|------|--------|------|
| `json_parse(text)` | `int` | パースしてルートノードindexを返す（失敗・容量超過は-1） |
| `json_kind(node)` | `JsonKind` | ノード種別（Null/Bool/Number/String/Array/Object） |
| `json_is_null(node)` | `bool` | nullか |
| `json_bool(node)` | `bool` | 真偽値を取得 |
| `json_number(node)` | `double` | 数値を取得 |
| `json_int(node)` | `int` | 数値をintで取得 |
| `json_string(node)` | `string` | 文字列を取得 |
| `json_array_len(node)` | `int` | 配列の要素数 |
| `json_array_get(node, i)` | `int` | 配列要素のノードindex |
| `json_object_get(node, key)` | `int` | オブジェクトフィールドのノードindex（不在は負値） |
| `json_object_has(node, key)` | `bool` | キーが存在するか |
| `json_stringify(node)` | `string` | コンパクトなJSON文字列へ直列化 |

---

## 対応する構文

- 文字列エスケープ: `\" \\ \/ \n \t \r \b \f`
- 数値: 符号・小数・指数表記
- ネストしたオブジェクト・配列、空白

## 注意事項

- ノード容量は既定1024（グローバルなノード配列の固定長）。超えると`json_parse`が-1を返します。必要ならソースの容量定数を拡張してください
- ノードは`int`のindexで参照します。`json_object_get`の不在キーは負値を返すため、アクセス前に`json_object_has`か負値チェックを行ってください

---

**関連:** [文字列の長さ](strings/length.html) · [StringBuilder](strings/builder.html)

---

<!-- nav -->
← 前: [TreeSet / HashSet - 集合](collections/sets.html) ｜ [目次](index.html) ｜ 次: [標準ライブラリの拡張方法](extending.html) →
