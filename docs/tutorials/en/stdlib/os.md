---
title: OS Integration (env / process / path / bytes)
---

# OS Integration Modules - std::env / std::process / std::path / std::bytes

OS integration APIs for writing compiler-like CLI tools (self-hosting preparation, v0.17.0).

> **Supported backends:** env / process are Native and JIT only. path / bytes / strings::split are available on all backends (pure Cm)

Namespace-style calls (`env::get(...)`) are not supported yet; use selective imports with aliases.

## std::env - Environment Variables

```cm
import std::io::println;
import std::env::{get as env_get, set as env_set};

int main() {
    env_set("MY_VAR", "hello");
    match (env_get("MY_VAR")) {
        Option::Some(v) => println(v),
        Option::None => println("(unset)"),
    }
    return 0;
}
```

| Function | Description |
|----------|-------------|
| `Option<string> get(string name)` | Get an environment variable (None if unset) |
| `bool set(string name, string value)` | Set an environment variable (overwrites) |

## std::process - Subprocesses

```cm
import std::io::println;
import std::process::{run, output};

int main() {
    const int code = run("clang --version > /dev/null");  // exit code
    println(code);
    match (output("echo hi")) {                            // collect stdout
        Result::Ok(text) => println(text.len()),
        Result::Err(e) => println(e),
    }
    return 0;
}
```

| Function | Description |
|----------|-------------|
| `int run(string cmd)` | Run via shell and return the exit code (-1 on launch failure) |
| `Result<string, string> output(string cmd)` | Return stdout as a string (Err only on launch failure) |

## std::path - Path Manipulation (pure Cm)

```cm
import std::path::{join, dirname, basename, extension, with_extension};

join("src", "main.cm");            // "src/main.cm"
dirname("src/main.cm");            // "src"
basename("src/main.cm");           // "main.cm"
extension("main.cm");              // "cm"
with_extension("main.cm", "o");    // "main.o"
```

The separator is fixed to `/` (supported platforms are macOS/Linux).

## std::bytes - Endian-Aware Byte Packing (pure Cm)

```cm
import std::bytes::{push_u32_le, read_u32_le};

utiny[] buf = [];
push_u32_le(buf, 0xFEEDFACF);          // append 4 bytes little-endian
const uint magic = read_u32_le(buf, 0);
```

Provides `push_u16_le/u32_le/u64_le`, big-endian variants (`*_be`), and the corresponding `read_*`.
64-bit read/write is not supported on the JS backend due to the 53-bit precision limit.

## std::strings::split / lines - String Splitting (pure Cm)

```cm
import std::strings::{split, lines};

string[] parts = split("a,b,,c", ",");   // ["a", "b", "", "c"] (keeps empty elements)
string[] ls = lines("x\r\ny\n");         // ["x", "y"] (normalizes \r\n, drops trailing empty)
```

When the separator is an empty string, `split` splits into individual code points.

`from_bytes(utiny[])` builds a string from a byte slice (v0.17.0). Byte sequences containing embedded NULs (0x00) are preserved correctly by `byte_len()`, `substring()`, and concatenation.

```cm
import std::strings::from_bytes;

utiny[] raw = [72, 105];          // "Hi"
string s = from_bytes(raw);
```
