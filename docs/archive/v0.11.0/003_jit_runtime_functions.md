# JITコンパイラ - ランタイム関数完全一覧

## 概要

本ドキュメントはJITコンパイラで登録が必要な**全ランタイム関数**を網羅します。
JITでは、これらの関数をシンボルとして事前登録し、LLVM IRから呼び出せるようにします。

---

## 1. 出力関数 (Print)

### 1.1 文字列出力
| 関数名 | シグネチャ | 説明 |
|--------|-----------|------|
| `cm_print_string` | `void(char*)` | 文字列出力 |
| `cm_println_string` | `void(char*)` | 文字列出力+改行 |

### 1.2 整数出力
| 関数名 | シグネチャ | 説明 |
|--------|-----------|------|
| `cm_print_int` | `void(i32)` | int出力 |
| `cm_println_int` | `void(i32)` | int出力+改行 |
| `cm_print_uint` | `void(u32)` | uint出力 |
| `cm_println_uint` | `void(u32)` | uint出力+改行 |
| `cm_print_long` | `void(i64)` | long出力 |
| `cm_println_long` | `void(i64)` | long出力+改行 |
| `cm_print_ulong` | `void(u64)` | ulong出力 |
| `cm_println_ulong` | `void(u64)` | ulong出力+改行 |

### 1.3 浮動小数点出力
| 関数名 | シグネチャ | 説明 |
|--------|-----------|------|
| `cm_print_double` | `void(f64)` | double出力 |
| `cm_println_double` | `void(f64)` | double出力+改行 |
| `cm_print_float` | `void(f32)` | float出力 |
| `cm_println_float` | `void(f32)` | float出力+改行 |

### 1.4 その他出力
| 関数名 | シグネチャ | 説明 |
|--------|-----------|------|
| `cm_print_bool` | `void(i8)` | bool出力 |
| `cm_println_bool` | `void(i8)` | bool出力+改行 |
| `cm_print_char` | `void(i8)` | char出力 |
| `cm_println_char` | `void(i8)` | char出力+改行 |

---

## 2. フォーマット関数 (Format)

### 2.1 数値→文字列変換
| 関数名 | シグネチャ | 説明 |
|--------|-----------|------|
| `cm_format_int` | `char*(i32)` | int→文字列 |
| `cm_format_uint` | `char*(u32)` | uint→文字列 |
| `cm_format_long` | `char*(i64)` | long→文字列 |
| `cm_format_ulong` | `char*(u64)` | ulong→文字列 |
| `cm_format_double` | `char*(f64)` | double→文字列 |
| `cm_format_bool` | `char*(i8)` | bool→文字列 |
| `cm_format_char` | `char*(i8)` | char→文字列 |
| `cm_format_ptr` | `char*(ptr)` | ptr→16進文字列 |

### 2.2 文字列補間
| 関数名 | シグネチャ | 説明 |
|--------|-----------|------|
| `cm_format_unescape_braces` | `char*(char*)` | `{{`→`{`エスケープ解除 |
| `cm_format_replace_int` | `char*(char*, i64)` | `{}`をintで置換 |
| `cm_format_replace_uint` | `char*(char*, u64)` | `{}`をuintで置換 |
| `cm_format_replace_double` | `char*(char*, f64)` | `{}`をdoubleで置換 |
| `cm_format_replace_string` | `char*(char*, char*)` | `{}`を文字列で置換 |
| `cm_format_replace_bool` | `char*(char*, i8)` | `{}`をboolで置換 |
| `cm_format_replace_char` | `char*(char*, i8)` | `{}`をcharで置換 |
| `cm_format_replace_ptr` | `char*(char*, i64)` | `{}`をポインタで置換 |

---

## 3. 文字列関数 (String)

### 3.1 基本操作
| 関数名 | シグネチャ | 説明 |
|--------|-----------|------|
| `cm_string_length` | `i64(char*)` | 文字列長取得 |
| `cm_string_concat` | `char*(char*, char*)` | 文字列連結 |
| `cm_strcmp` | `i32(char*, char*)` | 文字列比較 |
| `cm_strncmp` | `i32(char*, char*, size_t)` | N文字比較 |

### 3.2 ビルトインメソッド
| 関数名 | シグネチャ | 説明 |
|--------|-----------|------|
| `__builtin_string_len` | `size_t(char*)` | `.len()` |
| `__builtin_string_charAt` | `char(char*, i64)` | `.charAt(idx)` |
| `__builtin_string_first` | `char(char*)` | `.first()` |
| `__builtin_string_last` | `char(char*)` | `.last()` |
| `__builtin_string_substring` | `char*(char*, i64, i64)` | `.substring(a, b)` |
| `__builtin_string_indexOf` | `i64(char*, char*)` | `.indexOf(sub)` |
| `__builtin_string_toUpperCase` | `char*(char*)` | `.toUpperCase()` |
| `__builtin_string_toLowerCase` | `char*(char*)` | `.toLowerCase()` |
| `__builtin_string_trim` | `char*(char*)` | `.trim()` |
| `__builtin_string_startsWith` | `bool(char*, char*)` | `.startsWith(s)` |
| `__builtin_string_endsWith` | `bool(char*, char*)` | `.endsWith(s)` |
| `__builtin_string_includes` | `bool(char*, char*)` | `.includes(s)` |
| `__builtin_string_repeat` | `char*(char*, i64)` | `.repeat(n)` |
| `__builtin_string_replace` | `char*(char*, char*, char*)` | `.replace(from, to)` |

---

## 4. スライス関数 (Slice)

### 4.1 ライフサイクル
| 関数名 | シグネチャ | 説明 |
|--------|-----------|------|
| `cm_slice_new` | `ptr(i64, i64)` | スライス作成(elem_size, cap) |
| `cm_slice_free` | `void(ptr)` | スライス解放 |
| `cm_slice_len` | `i64(ptr)` | 長さ取得 |
| `cm_slice_cap` | `i64(ptr)` | 容量取得 |
| `cm_slice_elem_size` | `i64(ptr)` | 要素サイズ取得 |

### 4.2 要素操作
| 関数名 | シグネチャ | 説明 |
|--------|-----------|------|
| `cm_slice_push_i8` | `void(ptr, i8)` | push (char/bool) |
| `cm_slice_push_i32` | `void(ptr, i32)` | push (int) |
| `cm_slice_push_i64` | `void(ptr, i64)` | push (long) |
| `cm_slice_push_f64` | `void(ptr, f64)` | push (double) |
| `cm_slice_push_ptr` | `void(ptr, ptr)` | push (pointer) |
| `cm_slice_push_slice` | `void(ptr, ptr)` | push (inner slice) |
| `cm_slice_pop_i8` | `i8(ptr)` | pop (char/bool) |
| `cm_slice_pop_i32` | `i32(ptr)` | pop (int) |
| `cm_slice_pop_i64` | `i64(ptr)` | pop (long) |
| `cm_slice_pop_f64` | `f64(ptr)` | pop (double) |
| `cm_slice_pop_ptr` | `ptr(ptr)` | pop (pointer) |

### 4.3 要素アクセス
| 関数名 | シグネチャ | 説明 |
|--------|-----------|------|
| `cm_slice_get_i8` | `i8(ptr, i64)` | get (char/bool) |
| `cm_slice_get_i32` | `i32(ptr, i64)` | get (int) |
| `cm_slice_get_i64` | `i64(ptr, i64)` | get (long) |
| `cm_slice_get_f64` | `f64(ptr, i64)` | get (double) |
| `cm_slice_get_ptr` | `ptr(ptr, i64)` | get (pointer) |
| `cm_slice_get_element_ptr` | `ptr(ptr, i64)` | 要素ポインタ取得 |
| `cm_slice_get_subslice` | `ptr(ptr, i64)` | 多次元サブスライス |
| `cm_slice_first_i32` | `i32(ptr)` | 最初の要素 |
| `cm_slice_first_i64` | `i64(ptr)` | 最初の要素 |
| `cm_slice_last_i32` | `i32(ptr)` | 最後の要素 |
| `cm_slice_last_i64` | `i64(ptr)` | 最後の要素 |
| `cm_slice_first_ptr` | `ptr(ptr)` | 最初へのポインタ |
| `cm_slice_last_ptr` | `ptr(ptr)` | 最後へのポインタ |

### 4.4 変換・操作
| 関数名 | シグネチャ | 説明 |
|--------|-----------|------|
| `cm_slice_delete` | `void(ptr, i64)` | 要素削除 |
| `cm_slice_clear` | `void(ptr)` | 全クリア |
| `cm_slice_reverse` | `ptr(ptr)` | 逆順コピー |
| `cm_slice_sort` | `ptr(ptr)` | ソートコピー |
| `cm_slice_subslice` | `ptr(ptr, i64, i64)` | サブスライス |
| `cm_array_to_slice` | `ptr(ptr, i64, i64)` | 配列→スライス |
| `cm_array2d_to_slice2d` | `ptr(ptr, i64, i64, i64)` | 2D配列→2Dスライス |

### 4.5 比較
| 関数名 | シグネチャ | 説明 |
|--------|-----------|------|
| `cm_array_equal` | `bool(ptr, ptr, i64, i64, i64)` | 配列等値比較 |
| `cm_slice_equal` | `bool(ptr, ptr)` | スライス等値比較 |

---

## 5. 高階関数 (Higher-Order)

### 5.1 map / filter
| 関数名 | シグネチャ | 説明 |
|--------|-----------|------|
| `__builtin_array_map` | `ptr(ptr, i64, fn_ptr)` | map (i32) |
| `__builtin_array_map_closure` | `ptr(ptr, i64, fn_ptr, i64)` | map+クロージャ |
| `__builtin_array_map_i64` | `ptr(ptr, i64, fn_ptr)` | map (i64) |
| `__builtin_array_map_i64_closure` | `ptr(ptr, i64, fn_ptr, i64)` | map+クロージャ |
| `__builtin_array_filter` | `ptr(ptr, i64, fn_ptr)` | filter (i32) |
| `__builtin_array_filter_closure` | `ptr(ptr, i64, fn_ptr, i64)` | filter+クロージャ |
| `__builtin_array_filter_i64` | `ptr(ptr, i64, fn_ptr)` | filter (i64) |
| `__builtin_array_filter_i64_closure` | `ptr(ptr, i64, fn_ptr, i64)` | filter+クロージャ |

### 5.2 forEach / reduce
| 関数名 | シグネチャ | 説明 |
|--------|-----------|------|
| `__builtin_array_forEach_i32` | `void(ptr, i64, fn_ptr)` | forEach (i32) |
| `__builtin_array_forEach_i64` | `void(ptr, i64, fn_ptr)` | forEach (i64) |
| `__builtin_array_reduce_i32` | `i32(ptr, i64, fn_ptr, i32)` | reduce (i32) |
| `__builtin_array_reduce_i64` | `i64(ptr, i64, fn_ptr, i64)` | reduce (i64) |

### 5.3 some / every / findIndex / find
| 関数名 | シグネチャ | 説明 |
|--------|-----------|------|
| `__builtin_array_some_i32` | `bool(ptr, i64, fn_ptr)` | いずれかが条件を満たす |
| `__builtin_array_some_i64` | `bool(ptr, i64, fn_ptr)` | いずれかが条件を満たす |
| `__builtin_array_every_i32` | `bool(ptr, i64, fn_ptr)` | すべてが条件を満たす |
| `__builtin_array_every_i64` | `bool(ptr, i64, fn_ptr)` | すべてが条件を満たす |
| `__builtin_array_findIndex_i32` | `i32(ptr, i64, fn_ptr)` | 条件を満たす最初のインデックス |
| `__builtin_array_findIndex_i64` | `i64(ptr, i64, fn_ptr)` | 条件を満たす最初のインデックス |
| `__builtin_array_find_i32` | `i32(ptr, i64, fn_ptr)` | 条件を満たす最初の要素 |
| `__builtin_array_find_i64` | `i64(ptr, i64, fn_ptr)` | 条件を満たす最初の要素 |

### 5.4 first / last / sortBy
| 関数名 | シグネチャ | 説明 |
|--------|-----------|------|
| `__builtin_array_first_i32` | `i32(ptr, i64)` | 最初の要素 |
| `__builtin_array_first_i64` | `i64(ptr, i64)` | 最初の要素 |
| `__builtin_array_last_i32` | `i32(ptr, i64)` | 最後の要素 |
| `__builtin_array_last_i64` | `i64(ptr, i64)` | 最後の要素 |
| `__builtin_array_sortBy_i32` | `ptr(ptr, i64, fn_ptr)` | カスタム比較でソート |
| `__builtin_array_sortBy_i64` | `ptr(ptr, i64, fn_ptr)` | カスタム比較でソート |

### 5.5 配列スライス
| 関数名 | シグネチャ | 説明 |
|--------|-----------|------|
| `__builtin_array_slice` | `ptr(ptr, i64, i64, i64, i64, ptr)` | 汎用スライス |
| `__builtin_array_slice_i32` | `ptr(ptr, i64, i64, i64, ptr)` | i32配列スライス |
| `__builtin_array_slice_int` | `ptr(ptr, i64, i64, i64, ptr)` | int配列スライス |

---

## 6. メモリ関数

### 6.1 アロケータ
| 関数名 | シグネチャ | 説明 |
|--------|-----------|------|
| `cm_alloc` | `ptr(size_t)` | メモリ確保 |
| `cm_dealloc` | `void(ptr)` | メモリ解放 |
| `cm_realloc` | `ptr(ptr, size_t)` | 再確保 |

### 6.2 メモリ操作
| 関数名 | シグネチャ | 説明 |
|--------|-----------|------|
| `cm_memcpy` | `ptr(ptr, ptr, size_t)` | コピー |
| `cm_memmove` | `ptr(ptr, ptr, size_t)` | 移動(重複対応) |
| `cm_memset` | `ptr(ptr, i32, size_t)` | 埋める |
| `cm_strcmp` | `i32(char*, char*)` | 文字列比較 |
| `cm_strchr` | `char*(char*, i32)` | 文字検索 |
| `cm_strstr` | `char*(char*, char*)` | 部分文字列検索 |

---

## 7. libc関数（直接リンク）

JITでは以下のlibc関数を直接ホストプロセスからリンク：

| 関数名 | 用途 |
|--------|------|
| `printf` | フォーマット出力 |
| `puts` | 文字列出力 |
| `malloc` | メモリ確保 |
| `free` | メモリ解放 |
| `realloc` | 再確保 |
| `memcpy` | コピー |
| `memmove` | 移動 |
| `memset` | 埋める |
| `memcmp` | 比較 |
| `strlen` | 文字列長 |
| `strcpy` | コピー |
| `strncmp` | N文字比較 |
| `strstr` | 部分文字列 |
| `qsort` | ソート |
| `exit` | 終了 |
| `abort` | 異常終了 |

---

## 8. パニック・エラー

| 関数名 | シグネチャ | 説明 |
|--------|-----------|------|
| `cm_panic` | `void(char*)` | パニック発生 |
| `cm_panic_bounds` | `void(i64, i64)` | 境界チェック違反 |
| `cm_assert` | `void(bool, char*)` | アサーション |

---

## 9. ファイルI/O

| 関数名 | シグネチャ | 説明 |
|--------|-----------|------|
| `cm_read_file` | `char*(char*)` | ファイル読み込み |
| `cm_write_file` | `bool(char*, char*)` | ファイル書き込み |
| `cm_read_stdin` | `char*()` | 標準入力読み込み |

---

## 10. 実装コード

### JITでのシンボル登録（抜粋）

```cpp
void JITEngine::registerRuntimeSymbols() {
    auto& mainJD = jit_->getMainJITDylib();
    llvm::orc::SymbolMap symbols;

    // === Print Functions ===
    symbols[mangle("cm_print_string")] = ptr(&cm_print_string);
    symbols[mangle("cm_println_string")] = ptr(&cm_println_string);
    symbols[mangle("cm_print_int")] = ptr(&cm_print_int);
    symbols[mangle("cm_println_int")] = ptr(&cm_println_int);
    symbols[mangle("cm_print_uint")] = ptr(&cm_print_uint);
    symbols[mangle("cm_println_uint")] = ptr(&cm_println_uint);
    symbols[mangle("cm_print_long")] = ptr(&cm_print_long);
    symbols[mangle("cm_println_long")] = ptr(&cm_println_long);
    symbols[mangle("cm_print_ulong")] = ptr(&cm_print_ulong);
    symbols[mangle("cm_println_ulong")] = ptr(&cm_println_ulong);
    symbols[mangle("cm_print_double")] = ptr(&cm_print_double);
    symbols[mangle("cm_println_double")] = ptr(&cm_println_double);
    symbols[mangle("cm_print_float")] = ptr(&cm_print_float);
    symbols[mangle("cm_println_float")] = ptr(&cm_println_float);
    symbols[mangle("cm_print_bool")] = ptr(&cm_print_bool);
    symbols[mangle("cm_println_bool")] = ptr(&cm_println_bool);
    symbols[mangle("cm_print_char")] = ptr(&cm_print_char);
    symbols[mangle("cm_println_char")] = ptr(&cm_println_char);

    // === Format Functions ===
    symbols[mangle("cm_format_int")] = ptr(&cm_format_int);
    symbols[mangle("cm_format_uint")] = ptr(&cm_format_uint);
    symbols[mangle("cm_format_long")] = ptr(&cm_format_long);
    symbols[mangle("cm_format_ulong")] = ptr(&cm_format_ulong);
    symbols[mangle("cm_format_double")] = ptr(&cm_format_double);
    symbols[mangle("cm_format_bool")] = ptr(&cm_format_bool);
    symbols[mangle("cm_format_char")] = ptr(&cm_format_char);
    symbols[mangle("cm_format_ptr")] = ptr(&cm_format_ptr);
    symbols[mangle("cm_format_unescape_braces")] = ptr(&cm_format_unescape_braces);
    symbols[mangle("cm_format_replace_int")] = ptr(&cm_format_replace_int);
    symbols[mangle("cm_format_replace_uint")] = ptr(&cm_format_replace_uint);
    symbols[mangle("cm_format_replace_double")] = ptr(&cm_format_replace_double);
    symbols[mangle("cm_format_replace_string")] = ptr(&cm_format_replace_string);
    symbols[mangle("cm_format_replace_bool")] = ptr(&cm_format_replace_bool);
    symbols[mangle("cm_format_replace_char")] = ptr(&cm_format_replace_char);
    symbols[mangle("cm_format_replace_ptr")] = ptr(&cm_format_replace_ptr);

    // === String Functions ===
    symbols[mangle("cm_string_length")] = ptr(&cm_string_length);
    symbols[mangle("cm_string_concat")] = ptr(&cm_string_concat);
    symbols[mangle("cm_strcmp")] = ptr(&cm_strcmp);
    symbols[mangle("cm_strncmp")] = ptr(&cm_strncmp);
    symbols[mangle("__builtin_string_len")] = ptr(&__builtin_string_len);
    symbols[mangle("__builtin_string_charAt")] = ptr(&__builtin_string_charAt);
    symbols[mangle("__builtin_string_first")] = ptr(&__builtin_string_first);
    symbols[mangle("__builtin_string_last")] = ptr(&__builtin_string_last);
    symbols[mangle("__builtin_string_substring")] = ptr(&__builtin_string_substring);
    symbols[mangle("__builtin_string_indexOf")] = ptr(&__builtin_string_indexOf);
    symbols[mangle("__builtin_string_toUpperCase")] = ptr(&__builtin_string_toUpperCase);
    symbols[mangle("__builtin_string_toLowerCase")] = ptr(&__builtin_string_toLowerCase);
    symbols[mangle("__builtin_string_trim")] = ptr(&__builtin_string_trim);
    symbols[mangle("__builtin_string_startsWith")] = ptr(&__builtin_string_startsWith);
    symbols[mangle("__builtin_string_endsWith")] = ptr(&__builtin_string_endsWith);
    symbols[mangle("__builtin_string_includes")] = ptr(&__builtin_string_includes);
    symbols[mangle("__builtin_string_repeat")] = ptr(&__builtin_string_repeat);

    // === Slice Functions ===
    symbols[mangle("cm_slice_new")] = ptr(&cm_slice_new);
    symbols[mangle("cm_slice_free")] = ptr(&cm_slice_free);
    symbols[mangle("cm_slice_len")] = ptr(&cm_slice_len);
    symbols[mangle("cm_slice_cap")] = ptr(&cm_slice_cap);
    symbols[mangle("cm_slice_elem_size")] = ptr(&cm_slice_elem_size);
    symbols[mangle("cm_slice_push_i8")] = ptr(&cm_slice_push_i8);
    symbols[mangle("cm_slice_push_i32")] = ptr(&cm_slice_push_i32);
    symbols[mangle("cm_slice_push_i64")] = ptr(&cm_slice_push_i64);
    symbols[mangle("cm_slice_push_f64")] = ptr(&cm_slice_push_f64);
    symbols[mangle("cm_slice_push_ptr")] = ptr(&cm_slice_push_ptr);
    symbols[mangle("cm_slice_push_slice")] = ptr(&cm_slice_push_slice);
    symbols[mangle("cm_slice_pop_i8")] = ptr(&cm_slice_pop_i8);
    symbols[mangle("cm_slice_pop_i32")] = ptr(&cm_slice_pop_i32);
    symbols[mangle("cm_slice_pop_i64")] = ptr(&cm_slice_pop_i64);
    symbols[mangle("cm_slice_pop_f64")] = ptr(&cm_slice_pop_f64);
    symbols[mangle("cm_slice_pop_ptr")] = ptr(&cm_slice_pop_ptr);
    symbols[mangle("cm_slice_get_i8")] = ptr(&cm_slice_get_i8);
    symbols[mangle("cm_slice_get_i32")] = ptr(&cm_slice_get_i32);
    symbols[mangle("cm_slice_get_i64")] = ptr(&cm_slice_get_i64);
    symbols[mangle("cm_slice_get_f64")] = ptr(&cm_slice_get_f64);
    symbols[mangle("cm_slice_get_ptr")] = ptr(&cm_slice_get_ptr);
    symbols[mangle("cm_slice_get_element_ptr")] = ptr(&cm_slice_get_element_ptr);
    symbols[mangle("cm_slice_get_subslice")] = ptr(&cm_slice_get_subslice);
    symbols[mangle("cm_slice_first_i32")] = ptr(&cm_slice_first_i32);
    symbols[mangle("cm_slice_first_i64")] = ptr(&cm_slice_first_i64);
    symbols[mangle("cm_slice_last_i32")] = ptr(&cm_slice_last_i32);
    symbols[mangle("cm_slice_last_i64")] = ptr(&cm_slice_last_i64);
    symbols[mangle("cm_slice_first_ptr")] = ptr(&cm_slice_first_ptr);
    symbols[mangle("cm_slice_last_ptr")] = ptr(&cm_slice_last_ptr);
    symbols[mangle("cm_slice_delete")] = ptr(&cm_slice_delete);
    symbols[mangle("cm_slice_clear")] = ptr(&cm_slice_clear);
    symbols[mangle("cm_slice_reverse")] = ptr(&cm_slice_reverse);
    symbols[mangle("cm_slice_sort")] = ptr(&cm_slice_sort);
    symbols[mangle("cm_slice_subslice")] = ptr(&cm_slice_subslice);
    symbols[mangle("cm_array_to_slice")] = ptr(&cm_array_to_slice);
    symbols[mangle("cm_array2d_to_slice2d")] = ptr(&cm_array2d_to_slice2d);
    symbols[mangle("cm_array_equal")] = ptr(&cm_array_equal);
    symbols[mangle("cm_slice_equal")] = ptr(&cm_slice_equal);

    // === Higher-Order Functions ===
    symbols[mangle("__builtin_array_map")] = ptr(&__builtin_array_map);
    symbols[mangle("__builtin_array_map_closure")] = ptr(&__builtin_array_map_closure);
    symbols[mangle("__builtin_array_map_i64")] = ptr(&__builtin_array_map_i64);
    symbols[mangle("__builtin_array_map_i64_closure")] = ptr(&__builtin_array_map_i64_closure);
    symbols[mangle("__builtin_array_filter")] = ptr(&__builtin_array_filter);
    symbols[mangle("__builtin_array_filter_closure")] = ptr(&__builtin_array_filter_closure);
    symbols[mangle("__builtin_array_filter_i64")] = ptr(&__builtin_array_filter_i64);
    symbols[mangle("__builtin_array_filter_i64_closure")] = ptr(&__builtin_array_filter_i64_closure);

    // === Memory Functions ===
    symbols[mangle("cm_alloc")] = ptr(&cm_alloc);
    symbols[mangle("cm_dealloc")] = ptr(&cm_dealloc);
    symbols[mangle("cm_realloc")] = ptr(&cm_realloc);
    symbols[mangle("cm_memcpy")] = ptr(&cm_memcpy);
    symbols[mangle("cm_memmove")] = ptr(&cm_memmove);
    symbols[mangle("cm_memset")] = ptr(&cm_memset);
    symbols[mangle("cm_strchr")] = ptr(&cm_strchr);
    symbols[mangle("cm_strstr")] = ptr(&cm_strstr);
    symbols[mangle("cm_qsort")] = ptr(&cm_qsort);

    // === Panic Functions ===
    symbols[mangle("cm_panic")] = ptr(&cm_panic);
    symbols[mangle("cm_panic_bounds")] = ptr(&cm_panic_bounds);

    // === File I/O ===
    symbols[mangle("cm_read_file")] = ptr(&cm_read_file);
    symbols[mangle("cm_write_file")] = ptr(&cm_write_file);
    symbols[mangle("cm_read_stdin")] = ptr(&cm_read_stdin);

    // === libc (via DynamicLibrarySearchGenerator) ===
    // printf, puts, malloc, free, etc. are auto-resolved

    mainJD.define(llvm::orc::absoluteSymbols(std::move(symbols)));
}

// ヘルパーマクロ
#define mangle(name) jit_->mangleAndIntern(name)
#define ptr(fn) llvm::JITEvaluatedSymbol::fromPointer(fn)
```

---

## 11. ランタイムライブラリリンク方法

JITでは2つの方法でランタイムをリンクできます：

### 方法1: 静的リンク（推奨）
ランタイムライブラリをコンパイラに静的リンクし、関数ポインタを直接登録。

```cpp
// CMakeLists.txt
target_link_libraries(cm PRIVATE cm_runtime)
```

### 方法2: 動的ライブラリ
ランタイムを共有ライブラリとしてロード。

```cpp
auto generator = llvm::orc::DynamicLibrarySearchGenerator::Load(
    "libcm_runtime.so", jit_->getDataLayout().getGlobalPrefix());
mainJD.addGenerator(std::move(*generator));
```

---

## 12. チェックリスト

実装時は以下を確認：

- [ ] 全Print関数登録
- [ ] 全Format関数登録
- [ ] 全String関数登録
- [ ] 全Slice関数登録
- [ ] 全Higher-Order関数登録
- [ ] 全Memory関数登録
- [ ] Panic関数登録
- [ ] File I/O関数登録（必要に応じて）
- [ ] libcシンボル解決設定
