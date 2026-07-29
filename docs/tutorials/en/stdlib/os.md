---
title: OS Integration (env / process / path / bytes)
---

# OS Integration Modules - std::env / std::process / std::path / std::bytes

OS integration APIs for writing compiler-like CLI tools (self-hosting preparation, v0.17.0).

> **Supported backends:** env / process are Native and JIT only. path / bytes / strings::split are available on all backends (pure Cm)

Namespace-style calls (`env::get(...)`) are not supported yet; use selective imports with aliases.

## std::env - Environment Variables, Command-Line Arguments, Executable Path

```cm
import std::io::println;
import std::env::{get as env_get, set as env_set, args, current_exe};

int main() {
    env_set("MY_VAR", "hello");
    match (env_get("MY_VAR")) {
        Option::Some(v) => println(v),
        Option::None => println("(unset)"),
    }

    // Command-line arguments (the first entry is the executable/script path)
    const string[] argv = args();
    for (long i = 0; i < argv.len(); i++) {
        println("arg[{i}] = {argv[i]}");
    }

    // Absolute path of the running executable
    println(current_exe());
    return 0;
}
```

| Function | Description |
|----------|-------------|
| `Option<string> get(string name)` | Get an environment variable (None if unset) |
| `bool set(string name, string value)` | Set an environment variable (overwrites) |
| `string[] args()` | Command-line arguments (first entry is the executable name; empty when uninitialized) |
| `string current_exe()` | Absolute path of the running executable (empty string on failure) |

With JIT execution (`cm run`), everything after `--` is passed to the script, and the first entry of `args()` is the input file path.

```bash
cm run tool.cm -- input.cm -o out    # args() = ["tool.cm", "input.cm", "-o", "out"]
./tool input.cm -o out               # native binaries receive the OS argv as usual
```

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

## std::fs Extensions - Directory Listing and Binary-Safe I/O

```cm
import std::io::println;
import std::fs::{read_dir, read_bytes, write_bytes};

int main() {
    // List entry names in ascending order ("." and ".." are excluded)
    const string[] entries = read_dir("src");
    for (long i = 0; i < entries.len(); i++) {
        println(entries[i]);
    }

    // Read/write without losing embedded NULs or non-UTF-8 bytes
    match (read_bytes("input.o")) {
        Result::Ok(data) => {
            println("read {data.len()} bytes");
            write_bytes("copy.o", data);
        },
        Result::Err(e) => println(e),
    }
    return 0;
}
```

| Function | Description |
|----------|-------------|
| `string[] read_dir(string path)` | Entry names in the directory (sorted by name; empty if unopenable) |
| `Result<utiny[], string> read_bytes(string path)` | Read the whole file as bytes (Err if missing) |
| `bool write_bytes(string path, utiny[] data)` | Write bytes (length-explicit, so embedded NULs never truncate) |

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
