---
title: テスト
parent: Advanced Features
---

[English](../../en/advanced/testing.html)

# `#[test]` によるテスト

**ゴール:** `#[test]` 関数として単体テストを書き、`cm test` で実行する。
**レベル:** 🟢 初級

---

## テストを書く

関数に `#[test]` 属性を付け、`std::debug` の `assert(cond, message)` で検証する。
属性名のタイポ（`#[tset]` 等の未知属性）はコンパイル警告（`--strict` でエラー）になるため、タイポでテストが黙ってスキップされることはない（v0.17.0）。

```cm
import std::debug::assert;

int add(int a, int b) { return a + b; }

#[test]
void adds_positive_numbers() {
    assert(add(2, 3) == 5, "2 + 3");
}

#[test]
void adds_negative_numbers() {
    assert(add(-1, -1) == -2, "-1 + -1");
}
```

実行:

```bash
cm test mytests.cm
# [PASS] adds_positive_numbers
# [PASS] adds_negative_numbers
# ✓ 2 test(s) passed
```

`cm test` は各 `#[test]` 関数を独立に（JITで）実行する。`assert` が失敗すると `assertion failed: <message>` を表示して非0で終了する。テスト専用のヘルパは `#ifdef TEST ... #end` で囲めば `cm test` のときだけ存在する。

---

## ライブラリをテストする

ライブラリのテストは**ライブラリの隣**、同じディレクトリの `*_test.cm` に置く。コンパイラではなくライブラリ自体のテストなので、ライブラリと同梱する。

```
libs/web/html.cm         # ライブラリ本体
libs/web/html_test.cm    # そのテスト（#[test] 関数）
```

ライブラリをimportして振る舞いを検証する:

```cm
// libs/web/html_test.cm
import std::debug::assert;
import web::html::*;

#[test]
void escapes_text() {
    assert(span().add(text("a<b>")).render() == "<span>a&lt;b&gt;</span>", "escaping");
}
```

`tests/` のランナーで全ライブラリのテストをまとめて実行する:

```bash
make test-libs
# libs/**/*_test.cm を discover して各ファイルを `cm test` で実行
# [PASS] libs/web/html_test.cm (9 tests)
# [PASS] libs/std/json/mod_test.cm (7 tests)
```

`make test-libs` は `make test` にも含まれる。

---

<!-- nav -->
← 前: [応用編 - マクロ](macros.html) ｜ [目次](../index.html) ｜ 次: [応用編 - JSON](json.html) →
