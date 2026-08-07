---
title: std::mem
---

# std::mem - Memory Management

Provides memory allocation, deallocation, and type information.

> **Supported backends:** Native (LLVM) only

---

## Allocation / Deallocation

```cm
import std::mem::{alloc, dealloc};

void* ptr = alloc(1024);   // allocate 1024 bytes
// ... use ptr ...
dealloc(ptr);               // free
```

---

## Type Information

```cm
import std::mem::{size_of, type_name, align_of};

uint s = size_of<int>();       // 4
string n = type_name<int>();   // "int"
uint a = align_of<double>();   // 8
```

| Function | Returns | Description |
|----------|---------|-------------|
| `size_of<T>()` | `uint` | Size of type T in bytes |
| `type_name<T>()` | `string` | Name of type T |
| `align_of<T>()` | `uint` | Alignment of type T |

---

## Allocator Interface

Custom allocators can be implemented.

```cm
import std::mem::{Allocator, DefaultAllocator};

// default allocator (malloc-based)
DefaultAllocator da = DefaultAllocator{};
void* ptr = da.alloc(256);
da.dealloc(ptr);
```

| Method | Returns | Description |
|--------|---------|-------------|
| `alloc(size)` | `void*` | Allocate memory |
| `dealloc(ptr)` | `void` | Free memory |
| `reallocate(ptr, new_size)` | `void*` | Reallocate |
| `alloc_zeroed(size)` | `void*` | Zero-initialized allocation |

---

## Replacing the Global Allocator

Passing three Cm function pointers (alloc / dealloc / realloc) to `set_allocator_fns` routes all subsequent `std::mem` allocations through that allocator.
The internal allocations of `std::collections` (Vector/HashMap/Queue) also go through `std::mem`, so they use the registered allocator as well (v0.17.0 removed the raw-malloc bypass).
`reset_allocator()` restores the default allocator.

> **Supported backends:** JIT / Native only (WASM uses its own fixed free-list allocator so the facade is a no-op; JS/TS are GC-managed and out of scope)

```cm
import std::mem::{alloc, dealloc, set_allocator_fns, reset_allocator};

use libc {
    void* malloc(int size);
    void free(void* ptr);
    void* realloc(void* ptr, int size);
}

int alloc_count = 0;

// counting allocator: records the number of allocations and delegates to malloc
void* counting_alloc(long size) {
    alloc_count = alloc_count + 1;
    return malloc(size as int);
}

void counting_dealloc(void* ptr) {
    free(ptr);
}

void* counting_realloc(void* ptr, long new_size) {
    return realloc(ptr, new_size as int);
}

int main() {
    set_allocator_fns(counting_alloc as void*, counting_dealloc as void*, counting_realloc as void*);
    void* p = alloc(64);     // goes through counting_alloc (alloc_count == 1)
    dealloc(p);
    reset_allocator();       // back to the default allocator
    return 0;
}
```

| Function | Description |
|----------|-------------|
| `set_allocator_fns(alloc_fn, dealloc_fn, realloc_fn)` | Replace the global allocator with three Cm function pointers |
| `reset_allocator()` | Restore the default allocator |

The registered functions must have the signatures `void*(long)` / `void(void*)` / `void*(void*, long)`.

---

## libc FFI

libc functions available internally:

```cm
use libc {
    void* malloc(int size);
    void* calloc(int nmemb, int size);
    void* realloc(void* ptr, int size);
    void free(void* ptr);
}
```
