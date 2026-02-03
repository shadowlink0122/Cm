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
