---
title: Testing
parent: Advanced Features
---

[日本語](../../ja/advanced/testing.html)

# Testing with `#[test]`

**Goal:** Write unit tests as `#[test]` functions and run them with `cm test`.
**Level:** 🟢 Beginner

---

## Writing a test

Mark a function with the `#[test]` attribute and assert inside it with `assert(cond, message)` from `std::debug`.

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

Run it:

```bash
cm test mytests.cm
# [PASS] adds_positive_numbers
# [PASS] adds_negative_numbers
# ✓ 2 test(s) passed
```

`cm test` runs each `#[test]` function in isolation (JIT). A failing `assert` prints `assertion failed: <message>` and exits non-zero. Test-only helpers can be guarded with `#ifdef TEST ... #end` so they exist only under `cm test`.

---

## Testing a library

Library tests live **next to the library**, in a `*_test.cm` file in the same directory — they test the library, not the compiler, so they ship with it.

```
libs/web/html.cm         # the library
libs/web/html_test.cm    # its tests (#[test] functions)
```

Import the library and assert on its behavior:

```cm
// libs/web/html_test.cm
import std::debug::assert;
import web::html::*;

#[test]
void escapes_text() {
    assert(span().add(text("a<b>")).render() == "<span>a&lt;b&gt;</span>", "escaping");
}
```

Run every library's tests with the runner in `tests/`:

```bash
make test-libs
# discovers libs/**/*_test.cm and runs each with `cm test`
# [PASS] libs/web/html_test.cm (9 tests)
# [PASS] libs/std/json/mod_test.cm (7 tests)
```

`make test-libs` is part of `make test`.

---

<!-- nav -->
← Prev: [Advanced - Macros](macros.html) | [Contents](../index.html) | Next: [Advanced - JSON](json.html) →
