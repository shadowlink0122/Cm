# String Interpolation Examples and Code References

## Quick Reference Examples

### Example 1: Simple String Print

**Cm Code**:
```cm
fn main() {
    println("Hello, World!");
}
```

**HIR** (no interpolation):
```cm
CallExpr("println", [StringLiteral("Hello, World!")])
```

**MIR**:
```
%0 = const "Hello, World!"
%1 = call cm_println_string(%0)
return
```

**LLVM IR**:
```llvm
define i32 @main() {
entry:
  %0 = call void @cm_println_string(ptr @.str)
  ret i32 0
}
@.str = private unnamed_addr constant [14 x i8] c"Hello, World!\00"
declare void @cm_println_string(ptr)
```

---

### Example 2: Integer Interpolation

**Cm Code**:
```cm
fn main() {
    int x = 42;
    println("The answer is: {}", x);
}
```

**HIR Processing**:
```
StringInterpolationProcessor::hasInterpolation("The answer is: {}")
  → true (found {})

StringInterpolationProcessor::splitInterpolatedString(...)
  → [
      StringPart(LITERAL, "The answer is: "),
      StringPart(INTERPOLATION, "x", "")
    ]

StringInterpolationProcessor::createInterpolatedStringExpr(...)
  → Binary(Add,
      StringLiteral("The answer is: "),
      Call("toString", Variable("x"))
    )
```

**MIR**:
```
%0 = local x              // load x
%1 = const "The answer is: "
%2 = call cm_format_int(%0)  // x to string
%3 = binary_add %1, %2       // concatenate
%4 = call cm_println_string(%3)
return
```

**LLVM IR Generated** (in mir_to_llvm.cpp:556-615):
```llvm
entry:
  %x_alloca = alloca i32, align 4
  store i32 42, ptr %x_alloca
  
  ; Load x
  %x = load i32, ptr %x_alloca
  
  ; Convert to string: cm_format_int(42)
  %x_str = call ptr @cm_format_int(i32 %x)
  
  ; Get constant string
  %str_ptr = getelementptr [15 x i8], ptr @.str, i64 0, i64 0
  
  ; Concatenate: "The answer is: " + "42"
  %result = call ptr @cm_string_concat(ptr %str_ptr, ptr %x_str)
  
  ; Print: println(result)
  call void @cm_println_string(ptr %result)
  
  ret i32 0

@.str = private unnamed_addr constant [15 x i8] c"The answer is: \00"
declare ptr @cm_format_int(i32)
declare ptr @cm_string_concat(ptr, ptr)
declare void @cm_println_string(ptr)
```

---

### Example 3: Hex Format Specifier

**Cm Code**:
```cm
fn main() {
    int num = 255;
    println("Hex: {num:x}");
}
```

**HIR Processing**:
```
StringInterpolationProcessor::extractInterpolations("Hex: {num:x}")
  → [InterpolatedVar(name: "num", format_spec: "x")]

StringInterpolationProcessor::splitInterpolatedString(...)
  → [
      StringPart(LITERAL, "Hex: "),
      StringPart(INTERPOLATION, "num", "x")
    ]

StringInterpolationProcessor::createInterpolatedStringExpr(...)
  → Binary(Add,
      StringLiteral("Hex: "),
      Call("formatHex", Variable("num"))  // format spec applied
    )
```

**MIR**:
```
%0 = local num
%1 = const "Hex: "
%2 = call cm_format_int(%0)    // converts 255 to "ff"
%3 = binary_add %1, %2
%4 = call cm_println_string(%3)
return
```

---

### Example 4: Multiple Arguments

**Cm Code**:
```cm
fn main() {
    int x = 10;
    int y = 20;
    println("x={}, y={}", x, y);
}
```

**HIR Processing**:
```
StringInterpolationProcessor::splitInterpolatedString(...)
  → [
      StringPart(LITERAL, "x="),
      StringPart(INTERPOLATION, ""),    // positional arg 0
      StringPart(LITERAL, ", y="),
      StringPart(INTERPOLATION, "")     // positional arg 1
    ]

Result Expression (chained concatenation):
  "x=" + toString(x) + ", y=" + toString(y)
```

**MIR** (with chaining):
```
%0 = const "x="
%1 = local x
%2 = call cm_format_int(%1)      // "10"
%3 = binary_add %0, %2           // "x=" + "10"
%4 = const ", y="
%5 = binary_add %3, %4           // "x=10" + ", y="
%6 = local y
%7 = call cm_format_int(%6)      // "20"
%8 = binary_add %5, %7           // "x=10, y=" + "20"
%9 = call cm_println_string(%8)
return
```

---

### Example 5: Float Formatting

**Cm Code**:
```cm
fn main() {
    float pi = 3.14159;
    println("Pi: {pi:.2}", pi);
}
```

**HIR Processing**:
```
StringInterpolationProcessor::extractInterpolations(...)
  → [InterpolatedVar(name: "pi", format_spec: ".2")]

FormatSpec Parsing (format_string.hpp):
  precision = 2
  spec = FormatSpec::Default (but has_precision = true)
```

**LLVM IR Generated**:
```llvm
entry:
  %pi_alloca = alloca float, align 4
  store float 0x401921CACA000000, ptr %pi_alloca
  
  ; Load float
  %pi_float = load float, ptr %pi_alloca
  
  ; Convert f32 to f64 for format function
  %pi_double = fpext float %pi_float to double
  
  ; Format with precision: cm_format_double(pi, 2)
  %pi_str = call ptr @cm_format_double(double %pi_double)
  
  ; Get constant string
  %str_ptr = getelementptr [5 x i8], ptr @.str, i64 0, i64 0
  
  ; Concatenate
  %result = call ptr @cm_string_concat(ptr %str_ptr, ptr %pi_str)
  
  call void @cm_println_string(ptr %result)
  ret i32 0

@.str = private unnamed_addr constant [5 x i8] c"Pi: \00"
declare ptr @cm_format_double(double)
```

---

## Code Location References

### HIR String Interpolation Processing

**File**: `/Users/shadowlink/Documents/git/Cm/src/hir/string_interpolation.hpp`

**Key Functions**:

1. **hasInterpolation()** (Lines 23-39)
   ```cpp
   static bool hasInterpolation(const std::string& str) {
       size_t pos = 0;
       while ((pos = str.find('{', pos)) != std::string::npos) {
           // Skip escaped {{
           if (pos + 1 < str.size() && str[pos + 1] == '{') {
               pos += 2;
               continue;
           }
           
           size_t end = str.find('}', pos);
           if (end != std::string::npos) {
               return true;  // Found interpolation
           }
           pos++;
       }
       return false;
   }
   ```

2. **extractInterpolations()** (Lines 42-82)
   ```cpp
   static std::vector<InterpolatedVar> extractInterpolations(const std::string& str) {
       std::vector<InterpolatedVar> vars;
       size_t pos = 0;
       
       while ((pos = str.find('{', pos)) != std::string::npos) {
           // Skip escaped {{
           if (pos + 1 < str.size() && str[pos + 1] == '{') {
               pos += 2;
               continue;
           }
           
           size_t end = str.find('}', pos);
           if (end != std::string::npos) {
               std::string content = str.substr(pos + 1, end - pos - 1);
               
               InterpolatedVar var;
               var.position = pos;
               
               // Check for format spec (variable:format)
               size_t colon = content.find(':');
               if (colon != std::string::npos) {
                   var.name = content.substr(0, colon);
                   var.format_spec = content.substr(colon + 1);
               } else {
                   var.name = content;
                   var.format_spec = "";
               }
               
               if (!var.name.empty() && !std::isdigit(var.name[0])) {
                   vars.push_back(var);
               }
               
               pos = end + 1;
           } else {
               break;
           }
       }
       
       return vars;
   }
   ```

3. **splitInterpolatedString()** (Lines 92-146)
   - Converts string into parts with proper escape handling
   - Returns vector of StringPart with type (LITERAL or INTERPOLATION)

4. **createInterpolatedStringExpr()** (Lines 149-194)
   ```cpp
   static HirExprPtr createInterpolatedStringExpr(
       const std::string& str,
       const std::function<HirExprPtr(const std::string&)>& resolveVariable) {
       
       auto parts = splitInterpolatedString(str);
       
       // Single literal case
       if (parts.size() == 1 && parts[0].type == StringPart::LITERAL) {
           auto lit = std::make_unique<HirLiteral>();
           lit->value = parts[0].content;
           return std::make_unique<HirExpr>(std::move(lit), make_string());
       }
       
       // Chain parts with concatenation
       HirExprPtr result = nullptr;
       for (const auto& part : parts) {
           HirExprPtr part_expr;
           
           if (part.type == StringPart::LITERAL) {
               auto lit = std::make_unique<HirLiteral>();
               lit->value = part.content;
               part_expr = std::make_unique<HirExpr>(std::move(lit), make_string());
           } else {
               // Interpolation: resolve variable
               auto var_expr = resolveVariable(part.content);
               
               // Apply format if specified
               if (!part.format_spec.empty()) {
                   part_expr = applyFormat(std::move(var_expr), part.format_spec);
               } else {
                   part_expr = convertToString(std::move(var_expr));
               }
           }
           
           if (!result) {
               result = std::move(part_expr);
           } else {
               result = createStringConcat(std::move(result), std::move(part_expr));
           }
       }
       
       return result;
   }
   ```

---

### LLVM Code Generation

**File**: `/Users/shadowlink/Documents/git/Cm/src/codegen/llvm/mir_to_llvm.cpp`

#### Print/Println Handling (Lines 298-369)

```cpp
// print/println の特別処理
if (funcName == "print" || funcName == "println" ||
    funcName == "std::io::print" || funcName == "std::io::println") {
    
    if (!callData.args.empty()) {
        auto arg = convertOperand(*callData.args[0]);
        auto argType = arg->getType();
        bool isNewline = funcName.find("println") != std::string::npos;
        
        // String case
        if (argType->isPointerTy()) {
            auto printFunc = isNewline
                ? module->getOrInsertFunction("cm_println_string", ...)
                : module->getOrInsertFunction("cm_print_string", ...);
            builder->CreateCall(printFunc, {arg});
        }
        // Boolean case
        else if (argType->isIntegerTy()) {
            auto intType = llvm::cast<llvm::IntegerType>(argType);
            if (intType->getBitWidth() == 8) {
                auto printBoolFunc = module->getOrInsertFunction("cm_print_bool", ...);
                auto withNewline = isNewline 
                    ? llvm::ConstantInt::get(ctx.getI8Type(), 1) 
                    : llvm::ConstantInt::get(ctx.getI8Type(), 0);
                builder->CreateCall(printBoolFunc, {arg, withNewline});
            }
            // Integer case
            else {
                auto printFunc = isNewline
                    ? module->getOrInsertFunction("cm_println_int", ...)
                    : module->getOrInsertFunction("cm_print_int", ...);
                builder->CreateCall(printFunc, {intArg});
            }
        }
        // Float case
        else if (argType->isFloatingPointTy()) {
            auto printFunc = isNewline
                ? module->getOrInsertFunction("cm_println_double", ...)
                : module->getOrInsertFunction("cm_print_double", ...);
            builder->CreateCall(printFunc, {doubleArg});
        }
    }
}
```

#### String Concatenation (Lines 556-615)

```cpp
case mir::MirBinaryOp::Add: {
    auto lhsType = lhs->getType();
    auto rhsType = rhs->getType();
    
    // String concatenation logic
    if (lhsType->isPointerTy() || rhsType->isPointerTy()) {
        // Convert LHS to string
        llvm::Value* lhsStr = lhs;
        if (!lhsType->isPointerTy()) {
            if (lhsType->isFloatingPointTy()) {
                // Double formatting
                auto formatFunc = module->getOrInsertFunction("cm_format_double", ...);
                auto lhsDouble = lhs;
                if (lhsType->isFloatTy()) {
                    lhsDouble = builder->CreateFPExt(lhs, ctx.getF64Type());
                }
                lhsStr = builder->CreateCall(formatFunc, {lhsDouble});
            } else {
                // Integer formatting
                auto formatFunc = module->getOrInsertFunction("cm_format_int", ...);
                auto lhsInt = lhs;
                if (lhsType->isIntegerTy() && 
                    lhsType->getIntegerBitWidth() != 32) {
                    lhsInt = builder->CreateSExt(lhs, ctx.getI32Type());
                }
                lhsStr = builder->CreateCall(formatFunc, {lhsInt});
            }
        }
        
        // Convert RHS similarly...
        llvm::Value* rhsStr = rhs;
        if (!rhsType->isPointerTy()) {
            // ... same conversion logic ...
        }
        
        // Concatenate
        auto concatFunc = module->getOrInsertFunction("cm_string_concat", ...);
        return builder->CreateCall(concatFunc, {lhsStr, rhsStr});
    }
    
    // Numeric addition
    if (lhs->getType()->isFloatingPointTy()) {
        return builder->CreateFAdd(lhs, rhs, "fadd");
    }
    return builder->CreateAdd(lhs, rhs, "add");
}
```

#### String Literal Constant (Lines 540-548)

```cpp
else if (std::holds_alternative<std::string>(constant.value)) {
    // String literal -> global string pointer
    auto& str = std::get<std::string>(constant.value);
    return builder->CreateGlobalStringPtr(str, "str");
}
```

---

### Format String Parser

**File**: `/Users/shadowlink/Documents/git/Cm/src/common/format_string.hpp`

#### Placeholder Parsing (Lines 63-120)

```cpp
static ParseResult parse(const std::string& format) {
    ParseResult result;
    result.success = true;
    
    std::string current_literal;
    size_t pos = 0;
    size_t next_positional = 0;
    
    while (pos < format.size()) {
        if (format[pos] == '{') {
            if (pos + 1 < format.size() && format[pos + 1] == '{') {
                // Escaped '{'
                current_literal += '{';
                pos += 2;
            } else {
                // Placeholder start
                result.literal_parts.push_back(current_literal);
                current_literal.clear();
                
                auto [placeholder, end_pos] = parse_placeholder(format, pos);
                if (!placeholder.has_value()) {
                    result.success = false;
                    result.error_message = "Invalid placeholder at position " + 
                                          std::to_string(pos);
                    return result;
                }
                
                // Auto-assign positional index
                if (placeholder->type == Placeholder::Positional &&
                    placeholder->position == SIZE_MAX) {
                    placeholder->position = next_positional++;
                }
                
                result.placeholders.push_back(*placeholder);
                pos = end_pos;
            }
        } else if (format[pos] == '}') {
            if (pos + 1 < format.size() && format[pos + 1] == '}') {
                // Escaped '}'
                current_literal += '}';
                pos += 2;
            } else {
                // Unmatched '}'
                result.success = false;
                result.error_message = "Unmatched '}' at position " + 
                                      std::to_string(pos);
                return result;
            }
        } else {
            current_literal += format[pos];
            pos++;
        }
    }
    
    result.literal_parts.push_back(current_literal);
    return result;
}
```

---

## Testing Examples

### Test Case 1: Basic Interpolation

```cm
int x = 42;
println("Number: {}", x);
// Expected output: "Number: 42"
```

### Test Case 2: Hex Format

```cm
int num = 255;
println("Hex: {:x}", num);
// Expected output: "Hex: ff"
```

### Test Case 3: Float with Precision

```cm
float pi = 3.14159;
println("Pi: {:.2}", pi);
// Expected output: "Pi: 3.14"
```

### Test Case 4: Mixed Types

```cm
int x = 10;
float y = 2.5;
println("x={}, y={}", x, y);
// Expected output: "x=10, y=2.5"
```

### Test Case 5: Escaped Braces

```cm
println("{{not interpolated}}");
// Expected output: "{not interpolated}"
```

---

## Performance Considerations

### String Concatenation Cost

Each `+` operation in interpolation chain:
- 1x `strlen()` call (LHS)
- 1x `strlen()` call (RHS)
- 1x `malloc()` call
- 1x `memcpy()` call (LHS data)
- 1x `memcpy()` call (RHS data)

For 3-part string like `"a" + x + "b" + y`:
- Creates 3 temporary strings on heap
- 2 concatenation operations
- Total: 6x memory allocation

### Optimization Opportunities

1. **String Builder Pattern**
   - Accumulate parts in a buffer
   - Single final allocation
   - Would reduce allocations from N to 1

2. **Constant Folding**
   - Compile-time concatenation of literals
   - `"hello" + "world"` → `"helloworld"`

3. **Inline Formatting**
   - Small format functions could be inlined
   - Avoid function call overhead

4. **Memory Pooling**
   - Pre-allocate string buffers
   - Reduce malloc/free overhead

