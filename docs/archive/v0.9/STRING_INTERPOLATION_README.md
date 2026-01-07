[English](STRING_INTERPOLATION_README.en.html)

# String Interpolation and Format Strings - Complete Documentation

## Summary

This directory contains comprehensive documentation on how string interpolation and format strings are handled in the Cm language LLVM backend. The documentation has been organized into three main documents plus this README.

## Documentation Structure

### 1. STRING_INTERPOLATION_LLVM.md (14KB)
**Detailed technical overview of all four compilation layers**

Contains:
- HIR-level string interpolation processing (`StringInterpolationProcessor` class)
- Format string parser and formatter (`FormatStringParser`, `FormatStringFormatter`)
- LLVM code generation for print/println functions
- String concatenation handling in binary Add operation
- Runtime function signatures and declarations
- Standard library bindings (io/mod.cm, io_ffi.cm)
- Complete data flow example with actual code transformation

Key Sections:
- Layer 1: HIR String Interpolation Processing
- Layer 2: Format String Parser
- Layer 3: LLVM Code Generation
- Layer 4: LLVM Context Setup
- Data Flow Example

**Best for**: Understanding the complete pipeline from source to LLVM IR

---

### 2. STRING_INTERPOLATION_ARCHITECTURE.md (20KB)
**Visual diagrams and architecture overview**

Contains:
- Full compilation pipeline with visual ASCII diagrams
- Key decision points flowcharts
- Runtime function signatures (C API)
- Format specification enumeration
- Memory management lifecycle
- Type conversion matrix
- File organization structure
- Integration relationship diagrams

Key Sections:
- Compilation Pipeline (detailed flow)
- Key Decision Points (binary Add, function call)
- Runtime Function Signatures
- Memory Management (literals, formatted strings, concatenated strings)
- Type Conversion Matrix
- File Organization
- Integration Diagram

**Best for**: Understanding system architecture, decision flow, and memory management

---

### 3. STRING_INTERPOLATION_EXAMPLES.md (16KB)
**Concrete code examples and reference guide**

Contains:
- 5 detailed examples with step-by-step transformations
  1. Simple string print (no interpolation)
  2. Integer interpolation with default format
  3. Hex format specifier
  4. Multiple arguments with chaining
  5. Float formatting with precision
- Code location references with actual source code snippets
- Key function implementations from HIR and LLVM backends
- Testing examples and expected outputs
- Performance considerations and optimization opportunities

Key Sections:
- Example 1-5: Complete transformations from Cm to LLVM IR
- Code Location References
  - HIR String Interpolation Processing
  - LLVM Code Generation
  - Format String Parser
- Testing Examples
- Performance Considerations

**Best for**: Learning by example, finding specific code locations, testing reference

---

## Quick Navigation

### Finding Information About...

#### How String Interpolation is Detected
- See: STRING_INTERPOLATION_LLVM.md → Layer 1 → Interpolation Detection
- Code: `/Users/shadowlink/Documents/git/Cm/src/hir/string_interpolation.hpp:23-39`

#### How Placeholders are Parsed
- See: STRING_INTERPOLATION_LLVM.md → Layer 2 → Parsing Support
- Code: `/Users/shadowlink/Documents/git/Cm/src/common/format_string.hpp:63-120`

#### How println Calls are Code Generated
- See: STRING_INTERPOLATION_LLVM.md → Layer 3 → String Output Functions
- Code: `/Users/shadowlink/Documents/git/Cm/src/codegen/llvm/mir_to_llvm.cpp:298-369`

#### How String Concatenation Works
- See: STRING_INTERPOLATION_LLVM.md → Layer 3 → String Concatenation
- Code: `/Users/shadowlink/Documents/git/Cm/src/codegen/llvm/mir_to_llvm.cpp:556-615`

#### Runtime Function Declarations
- See: STRING_INTERPOLATION_LLVM.md → Layer 4
- Code: `/Users/shadowlink/Documents/git/Cm/src/codegen/llvm/context.cpp:135-215`

#### Memory Management Details
- See: STRING_INTERPOLATION_ARCHITECTURE.md → Memory Management
- Shows: Literals, formatted strings, concatenated strings lifetimes

#### Type Conversion Rules
- See: STRING_INTERPOLATION_ARCHITECTURE.md → Type Conversion Matrix
- Shows: Which runtime function used for each type

#### Complete Example with Output
- See: STRING_INTERPOLATION_EXAMPLES.md → Example 2 (Integer Interpolation)
- Includes: Cm code, HIR, MIR, LLVM IR, and expected runtime behavior

---

## File Reference Map

```
Source Code Files:
├── /Users/shadowlink/Documents/git/Cm/src/hir/string_interpolation.hpp
│   ├── StringInterpolationProcessor class
│   ├── hasInterpolation()
│   ├── extractInterpolations()
│   ├── splitInterpolatedString()
│   └── createInterpolatedStringExpr()
│
├── /Users/shadowlink/Documents/git/Cm/src/common/format_string.hpp
│   ├── FormatStringParser class
│   ├── FormatStringFormatter class
│   ├── Placeholder struct
│   └── FormatSpec enum
│
├── /Users/shadowlink/Documents/git/Cm/src/codegen/llvm/mir_to_llvm.cpp
│   ├── convertTerminator() - print/println special handling (298-369)
│   ├── convertBinaryOp() - string concatenation (556-615)
│   ├── convertConstant() - string literals (540-548)
│   └── declareExternalFunction() - printf (717-747)
│
├── /Users/shadowlink/Documents/git/Cm/src/codegen/llvm/context.cpp
│   ├── declareRuntimeFunctions() (135-154)
│   └── setupStd() (201-215)
│
└── /Users/shadowlink/Documents/git/Cm/std/
    ├── io/mod.cm - Wrapper functions
    └── io_ffi.cm - FFI declarations
```

---

## Key Concepts

### String Interpolation vs Format Strings
- **Interpolation**: Variable substitution in string literals `"{x}"`
- **Format Strings**: Specifying how values are displayed `"{x:x}"` for hex
- **Combined**: `println("Hex: {num:x}", num);`

### Compilation Pipeline
1. **Parse**: String literal detected by parser
2. **HIR**: StringInterpolationProcessor detects and processes
3. **MIR**: Lowered to binary operations (Add for concatenation)
4. **LLVM**: Code generation with runtime function calls

### Runtime Execution
```
Cm Source Code
    ↓
Interpolated String Expression
    ↓
String Concatenation Calls (chained)
    ↓
Runtime Format Functions (cm_format_int, etc.)
    ↓
Runtime Concat Function (cm_string_concat)
    ↓
Print Function (cm_println_string)
    ↓
Output
```

### Memory Management
- **String Literals**: Global constants (static lifetime)
- **Formatted Strings**: Heap-allocated (caller must free)
- **Concatenated Strings**: Heap-allocated (caller must free)

---

## Common Patterns

### Pattern 1: Simple Interpolation
```cm
let x = 42;
println("Value: {}", x);
```
**Compilation**: `"Value: " + toString(x)` → `cm_string_concat` + `cm_println_string`

### Pattern 2: Format Specifier
```cm
let n = 255;
println("Hex: {:x}", n);
```
**Compilation**: `"Hex: " + formatHex(n)` → `cm_format_int` + `cm_string_concat` + `cm_println_string`

### Pattern 3: Multiple Values
```cm
let x = 10, y = 20;
println("({}, {})", x, y);
```
**Compilation**: Chained concatenation of 5 parts with 4 string concat calls

### Pattern 4: Type Conversion
```cm
let f = 3.14;
println("Pi: {}", f);
```
**Compilation**: Float → `cm_format_double` → String → `cm_string_concat` → `cm_println_string`

---

## Implementation Details

### Decision Tree for String Operations

**Is it a Binary Add?**
- Yes: Check if either operand is pointer (string)
  - Yes: String concatenation path → `cm_string_concat`
  - No: Numeric addition → `CreateAdd` or `CreateFAdd`
- No: Continue

**Is it print/println?**
- Yes: Check argument type
  - Pointer: `cm_print_string` or `cm_println_string`
  - i8: `cm_print_bool`
  - i32/i64: `cm_print_int` or `cm_println_int`
  - float/double: `cm_print_double` or `cm_println_double`
- No: Normal function call

---

## Performance Notes

### Current Approach
- String literals: Compiled to global constants (efficient)
- Interpolation: Converted to concatenation chains
- Memory: Each concatenation allocates new string on heap
- Overhead: Multiple malloc/free cycles for chained operations

### Optimization Opportunities
1. **StringBuilder Pattern**: Accumulate in buffer, single allocation
2. **Constant Folding**: Compile-time concatenation of literals
3. **Inline Formatting**: Small format functions inlined
4. **Memory Pooling**: Pre-allocated buffers for common sizes

See: STRING_INTERPOLATION_EXAMPLES.md → Performance Considerations

---

## Related Standards and Specifications

### Format Spec Syntax
Based on Rust-like format syntax:
- `{:b}` - Binary
- `{:o}` - Octal
- `{:x}` - Hex lowercase
- `{:X}` - Hex uppercase
- `{:e}` - Exponential
- `{:E}` - Exponential uppercase
- `{:width}` - Minimum field width
- `{:.precision}` - Decimal precision
- `{:<}`, `{:>}`, `{:^}` - Alignment

### Placeholder Syntax
- `{}` - Positional argument (auto-indexed)
- `{0}`, `{1}` - Positional argument (explicit index)
- `{name}` - Named argument (partial support)
- `{:spec}` - Format specification
- `{{` - Escaped opening brace
- `}}` - Escaped closing brace

---

## Testing

To verify string interpolation behavior:

```bash
# Build the project
cmake -B build && cmake --build build

# Run Cm regression tests
./build/bin/cm tests/regression/*.cm

# Look for test files related to strings
grep -r "println.*{" tests/
```

Example test patterns in documentation:
- Basic interpolation
- Hex format
- Float with precision
- Mixed types
- Escaped braces

---

## Contributing

When modifying string interpolation handling:

1. **HIR Changes**: Update `src/hir/string_interpolation.hpp`
   - Keep escape handling consistent
   - Test with `splitInterpolatedString`

2. **Format String Changes**: Update `src/common/format_string.hpp`
   - Add new format specs to `FormatSpec` enum
   - Add parsing in `parse_format_spec()`
   - Update formatter in `format_value()`

3. **LLVM Changes**: Update `src/codegen/llvm/mir_to_llvm.cpp`
   - Modify `convertBinaryOp()` for concatenation
   - Modify `convertTerminator()` for print functions
   - Test with concrete Cm examples

4. **Runtime Changes**: Update `std/io/mod.cm`
   - Add wrapper functions
   - Document parameters
   - Handle edge cases (null, empty strings)

---

## Additional Resources

### Internal Documents
- `/Users/shadowlink/Documents/git/Cm/docs/design/CANONICAL_SPEC.md` - Language specification
- `/Users/shadowlink/Documents/git/Cm/docs/design/architecture.md` - System architecture
- `/Users/shadowlink/Documents/git/Cm/docs/PROJECT_STRUCTURE.md` - Project organization

### External References
- LLVM Documentation: https://llvm.org/docs/
- Rust Format Syntax: https://doc.rust-lang.org/std/fmt/

---

## Document Metadata

| Document | Size | Sections | Focus |
|----------|------|----------|-------|
| STRING_INTERPOLATION_LLVM.md | 14KB | 8 | Technical layers |
| STRING_INTERPOLATION_ARCHITECTURE.md | 20KB | 10 | Visual/diagrams |
| STRING_INTERPOLATION_EXAMPLES.md | 16KB | 7 | Code examples |
| **Total** | **50KB** | **25** | **Complete** |

Generated: 2025-12-10
Last Updated: 2025-12-10

---

## Questions?

For specific questions about:
- **How it works**: See STRING_INTERPOLATION_LLVM.md
- **Architecture**: See STRING_INTERPOLATION_ARCHITECTURE.md
- **Examples**: See STRING_INTERPOLATION_EXAMPLES.md
