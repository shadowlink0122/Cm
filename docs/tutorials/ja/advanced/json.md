---
title: JSON
parent: Advanced Features
---

[English](../../en/advanced/json.html)

# `std::json` によるJSONパース

**ゴール:** JSONのパース・参照・直列化をCmだけで行う。
**レベル:** 🟡 中級

---

## 概要

`std::json` は純粋なCmで書いたJSONパーサ。（再帰的な）JSONツリーを固定容量のアリーナ（インデックス参照のノード配列）に格納するため、再帰型やポインタを使わず、どのバックエンド（jit/native/wasm/js/ts）でも同じ動作をする。ノードは `int` のindexで参照し、`json_parse` はルートのindex（失敗時 `-1`）を返す。

---

## パースと参照

```cm
import std::json::*;

int main() {
    int root = json_parse("{\"name\":\"Cm\",\"nums\":[1,2,30],\"ok\":true}");
    if (root < 0) { println("parse error"); return 1; }

    int name = json_object_get(root, "name");
    println(json_string(name));                 // Cm

    int nums = json_object_get(root, "nums");
    println(json_array_len(nums));              // 3
    println(json_int(json_array_get(nums, 2))); // 30

    println(json_bool(json_object_get(root, "ok")));  // true
    return 0;
}
```

### API

| 関数 | 意味 |
|------|------|
| `json_parse(text) -> int` | パース。ルートノードindex、失敗で `-1` |
| `json_kind(node) -> JsonKind` | `Null` / `Bool` / `Number` / `String` / `Array` / `Object` |
| `json_bool` / `json_number` / `json_int` / `json_string` | スカラ値の取得 |
| `json_is_null(node)` | JSON null か |
| `json_array_len(node)` / `json_array_get(node, i)` | 配列長 / i番目の要素index（範囲外は `-1`） |
| `json_object_get(node, key)` / `json_object_has(node, key)` | メンバのindex（無ければ `-1`） / 有無 |
| `json_stringify(node) -> string` | コンパクトなJSON文字列へ直列化 |

---

## 直列化

```cm
int root = json_parse("{\"a\":[1,2],\"b\":\"x\"}");
println(json_stringify(root));   // {"a":[1,2],"b":"x"}
```

---

## 容量

アリーナは既定で最大1024ノード（JSON値1つにつき1ノード）。上限を超えると `json_parse` は `-1` を返す。上限はグローバルなノード配列の固定長そのもので、必要ならソースで大きくできる。

---

<!-- nav -->
← 前: [応用編 - テスト](testing.html) ｜ [目次](../index.html) ｜ 次: [型システム編](../types/index.html) →
