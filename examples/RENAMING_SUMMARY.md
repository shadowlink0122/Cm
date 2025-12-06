# Examples ファイル名変更サマリー

## ファイル名変更一覧

すべてのサンプルファイルを `0*_*.cm` パターンに統一しました。

### basics/
- 00_simple.cm → 01_simple.cm
- 01_hello.cm → 02_hello.cm
- 02_variables.cm → 03_variables.cm
- 05_compound_assignments.cm → 04_compound_assignments.cm
- 06_format_strings.cm → 05_format_strings.cm
- 07_named_placeholders.cm → 06_named_placeholders.cm
- 08_auto_variable_capture.cm → 07_auto_variable_capture.cm
- hello_world_with_import.cm → 08_hello_world_with_import.cm (移動)

### control_flow/
- 03_control_flow.cm → 01_control_flow.cm
- 07_loop.cm → 02_loop.cm

### functions/
- 04_functions.cm → 01_functions.cm

### modules/
- 08_modules.cm → 01_modules.cm
- 09_custom_module.cm → 02_custom_module.cm
- 10_macros.cm → 03_macros.cm

### optimization/
- 06_optimization.cm → 01_optimization.cm

### advanced/
- memory_safety_demo.cm → 01_memory_safety_demo.cm

### generics/
- implicit_generics.cm → 01_implicit_generics.cm

### impl/
- constructor_example.cm → 01_constructor_example.cm

### overload/
- basic_overload.cm → 01_basic_overload.cm

### syntax/
- option3_functions.cm → 01_functions.cm

### transpiler/
- overload_compatibility.cm → 01_overload_compatibility.cm

### types/
- typedef_example.cm → 01_typedef_example.cm

## コード修正内容

### C++スタイル構文への統一

1. **関数定義**: `fn name() -> type` → `type name()`
2. **変数宣言**: `let x = value` → `type x = value`
3. **構造体定義**: `field: type` → `type field;`
4. **import文**: `import std.io` → `import std::io`

### 新機能の活用

1. **変数自動キャプチャ**:
   - 旧: `println("Value: {}", x)`
   - 新: `println("Value: {x}")`

2. **println統一**:
   - すべての `print()` を `println()` に変更
   - 適切な `import std::io::println;` を追加

### 修正済みファイル

✅ basics/02_hello.cm - 変数自動キャプチャ使用
✅ control_flow/01_control_flow.cm - println化、変数キャプチャ
✅ functions/01_functions.cm - println化、double型使用
✅ modules/01_modules.cm - import構文修正
✅ advanced/01_memory_safety_demo.cm - 完全なC++スタイル化