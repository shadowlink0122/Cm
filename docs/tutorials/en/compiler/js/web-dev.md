---
title: Web Development in Cm
parent: Compiler
---

[日本語](../../../ja/compiler/js/web-dev.html)

# Web Development in Cm (no vendor JS)

**Goal:** Build a server-rendered web app entirely in Cm — HTML, CSS, routing, and an in-memory store — using only Node's built-in `http`.
**Time:** 20 minutes
**Level:** 🟡 Intermediate

---

## Overview

Cm targets JS/TS as first-class backends, so you can write a whole web app in Cm without any hand-written JS or JSON glue. The only FFI you need is Node's built-in `http` module, and `require("http")` resolves with no `vendor/` files or `npm install`.

The building blocks:

- **HTML** — the `web::html` struct builder (`libs/web/html.cm`)
- **CSS** — `with Css` structs
- **State** — a module-global in-memory store
- **HTTP** — Node's built-in `http` via `use "http" { ... }`

See the full example under `examples/web-fullstack/`.

---

## HTML with the `web::html` builder

Instead of concatenating strings, build a tree of `Html` nodes. Builder methods return a new `Html` (they never mutate `self`), so the output is identical on every backend, and text/attributes are HTML-escaped automatically.

```cm
import web::html::*;

string card(string label) {
    return div().class("card")
        .add(h1().add(text(label)))
        .add(p().add(text("built with Cm")))
        .render();
}
// card("Hi <b>") => <div class="card"><h1>Hi &lt;b&gt;</h1><p>built with Cm</p></div>
```

Shortcuts exist for common tags (`div`, `ul`, `li`, `h1`, `a`, `form`, `button`, `meta`, ...). `text()` escapes its content; `raw()` inserts trusted HTML as-is; `document(root)` prepends `<!doctype html>`.

> Import with `import web::html::*` (wildcard). Avoid naming a local the same as a builder shortcut (e.g. a parameter called `title` collides with `title()`).

---

## CSS with `with Css`

Generate CSS from Cm structs — no CSS strings by hand. Field `background_color` becomes the property `background-color`.

```cm
struct BodyStyle with Css {
    string font_family;
    string max_width;
    string color;
}

string stylesheet() {
    BodyStyle body = { font_family: "system-ui, sans-serif", max_width: "640px", color: "#1f2937" };
    return "body { " + body.to_css() + "}\n";
}
```

---

## An HTTP server on Node's built-in `http`

Declare the shape of Node's `http` objects with structs. Plain fields (`req.method`, `req.url`, `res.statusCode`) map to JS properties; methods (`res.setHeader`, `res.end`, `server.listen`) are **function-typed fields** so `this` is preserved.

```cm
//! platform: ts

struct Req { string method; string url; }
struct Res {
    int statusCode;
    void*(string, string) setHeader;
    void*(string) end;
}
struct Server { void*(int) listen; }

use "http" {
    Server createServer(void*(Req, Res) handler);
}

void handle(Req req, Res res) {
    res.statusCode = 200;
    res.setHeader("Content-Type", "text/html; charset=utf-8");
    res.end(card(req.url));   // render with the HTML builder
}

int main() {
    Server server = createServer(handle);
    server.listen(8137);
    println("listening on http://localhost:8137");
    return 0;
}
```

`createServer(handle)` passes the Cm function `handle` as the JS callback. The server object returned by `createServer` is a foreign JS object; Cm keeps it by reference (it is not deep-copied), so `server.listen(...)` works.

Run it:

```bash
cm compile --target=js server.cm -o server.js
node server.js
curl -s http://localhost:8137/
```

---

## Multi-file structure

Split the app into modules and keep the dependency graph a tree so no module is imported through two paths (which would duplicate its globals). A common pattern is a **facade** module (here `view`) that is the single entry point to the store, styles, and HTML, so the server imports only the facade.

```
server.cm  →  view.cm  →  { store.cm, style.cm, web::html, models.cm }
```

A module can freely use a library that it imports transitively through another module — `import ./view::{page}` in the server pulls in `web::html` used inside `view`.

---

## Next steps

- Swap the in-memory store for a real database via FFI (`use "pg" { ... }`)
- Parse URL parameters and POST bodies for forms
- See `examples/web-fullstack/` for the complete app

<!-- nav -->
← Prev: [npm Package Interop](npm-interop.html) | [Contents](../index.html) | Next: [Compiler - SystemVerilog Backend](../sv/index.html) →
