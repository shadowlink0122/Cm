---
title: npm Package Interop
parent: Compiler
---

[日本語](../../../ja/compiler/js/npm-interop.html)

# npm Package Interop (JS FFI)

**Goal:** Learn how to use JS/TS packages from node_modules in Cm with `use "package"`.
**Time:** 15 minutes
**Level:** 🟡 Intermediate

---

## Overview

> The examples on this page work with both `--target=js` and `--target=ts`. In a TS project, use `--target=ts` to get typed output.

On the JS target, `use "package" { declarations }` gives access to npm packages and Node built-in modules.
The generated code emits `const pkg = require("package")` and declared functions are called as `pkg.func(args)`.

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

## Struct interop

Cm structs are passed as plain JS objects (field names preserved, no conversion), and returned objects can be received as structs.

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

## Callbacks

Function pointers and lambdas are passed as JS functions as-is.

```cm
use "mylib" {
    int applyTwice(int*(int) fn, int v);
}

int triple(int x) {
    return x * 3;
}
// Both applyTwice(triple, 7) and applyTwice((int x) => x * 2, 5) work
```

---

## Calling JS object methods (function-typed fields)

JS methods are function-valued properties, so declare them as **function-typed fields** of a struct and invoke them with method syntax.
The generated code emits `obj.method(args)` directly, so **this-binding is preserved**.

```cm
struct Greeter {
    string name;
    string*(string) greet;  // function-typed field = JS method
}

use "greeter" {
    Greeter makeGreeter(string name);
}

int main() {
    Greeter g = makeGreeter("Cm");
    string s = g.greet("Hello");  // this.name is accessible
    println("{s}");
    return 0;
}
```

Function-typed fields are not JS-specific: assignment, replacement, and calls also work on native/wasm/jit.

---

## Fixtures in tests

Placing a local package under the test directory (like `tests/js/ffi/node_modules/`) lets the test runner resolve it via `NODE_PATH` (no npm install required).

---

## Future work

See `docs/design/js_interop_roadmap.md` for the roadmap covering TypeScript type definitions (.d.ts), Promise interop, and React/DOM support.

---

<!-- nav -->
← Prev: [Compiler - JS Backend](index.html) | [Contents](../index.html) | Next: [Compiler - SystemVerilog Backend](../sv/index.html) →
