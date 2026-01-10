[English](STRING_INTERPOLATION_LLVM.en.html)

# String Interpolation and Format Strings in LLVM Backend

## Overview

String interpolation and format string handling in the Cm language LLVM backend follows a multi-layer approach:

1. **HIR Level** - String interpolation detection and parsing
2. **MIR Level** - Converted to string concatenation operations
3. **LLVM Backend** - Code generation with runtime function calls

---

## Layer 1: HIR-Level String Interpolation Processing

### Location
- **File**: `/Users/shadowlink/Documents/git/Cm/src/hir/string_interpolation.hpp`
- **Namespace**: `cm::hir`
- **Class**: `StringInterpolationProcessor`

### Key Features

#### Interpolation Detection
```cpp
static bool hasInterpolation(const std::string& str)
```
- Detects `{...}` patterns in strings
- Handles escaped braces: `{{` → `{`, `}}` → `}`

#### Interpolation Extraction
```cpp
static std::vector<InterpolatedVar> extractInterpolations(const std::string& str)
```
- Extracts variable names and format specifiers
- Example: `"Value: {x:hex}"` → `InterpolatedVar{name: "x", format_spec: "hex"}`

#### String Splitting
```cpp
static std::vector<StringPart> splitInterpolatedString(const std::string& str)
```
- Splits string into literal and interpolation parts
- Returns `StringPart` with type (LITERAL/INTERPOLATION) and content

#### HIR Expression Generation
```cpp
static HirExprPtr createInterpolatedStringExpr(
    const std::string& str,
    const std::function<HirExprPtr(const std::string&)>& resolveVariable)
```
- Converts interpolated strings to string concatenation expressions
- Applies format functions (`formatHex`, `formatBinary`, etc.)
- Chains parts with `+` operator (string concat)

### Format Spec Support
- `:x` - Hex (lowercase)
- `:X` - Hex (uppercase)
- `:b` - Binary
- `:o` - Octal
- `:.2f` - Decimal precision
- `:e` - Exponential (lowercase)
- `:E` - Exponential (uppercase)

---

## Layer 2: Format String Parser

### Location
- **File**: `/Users/shadowlink/Documents/git/Cm/src/common/format_string.hpp`
- **Namespace**: `cm`
- **Classes**: `FormatStringParser`, `FormatStringFormatter`

### Placeholder Structure
```cpp
struct Placeholder {
    enum Type {
        Positional,  // {} or {0}, {1}
        Named,       // {name}
    };
    
    Type type;
    size_t position;        // Index for positional args
    std::string name;       // Name for named args
    FormatSpec spec;        // Format type (Binary, Hex, etc.)
    Alignment align;        // Left, Right, Center
    size_t width;           // Minimum field width
    size_t precision;       // Decimal precision
    char fill_char;         // Padding character
    bool has_width;
    bool has_precision;
};
```

### FormatSpec Types
```cpp
enum class FormatSpec {
    Default,      // Default display
    Binary,       // :b
    Octal,        // :o
    Hex,          // :x (lowercase)
    HexUpper,     // :X (uppercase)
    Exponential,  // :e (lowercase)
    ExpUpper,     // :E (uppercase)
};
```

### Parsing Support
- Position arguments: `{}`, `{0}`, `{1}`
- Named arguments: `{name}` (partial support)
- Width: `{:10}` (10 chars minimum)
- Precision: `{:.2f}` (2 decimal places)
- Alignment: `{:<10}`, `{:>10}`, `{:^10}`
- Fill char: `{:*>10}` (pad with `*`)
- Combined: `{:0>8x}` (8-char hex padded with 0)

---

## Layer 3: LLVM Code Generation

### Location
- **File**: `/Users/shadowlink/Documents/git/Cm/src/codegen/llvm/mir_to_llvm.cpp`
- **Class**: `MIRToLLVM`

### String Output Functions

#### println/print Implementation (Lines 298-369)

```cpp
// print/println の特別処理
if (funcName == "print" || funcName == "println" ||
    funcName == "std::io::print" || funcName == "std::io::println") {
    
    if (!callData.args.empty()) {
        auto arg = convertOperand(*callData.args[0]);
        auto argType = arg->getType();
        bool isNewline = funcName.find("println") != std::string::npos;
        
        // ケース1: String (pointer type)
        if (argType->isPointerTy()) {
            auto printFunc = isNewline
                ? module->getOrInsertFunction("cm_println_string",
                    llvm::FunctionType::get(ctx.getVoidType(), {ctx.getPtrType()}, false))
                : module->getOrInsertFunction("cm_print_string",
                    llvm::FunctionType::get(ctx.getVoidType(), {ctx.getPtrType()}, false));
            builder->CreateCall(printFunc, {arg});
        }
        
        // ケース2: Integer
        else if (argType->isIntegerTy()) {
            auto intType = llvm::cast<llvm::IntegerType>(argType);
            if (intType->getBitWidth() == 8) {
                // Boolean
                auto printBoolFunc = module->getOrInsertFunction("cm_print_bool",
                    llvm::FunctionType::get(ctx.getVoidType(),
                        {ctx.getI8Type(), ctx.getI8Type()}, false));
                auto withNewline = isNewline ? llvm::ConstantInt::get(ctx.getI8Type(), 1) : 0;
                builder->CreateCall(printBoolFunc, {arg, withNewline});
            } else {
                // Int
                auto printFunc = isNewline
                    ? module->getOrInsertFunction("cm_println_int",
                        llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI32Type()}, false))
                    : module->getOrInsertFunction("cm_print_int",
                        llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI32Type()}, false));
                builder->CreateCall(printFunc, {intArg});
            }
        }
        
        // ケース3: Float
        else if (argType->isFloatingPointTy()) {
            auto printFunc = isNewline
                ? module->getOrInsertFunction("cm_println_double",
                    llvm::FunctionType::get(ctx.getVoidType(), {ctx.getF64Type()}, false))
                : module->getOrInsertFunction("cm_print_double",
                    llvm::FunctionType::get(ctx.getVoidType(), {ctx.getF64Type()}, false));
            builder->CreateCall(printFunc, {doubleArg});
        }
    }
}
```

**Runtime Functions Called**:
- `cm_print_string(ptr)` - Print string without newline
- `cm_println_string(ptr)` - Print string with newline
- `cm_print_int(i32)` - Print integer
- `cm_println_int(i32)` - Print integer with newline
- `cm_print_double(f64)` - Print float
- `cm_println_double(f64)` - Print float with newline
- `cm_print_bool(i8, i8)` - Print bool (second arg: with_newline)

### String Concatenation (Binary `+` Operation)

#### Lines 556-615: Binary Add Operation Handling

```cpp
case mir::MirBinaryOp::Add: {
    auto lhsType = lhs->getType();
    auto rhsType = rhs->getType();
    
    // String concatenation when either operand is pointer type
    if (lhsType->isPointerTy() || rhsType->isPointerTy()) {
        // Convert left operand to string if needed
        llvm::Value* lhsStr = lhs;
        if (!lhsType->isPointerTy()) {
            if (lhsType->isFloatingPointTy()) {
                // Double → String conversion
                auto formatFunc = module->getOrInsertFunction("cm_format_double",
                    llvm::FunctionType::get(ctx.getPtrType(), {ctx.getF64Type()}, false));
                auto lhsDouble = lhs;
                if (lhsType->isFloatTy()) {
                    lhsDouble = builder->CreateFPExt(lhs, ctx.getF64Type());
                }
                lhsStr = builder->CreateCall(formatFunc, {lhsDouble});
            } else {
                // Integer → String conversion
                auto formatFunc = module->getOrInsertFunction("cm_format_int",
                    llvm::FunctionType::get(ctx.getPtrType(), {ctx.getI32Type()}, false));
                auto lhsInt = lhs;
                if (lhsType->isIntegerTy() && lhsType->getIntegerBitWidth() != 32) {
                    lhsInt = builder->CreateSExt(lhs, ctx.getI32Type());
                }
                lhsStr = builder->CreateCall(formatFunc, {lhsInt});
            }
        }
        
        // Convert right operand similarly
        llvm::Value* rhsStr = rhs;
        if (!rhsType->isPointerTy()) {
            // ... same conversion logic ...
        }
        
        // String concatenation
        auto concatFunc = module->getOrInsertFunction("cm_string_concat",
            llvm::FunctionType::get(ctx.getPtrType(), 
                {ctx.getPtrType(), ctx.getPtrType()}, false));
        return builder->CreateCall(concatFunc, {lhsStr, rhsStr});
    }
    
    // Numeric addition (falls through to normal arithmetic)
    if (lhs->getType()->isFloatingPointTy()) {
        return builder->CreateFAdd(lhs, rhs, "fadd");
    }
    return builder->CreateAdd(lhs, rhs, "add");
}
```

**Runtime Functions Called**:
- `cm_format_int(i32) -> ptr` - Convert integer to string
- `cm_format_double(f64) -> ptr` - Convert double to string
- `cm_string_concat(ptr, ptr) -> ptr` - Concatenate two strings

### String Literal Handling

#### Lines 540-548: convertConstant()

```cpp
else if (std::holds_alternative<std::string>(constant.value)) {
    // String literal
    auto& str = std::get<std::string>(constant.value);
    return builder->CreateGlobalStringPtr(str, "str");
}
```

- Creates global string constant with `CreateGlobalStringPtr`
- Returns pointer to string data (i8*)

---

## Layer 4: LLVM Context Setup

### Location
- **File**: `/Users/shadowlink/Documents/git/Cm/src/codegen/llvm/context.cpp`
- **Class**: `LLVMContext`

### Runtime Function Declarations

```cpp
void LLVMContext::declareRuntimeFunctions() {
    // printf (debug use)
    auto printfType = llvm::FunctionType::get(
        i32Ty,
        {ptrTy},  // format string
        true      // variadic
    );
    module->getOrInsertFunction("printf", printfType);
    
    // puts (simple output)
    auto putsType = llvm::FunctionType::get(
        i32Ty,
        {ptrTy},  // string
        false
    );
    module->getOrInsertFunction("puts", putsType);
}
```

### External Function Declaration (Lines 717-747)

```cpp
llvm::Function* MIRToLLVM::declareExternalFunction(const std::string& name) {
    if (name == "print" || name == "println") {
        // Declare as printf
        auto printfType = llvm::FunctionType::get(
            ctx.getI32Type(),
            {ctx.getPtrType()},
            true  // variadic
        );
        auto func = module->getOrInsertFunction("printf", printfType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // ... other declarations ...
}
```

---

## Standard Library Bindings

### Location
- **File 1**: `/Users/shadowlink/Documents/git/Cm/std/io/mod.cm`
- **File 2**: `/Users/shadowlink/Documents/git/Cm/std/io_ffi.cm`

### FFI Declarations (io_ffi.cm)
```cm
extern "C" int printf(const char* format, ...);
extern "C" int puts(const char* str);
extern "C" int fprintf(void* stream, const char* format, ...);
```

### Wrapper Functions (io/mod.cm)
```cm
extern "C" void cm_print_string(string s);
extern "C" void cm_print_int(int i);
extern "C" void cm_print_float(float f);
extern "C" void cm_print_bool(bool b);
extern "C" void cm_print_char(char c);

export void print(string s) {
    cm_print_string(s);
}

export void println(string s) {
    cm_print_string(s);
    cm_print_string("\n");
}
```

---

## Data Flow Example

### Input Cm Code
```cm
int x = 42;
println("Value: {}", x);
```

### Transformation Flow

1. **Parser**: Creates AST with string literal `"Value: {}"`

2. **HIR**: 
   - Detects interpolation in string
   - Splits into parts: `["Value: ", x]`
   - Generates: `"Value: " + toString(x)`

3. **MIR**:
   - Converts to MIR binary operation
   - LHS: String constant `"Value: "`
   - RHS: Variable `x` (i32)
   - Op: Add (string concatenation)

4. **LLVM Codegen**:
   ```llvm
   ; Get x value (assume in register)
   %x = load i32, ptr %x_alloca
   
   ; Convert x to string
   %x_str = call ptr @cm_format_int(i32 %x)
   
   ; Get "Value: " constant
   @const_str = private unnamed_addr constant [8 x i8] c"Value: \00"
   %str_ptr = getelementptr [8 x i8], ptr @const_str, i64 0, i64 0
   
   ; Concatenate
   %result = call ptr @cm_string_concat(ptr %str_ptr, ptr %x_str)
   
   ; Print result
   call void @cm_println_string(ptr %result)
   ```

5. **Runtime**:
   - `cm_format_int(42)` → `"42"` (allocated on heap)
   - `cm_string_concat("Value: ", "42")` → `"Value: 42"` (new allocation)
   - `cm_println_string(...)` → Output with newline

---

## Key Implementation Details

### Type Conversions

型情報はMIRオペランドから取得され、適切な変換が行われます：

- **bool (HIR: Bool)** → cm_print_bool with newline flag
- **tiny/short/int (signed)** → SExt to i32, cm_print_int / cm_format_replace_int
- **utiny/ushort/uint (unsigned)** → ZExt to i32, cm_print_uint / cm_format_replace_uint
- **float** → FPExt to f64, cm_print_double / cm_format_replace_double
- **double** → cm_print_double / cm_format_replace_double
- **char** → cm_format_char
- **string (ptr)** → cm_print_string / cm_format_replace_string

### Supported Format Specifiers

| 指定子 | 整数 | 浮動小数点 | 文字列 |
|--------|------|-----------|--------|
| `{}` | ✅ 10進数 | ✅ デフォルト | ✅ そのまま |
| `{:x}` | ✅ 16進数小文字 | - | - |
| `{:X}` | ✅ 16進数大文字 | - | - |
| `{:b}` | ✅ 2進数 | - | - |
| `{:o}` | ✅ 8進数 | - | - |
| `{:.N}` | - | ✅ 小数点以下N桁 | - |
| `{:e}` | - | ✅ 指数表記小文字 | - |
| `{:E}` | - | ✅ 指数表記大文字 | - |
| `{:<N}` | ✅ 左揃え | - | ✅ 左揃え |
| `{:>N}` | ✅ 右揃え | - | ✅ 右揃え |
| `{:^N}` | ✅ 中央揃え | - | ✅ 中央揃え |
| `{:0>N}` | ✅ ゼロ埋め | - | - |

### Optimization Opportunities
1. **String constant pooling** - Identical strings share same global
2. **Dead code elimination** - Unused formatting functions removed
3. **Format function specialization** - Different functions for different types
4. **Inline formatting** - Small formats could be inlined

### Limitations (Current)
- Named placeholders partially supported
- No custom format implementations
- Runtime heap allocation for all concatenations

---

## Integration Points

### Frontend to HIR
- Parser produces string literals
- HIR level applies interpolation processing
- Converts to expression trees for MIR lowering

### MIR to LLVM
- Binary Add operations trigger concatenation logic
- Function calls match against print/println patterns
- Type information retrieved via `getOperandType()` helper
- Signed/unsigned type distinction preserved for correct SExt/ZExt

### External Dependencies
- C standard library: `printf`, `puts`
- Custom runtime: `cm_print_*`, `cm_format_*`, `cm_format_replace_*`, `cm_string_concat`

---

## Related Files

| File | Purpose |
|------|---------|
| `src/hir/string_interpolation.hpp` | HIR-level interpolation |
| `src/common/format_string.hpp` | Format string parsing |
| `src/codegen/llvm/mir_to_llvm.cpp` | LLVM code generation |
| `src/codegen/llvm/mir_to_llvm.hpp` | MIR to LLVM converter header |
| `src/codegen/llvm/runtime.c` | C runtime library |
| `src/codegen/llvm/context.cpp` | Runtime function setup |
| `std/io.cm` | Standard I/O library with FFI |
