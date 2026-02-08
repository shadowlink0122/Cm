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
```

## Check Items

### 1. Naming Conventions

Cm recommends the following naming conventions:

| Target | Convention | Example |
|--------|------------|---------|
| Variables/Functions | snake_case | `my_variable`, `calc_sum` |
| Types/Structs | PascalCase | `MyStruct`, `HttpClient` |
| Constants | SCREAMING_SNAKE_CASE | `MAX_SIZE`, `PI` |

```cm
// ⚠️ Warning: variable names should use snake_case
int myVariable = 10;  // → my_variable

// ✅ OK
int my_variable = 10;
```

### 2. Unused Variables

Detects variables that are not used:

```cm
int main() {
    int x = 10;  // ⚠️ Warning: variable 'x' is unused
    return 0;
}
```

### 3. const Suggestions

Suggests `const` for variables that are not modified:

```cm
int main() {
    int size = 100;  // ⚠️ Warning: 'size' can be const
    println("Size: {size}");
    return 0;
}
```

## Configuration File (.cmlint.yml)

Place `.cmlint.yml` in project root to configure rules:

```yaml
# Enable naming convention checks
naming:
  enabled: true
  variables: snake_case
  types: PascalCase

# Enable unused variable checks
unused:
  enabled: true
  warn_unused_params: false  # Don't warn about unused parameters

# Disable const suggestions
const_suggestion:
  enabled: false
```

## Output Example

```
warning: variable 'myValue' should use snake_case naming
  --> src/main.cm:5:9
  |
5 |     int myValue = 10;
  |         ^^^^^^^
  |
  = help: consider renaming to 'my_value'

warning: unused variable 'x'
  --> src/main.cm:8:9
  |
8 |     int x = 42;
  |         ^
  |
```

## Related Topics

- [Formatter](formatter.md) - Code formatter

---

**Last Updated:** 2026-02-08
