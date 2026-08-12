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

## 容量と入力検証

アリーナはスライスにより自動拡張されるため、ノード数（JSON値1つにつき1ノード）の上限はない（v0.17.0。従来の固定1024上限は撤廃）。
入力はJSONとして厳密に検証され、末尾ゴミ（`{"a":1}xyz`）・複数値（`1 2`）・数字を伴わない`-`は `-1` を返す。文字列中の `\uXXXX` はサロゲートペアを含めUTF-8へ実デコードされ、不正なエスケープ（`\x` 等）・不正な16進・対を成さないサロゲートは `-1` を返す。

---

<!-- nav -->
← 前: [応用編 - テスト](testing.html) ｜ [目次](../index.html) ｜ 次: [応用編 - TreeMap](treemap.html) →
