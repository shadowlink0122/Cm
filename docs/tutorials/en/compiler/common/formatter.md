---
title: Formatter
parent: Compiler
nav_order: 2
---

# Formatter (cm fmt)

Cm has a built-in code formatter. It helps maintain consistent code style.

## Basic Usage

```bash
# Display formatted result
./cm fmt src/main.cm

# Write to file
./cm fmt -w src/main.cm

# Recursively format a directory
./cm fmt -w src/
```

## Formatting Rules

### 1. Brace Style (K&R Style)

Opening braces are placed on the same line:

```cm
// Before
int main()
{
    if (x > 0)
    {
        return 1;
    }
}

// After
int main() {
    if (x > 0) {
        return 1;
    }
}
```

### 2. Indentation (4 spaces)

Indentation is standardized to 4 spaces:

```cm
// Before
int main() {
  int x = 10;
      int y = 20;
}

// After
int main() {
    int x = 10;
    int y = 20;
}
```

### 2.1 Conditional-compilation block indentation (v0.16.0)

The contents of `#ifdef` / `#ifndef` / `#else` ... `#end` blocks are indented one level. The directives themselves stay at the enclosing indent level, and nesting or combining with braces is supported:

```cm
// Before
#ifdef TEST
#[input] posedge clk;
const uint N = 1;
#end

// After
#ifdef TEST
    #[input] posedge clk;
    const uint N = 1;
#end
```

### 2.2 Continuation-line indentation (v0.16.0)

Continuation lines of a wrapped long expression are indented one level deeper than the first line of the statement. The first wrap adds one level, and subsequent wraps align to the same depth:

```cm
// Before
uint n1 = (a & 1) + ((a >> 1) & 1)
+ ((a >> 2) & 1)
+ ((a >> 3) & 1);

// After
uint n1 = (a & 1) + ((a >> 1) & 1)
    + ((a >> 2) & 1)
    + ((a >> 3) & 1);
```

Continuation lines that carry an unclosed parenthesis follow the parenthesis depth instead:

```cm
out = (q0 | (q1 << 1) | (q2 << 2)
    | (q3 << 3)
    | (1 << 8)) as ushort;
```

### 2.3 Trailing-comment position (v0.16.0)

Manually adjusted trailing-comment positions are respected. The gap between code and comment is widened to two spaces only when it is smaller than that:

```cm
tmds_out = 852;   // manually aligned comments stay as-is
tmds_out = 171;   // (the 3-space adjustment is preserved)
```

### 3. Single-line Block Preservation

Short blocks are kept on a single line:

```cm
// Single-line lambdas are preserved
arr.map(|x| => x * 2);

// Single-line if statements can be preserved
if (x > 0) { return true; }
```

### 4. Empty Line Normalization

Consecutive empty lines are reduced to one:

```cm
// Before
int main() {
    int x = 10;



    return x;
}

// After
int main() {
    int x = 10;

    return x;
}
```

### 5. Binary Operator Spacing (v0.17.0)

Missing spaces are added around unambiguous binary operators (`=`, compound assignments, `==` `!=` `<=` `>=` `&&` `||` `=>` `<<=` `>>=`).
Existing spaces are never removed, so intentional column alignment is preserved.

```cm
// Before
int a=1;
if (a==3&&a<=5) {
    a+=2;
}

// After
int a = 1;
if (a == 3 && a <= 5) {
    a += 2;
}
```

Standalone `+ - * / % < > & ^` are excluded because they are ambiguous with unary operators, generics (`<T>`), and exponent notation (`1e+5`).
`operator` declaration lines keep the declaration style (`operator bool ==(T other)`).

## Usage Example

### Before

```cm
int main(){
int x=10;
  int y   =  20;
if(x>y){
return x;
}else{
return y;
}
}
```

### After

```cm
int main() {
    int x = 10;
    int y = 20;
    if (x > y) {
        return x;
    } else {
        return y;
    }
}
```

## CI Usage

To check code style in CI:

```bash
# Check if formatting is needed (fails if diff exists)
./cm fmt --check src/
```

## Related Topics

- [Linter](linter.md) - Static analysis tool

---

**Last Updated:** 2026-02-08

---

<!-- nav -->← Prev: [Linter (cm lint)](linter.html) | [Contents](../index.html) | Next: [Configuration File (.cmconfig.yml)](config.html) →
