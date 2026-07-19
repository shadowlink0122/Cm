---
title: Linter
parent: Compiler
nav_order: 1
---

# Linter (cm lint)

Cm has a built-in static analysis tool (Linter). It detects code quality issues and provides improvement hints.

## Basic Usage

```bash
# Check a single file
./cm lint src/main.cm

# Recursively check a directory
./cm lint src/

# Enable naming convention checks (L001)
./cm lint --strict src/main.cm
./cm check --strict src/main.cm
```

## Check Items

### 1. Naming Conventions (L001, --strict only)

`cm check --strict` / `cm lint --strict` verify that every declaration name follows the world-standard (C/C++/Rust) naming conventions:

| Declaration | Allowed case | Example |
|--------|------------|---------|
| struct / enum / interface / typedef names | PascalCase | `AdderIo`, `HttpClient` |
| Generic type parameters | PascalCase | `T`, `TKey` |
| Functions / methods | snake_case | `calc_sum` |
| Variables / parameters / fields | snake_case | `total_count` |
| Global constants (const) | UPPER_SNAKE_CASE | `MAX_SIZE`, `CLK_FREQ` |
| Local constants (const) | snake_case / UPPER_SNAKE_CASE | `base`, `MAX_N` |
| Enum variants | PascalCase / UPPER_SNAKE_CASE | `North`, `CTRL_00` |
| Module names | snake_case | `hdmi_out` |

camelCase is not allowed for any declaration. Leading underscores are stripped before checking (`_unused` counts as snake_case).

```cm
// ⚠️ L001: struct name 'bad_struct' does not follow PascalCase
struct bad_struct {
    int badField;  // ⚠️ L001: field names use snake_case
}

// ✅ OK
struct GoodStruct {
    int good_field;
}
```

Exempt from checks (names fixed externally, etc.):

- `extern struct` and its fields (vendor primitives: `OSC`, `TLVDS_D2`, ...)
- Functions inside `extern "C"` blocks (C symbol names)
- `self()` constructors / `~self()` destructors / operator overloads / `main`
- Standard library namespaces inlined by import (`std` / `native`, ...)

### 2. Unused Variables (W001)

Detects variables that are never used:

```cm
int main() {
    int x = 10;  // ⚠️ W001: variable 'x' is never used
    return 0;
}
```

### 3. const Suggestions

Recommends `const` for variables that are never modified:

```cm
int main() {
    int size = 100;  // ⚠️ warning: 'size' can be const
    println("Size: {size}");
    return 0;
}
```

## Configuration File (.cmconfig.yml)

Place `.cmconfig.yml` in the project root (or a parent directory) to configure per-rule levels:

```yaml
lint:
  rules:
    L001: disabled   # disable naming convention checks
    W001: error      # promote unused variables to errors
```

Use `exclude:` to omit paths from directory scans (`-r`), such as test corpora or import-only module files. Explicitly specified files, or directories under an excluded path given directly, are still linted:

```yaml
lint:
  exclude:
    - tests/          # exclude the test corpus from scans
    - libs/std/core/  # import-only module aggregation dialect
```

Line-level disabling comments are also available:

```cm
// @cm-disable-next-line L001
struct legacy_struct {  // L001 is not reported on this line
    int id;
}
```

## Output Example

```
src/main.cm:5:9: warning: 変数名 'myValue' は snake_case 命名規則に従っていません [L001]
src/main.cm:8:9: warning: Variable 'x' is never used [W001]
```

## See Also

- [Formatter](formatter.md) - Code formatter

---

<!-- nav -->
← Prev: [Compiler - Preprocessor (Conditional Compilation)](preprocessor.html) | [Index](../index.html) | Next: [Formatter (cm fmt)](formatter.html) →
