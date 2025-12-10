# String Interpolation Architecture Diagram

## Compilation Pipeline

```
┌─────────────────────────────────────────────────────────────────┐
│  SOURCE CODE                                                     │
│  println("Value: {x:hex}", x);                                  │
└──────────────────┬──────────────────────────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────────────────────────┐
│  LEXER/PARSER (Parser)                                          │
│  Creates AST with string literal node                           │
│  "Value: {x:hex}"                                               │
└──────────────────┬──────────────────────────────────────────────┘
                   │
                   ▼
┌──────────────────────────────────────────────────────────────────┐
│  HIR GENERATION (type_checker.hpp)                              │
│  ▼ StringInterpolationProcessor::hasInterpolation()             │
│    Detects {x:hex} pattern                                      │
│  ▼ StringInterpolationProcessor::splitInterpolatedString()      │
│    ["Value: ", InterpolatedVar{name: "x", format: "hex"}]      │
│  ▼ StringInterpolationProcessor::createInterpolatedStringExpr() │
│    Generates: "Value: " + formatHex(x)                         │
└──────────────────┬──────────────────────────────────────────────┘
                   │
                   ▼
┌──────────────────────────────────────────────────────────────────┐
│  MIR GENERATION (mir_lowering.hpp)                              │
│  Lowers HIR expression to MIR binary operations:                │
│                                                                  │
│  %t0 = const "Value: "  (string literal)                       │
│  %t1 = load x           (load variable)                         │
│  %t2 = call formatHex(%t1)  (format int to hex string)         │
│  %result = add %t0, %t2     (string concatenation)             │
└──────────────────┬──────────────────────────────────────────────┘
                   │
                   ▼
┌──────────────────────────────────────────────────────────────────┐
│  LLVM CODE GENERATION (mir_to_llvm.cpp)                         │
│                                                                  │
│  1. convertOperand() → extract string constant                 │
│  2. convertOperand() → load x as i32                           │
│  3. convertBinaryOp() → detect string concat (Add)             │
│     - Check: lhsType->isPointerTy() ✓                          │
│     - Check: rhsType->isIntegerTy() ✓                          │
│  4. Generate:                                                   │
│     a. %x_str = call cm_format_int(i32 %x)                     │
│     b. %result = call cm_string_concat(ptr %str, ptr %x_str)   │
│     c. call cm_println_string(ptr %result)                     │
└──────────────────┬──────────────────────────────────────────────┘
                   │
                   ▼
┌──────────────────────────────────────────────────────────────────┐
│  LLVM IR (Generated)                                            │
│                                                                  │
│  define i32 @main() {                                           │
│  entry:                                                          │
│    %x_alloca = alloca i32, align 4                             │
│    store i32 42, ptr %x_alloca                                 │
│                                                                  │
│    %x = load i32, ptr %x_alloca                                │
│    %x_str = call ptr @cm_format_int(i32 %x)                    │
│                                                                  │
│    %str_ptr = getelementptr [8 x i8], ptr @.str, i64 0, i64 0  │
│    %result = call ptr @cm_string_concat(ptr %str_ptr, ptr %x_str)
│    call void @cm_println_string(ptr %result)                   │
│                                                                  │
│    ret i32 0                                                    │
│  }                                                              │
│                                                                  │
│  @.str = private unnamed_addr constant [8 x i8] c"Value: \00"  │
│                                                                  │
│  declare ptr @cm_format_int(i32)                               │
│  declare ptr @cm_string_concat(ptr, ptr)                       │
│  declare void @cm_println_string(ptr)                          │
└──────────────────┬──────────────────────────────────────────────┘
                   │
                   ▼
┌──────────────────────────────────────────────────────────────────┐
│  COMPILATION (llvm-backend)                                     │
│  LLVM optimizations and machine code generation                │
└──────────────────┬──────────────────────────────────────────────┘
                   │
                   ▼
┌──────────────────────────────────────────────────────────────────┐
│  RUNTIME EXECUTION                                              │
│                                                                  │
│  1. main() called                                               │
│  2. x = 42                                                      │
│  3. cm_format_int(42) → "2a" (hex string on heap)             │
│  4. cm_string_concat("Value: ", "2a") → "Value: 2a" (heap)    │
│  5. cm_println_string("Value: 2a") → prints + newline          │
│  6. Free allocated strings                                      │
│  7. Return 0                                                    │
└──────────────────────────────────────────────────────────────────┘
```

---

## Key Decision Points in LLVM Codegen

### Binary Add Operation (mir_to_llvm.cpp:556-615)

```
detectBinaryAdd()
    │
    ├─ Check lhsType.isPointerTy()
    │   └─ YES → lhsStr = lhs
    │   └─ NO  → Continue to next check
    │
    ├─ Check rhsType.isPointerTy()
    │   └─ YES → rhsStr = rhs (or convert if not pointer)
    │   └─ NO  → String concat needed (one is pointer)
    │
    ├─ Convert non-pointer operands
    │   │
    │   ├─ isFloatingPointTy()
    │   │   └─ Call cm_format_double(f64) → ptr
    │   │
    │   └─ isIntegerTy()
    │       └─ Call cm_format_int(i32) → ptr
    │
    └─ Call cm_string_concat(ptr, ptr) → ptr
        └─ Result can be printed or further concatenated
```

### Function Call (mir_to_llvm.cpp:298-369)

```
detectFunctionCall()
    │
    ├─ Match: "print" | "println"
    │   │
    │   └─ Check argument type
    │       │
    │       ├─ isPointerTy() (String)
    │       │   ├─ println → Call cm_println_string(ptr)
    │       │   └─ print  → Call cm_print_string(ptr)
    │       │
    │       ├─ isIntegerTy()
    │       │   ├─ BitWidth == 8 (bool)
    │       │   │   ├─ println → Call cm_print_bool(i8, 1)
    │       │   │   └─ print  → Call cm_print_bool(i8, 0)
    │       │   │
    │       │   └─ BitWidth >= 16 (int)
    │       │       ├─ println → Call cm_println_int(i32)
    │       │       └─ print  → Call cm_print_int(i32)
    │       │
    │       ├─ isFloatingPointTy()
    │       │   ├─ println → Call cm_println_double(f64)
    │       │   └─ print  → Call cm_print_double(f64)
    │       │
    │       └─ else → Error or default behavior
    │
    └─ Other functions → Normal call handling
```

---

## Runtime Function Signatures

### Print Functions

```c
// Output a string (no newline)
void cm_print_string(const char* s);

// Output a string with newline
void cm_println_string(const char* s);

// Output an integer
void cm_print_int(int32_t i);

// Output an integer with newline
void cm_println_int(int32_t i);

// Output a double
void cm_print_double(double d);

// Output a double with newline
void cm_println_double(double d);

// Output a bool (with_newline: 0=false, 1=true)
void cm_print_bool(uint8_t b, uint8_t with_newline);
```

### Format Functions

```c
// Convert integer to string representation
char* cm_format_int(int32_t i);

// Convert double to string representation
char* cm_format_double(double d);

// Concatenate two strings (allocates new string on heap)
char* cm_string_concat(const char* left, const char* right);
```

### Format Specifications (FormatSpec enum)

```
FormatSpec::Binary     → :b  → 2进数 (0b1010)
FormatSpec::Octal      → :o  → 8进数 (0o12)
FormatSpec::Hex        → :x  → 16进数小文字 (0xa)
FormatSpec::HexUpper   → :X  → 16进数大文字 (0xA)
FormatSpec::Exponential → :e  → 指数表記小文字 (1.23e+2)
FormatSpec::ExpUpper   → :E  → 指数表記大文字 (1.23E+2)
```

---

## Memory Management

### String Literal Lifetime
```
┌─────────────────────────────────────────┐
│  Compile Time: Global String Constants  │
│  @.str = "Value: \00"                  │
│  (Read-only memory segment)             │
│  Lifetime: Entire program execution    │
└─────────────────────────────────────────┘
```

### Formatted String Lifetime
```
┌──────────────────────────────────────┐
│  Runtime: Dynamic Allocation        │
│  cm_format_int(42) → malloc(3)      │
│  Returns pointer to "42" on heap    │
│  Lifetime: Until explicitly freed   │
│  Responsibility: Caller must free   │
└──────────────────────────────────────┘
```

### Concatenated String Lifetime
```
┌────────────────────────────────────────┐
│  Runtime: Dynamic Allocation           │
│  cm_string_concat(s1, s2)             │
│  │                                     │
│  ├─ malloc(strlen(s1) + strlen(s2))  │
│  ├─ strcpy/strcat operations         │
│  └─ Return pointer to new string     │
│                                        │
│  Lifetime: Until explicitly freed     │
│  Responsibility: Caller must free     │
└────────────────────────────────────────┘
```

---

## Type Conversion Matrix

### From Non-String Types

```
┌────────────┬──────────────────┬────────────────────────────────┐
│  From Type │  LLVM Type       │  Conversion Function           │
├────────────┼──────────────────┼────────────────────────────────┤
│  i8 (bool) │  i8              │  N/A (cm_print_bool direct)    │
├────────────┼──────────────────┼────────────────────────────────┤
│  i16, i32, │  i32/i64         │  cm_format_int(i32) → ptr      │
│  i64 (int) │                  │  (or cm_print_int direct)      │
├────────────┼──────────────────┼────────────────────────────────┤
│  f32       │  float           │  f32 → f64 (fpext)             │
│            │                  │  then cm_format_double(f64)    │
├────────────┼──────────────────┼────────────────────────────────┤
│  f64       │  double          │  cm_format_double(f64) → ptr   │
│            │                  │  (or cm_print_double direct)   │
├────────────┼──────────────────┼────────────────────────────────┤
│  string    │  i8*             │  No conversion needed          │
└────────────┴──────────────────┴────────────────────────────────┘
```

---

## File Organization

```
src/
├── hir/
│   └── string_interpolation.hpp          ← Interpolation parsing
│
├── common/
│   └── format_string.hpp                 ← Format string parsing
│
├── codegen/llvm/
│   ├── mir_to_llvm.hpp
│   ├── mir_to_llvm.cpp                   ← Code generation (print/concat)
│   ├── context.hpp
│   └── context.cpp                       ← Runtime function declarations
│
└── frontend/types/
    └── type_checker.hpp                  ← Uses string_interpolation.hpp

std/
├── io/
│   └── mod.cm                            ← Wrapper functions
│
└── io_ffi.cm                             ← FFI declarations
```

---

## Integration with Other Components

### Relationship Diagram

```
┌─────────────────────────────────────────────────────────────┐
│  Source Code (println("Value: {}", x))                      │
└──────────────────────┬──────────────────────────────────────┘
                       │ Parse
                       ▼
┌─────────────────────────────────────────────────────────────┐
│  AST (StringLiteral "Value: {}", Variable x)               │
└──────────────────────┬──────────────────────────────────────┘
                       │ Type Check
                       │ StringInterpolationProcessor
                       │   .hasInterpolation() → true
                       │   .splitInterpolatedString() → parts
                       │   .createInterpolatedStringExpr()
                       ▼
┌─────────────────────────────────────────────────────────────┐
│  HIR (Binary(Add, StringLit "Value: ", Call(toString, x))) │
└──────────────────────┬──────────────────────────────────────┘
                       │ Lowering
                       ▼
┌─────────────────────────────────────────────────────────────┐
│  MIR                                                        │
│  bb0:                                                       │
│    %0 = const "Value: "                                    │
│    %1 = local x                                            │
│    %2 = call cm_format_int(%1)                             │
│    %3 = binary_add %0, %2                                  │
│    %4 = call cm_println_string(%3)                         │
│    return                                                   │
└──────────────────────┬──────────────────────────────────────┘
                       │ LLVM Codegen
                       │ convertBinaryOp()
                       │   .isPointerTy() → true
                       │   .isIntegerTy() → true
                       │   → String concat path
                       │ convertTerminator()
                       │   .funcName == "println"
                       │   → cm_println_string call
                       ▼
┌─────────────────────────────────────────────────────────────┐
│  LLVM IR (with runtime calls)                              │
└──────────────────────┬──────────────────────────────────────┘
                       │ Link
                       ▼
┌─────────────────────────────────────────────────────────────┐
│  Executable (with cm_*.o runtime)                          │
└──────────────────────┬──────────────────────────────────────┘
                       │ Execute
                       ▼
┌─────────────────────────────────────────────────────────────┐
│  Output: Value: 42                                          │
└─────────────────────────────────────────────────────────────┘
```

