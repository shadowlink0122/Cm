---
title: npmパッケージ連携
parent: Compiler
---

[English](../../../en/compiler/js/npm-interop.html)

# npmパッケージ連携（JS FFI）

**学習目標:** `use "パッケージ名"` でnode_modulesのJS/TSパッケージをCmから利用する方法を学びます。
**所要時間:** 15分
**難易度:** 🟡 中級

---

## 概要

> このページの例は `--target=js` / `--target=ts` の両方で動作します。TSプロジェクトでは `--target=ts` を使うと型注釈付きの出力が得られます。

JSターゲットでは `use "パッケージ名" { 宣言 }` でnpmパッケージやNode組み込みモジュールを利用できます。
生成コードには `const pkg = require("パッケージ名")` が出力され、宣言した関数は `pkg.func(args)` として呼ばれます。

```cm
//! platform: js
use "path" {
    string join(string a, string b);
}

int main() {
    string p = join("foo", "bar");
    println("{p}");  // foo/bar
    return 0;
}
```

---

## 構造体の互換

Cmの構造体はJSオブジェクトとしてそのまま渡り、返却オブジェクトも構造体で受けられます（フィールド名保持・変換不要）。

```cm
struct Point {
    int x;
    int y;
}

use "geometry" {
    Point scale(Point p, int factor);
}
```

---

## コールバック

関数ポインタ・ラムダはJS関数としてそのまま渡せます。

```cm
use "mylib" {
    int applyTwice(int*(int) fn, int v);
}

int triple(int x) {
    return x * 3;
}
// applyTwice(triple, 7) / applyTwice((int x) => x * 2, 5) の両方が動作する
```

---

## JSオブジェクトのメソッド呼び出し（関数型フィールド）

JSのメソッドは「関数値を持つプロパティ」なので、構造体の**関数型フィールド**として宣言するとメソッド構文で呼べます。
生成コードは `obj.method(args)` を直接出力するため、**this束縛が保持されます**。

```cm
struct Greeter {
    string name;
    string*(string) greet;  // 関数型フィールド = JSメソッド
}

use "greeter" {
    Greeter makeGreeter(string name);
}

int main() {
    Greeter g = makeGreeter("Cm");
    string s = g.greet("Hello");  // this.name が参照できる
    println("{s}");
    return 0;
}
```

関数型フィールドはJS連携専用ではなく、native/wasm/jitでも同様に代入・差し替え・呼び出しができます。

---

## テストでのフィクスチャ

`tests/js/ffi/node_modules/` のようにテストディレクトリへローカルパッケージを同梱すると、テストランナーが `NODE_PATH` で解決します（npm install不要）。

---

## 今後の拡張

TypeScript型定義（.d.ts）の出力・Promise連携・React/DOM対応のロードマップは `docs/design/js_interop_roadmap.md` を参照してください。

---

<!-- nav -->
← 前: [コンパイラ編 - JSバックエンド](index.html) ｜ [目次](../index.html) ｜ 次: [コンパイラ編 - SystemVerilogバックエンド](../sv/index.html) →
