# Cm言語 統合メモリ安全性設計

## 概要

Cm言語は、低レベルシステムプログラミングからWebフロントエンドまでをカバーするため、プラットフォームごとに異なるメモリ安全性戦略を採用する。

## プラットフォーム別メモリモデル

### 1. Native (Rust/LLVM Backend)

```c
// 実際のメモリアドレスを使用
int* ptr = &value;
*ptr = 42;

// Pin保証
pinned struct Node {
    struct Node* next;
    int value;
};
```

### 2. TypeScript Backend

```typescript
// ポインタエミュレーション
class CmPtr<T> {
    private pinned: boolean = false;
    private frozen: boolean = false;

    constructor(private ref: { value: T }) {}

    pin(): void {
        this.pinned = true;
        Object.freeze(this.ref); // 一部の保護
    }

    deref(): T {
        this.checkAccess();
        return this.ref.value;
    }

    private checkAccess(): void {
        if (this.frozen) throw new Error("Access to frozen memory");
    }
}
```

### 3. WASM Backend

```c
// Linear Memoryと統合
@[wasm_memory]
struct WasmPtr {
    uint32_t offset;

    template<typename T>
    T read() {
        return wasm_memory_read(this->offset, sizeof(T));
    }

    template<typename T>
    void write(T value) {
        if (is_pinned(this->offset)) {
            trap("Cannot write to pinned memory");
        }
        wasm_memory_write(this->offset, value);
    }
};
```

## 統一メモリ安全性API

```cpp
// すべてのバックエンドで動作する統一API
namespace memory {
    // スマートポインタ
    template<typename T>
    class UniquePtr {
        // Native: std::unique_ptr
        // TypeScript: 単一所有権エミュレーション
        // WASM: Linear memory管理
    };

    template<typename T>
    class SharedPtr {
        // Native: std::shared_ptr (参照カウント)
        // TypeScript: WeakMap for cycle prevention
        // WASM: Reference counting in linear memory
    };

    // Pin API
    template<typename T>
    class Pin {
        #if BACKEND == NATIVE
            // Hardware-assisted pinning
            T* ptr;
            MpxBounds bounds;
        #elif BACKEND == TYPESCRIPT
            // Software emulation
            CmPtr<T> ptr;
            readonly address: symbol;
        #elif BACKEND == WASM
            // WASM memory protection
            uint32_t offset;
            bool pinned;
        #endif

        static Pin<T> new(T value);
        T& deref();
        T& deref_mut(); // requires(!pinned)
    };
}
```

## メモリライフサイクル管理

```c
// ライフタイムアノテーション（全バックエンド）
template<lifetime 'a>
Field& borrow(Data& data) {
    return data.field;
}

// 所有権の移動
void take_ownership(Data data) {
    // Dataの所有権が移動
}

// RAII パターン
struct Guard {
    Resource resource;

    Guard() {
        this->resource = acquire_resource();
    }

    ~Guard() {
        release_resource(this->resource);
    }
};
```

## バックエンド間の相互運用性

```c
// FFI境界でのポインタ変換
@[ffi_boundary]
JsHandle export_to_js(const Data* ptr) {
    // Native pointer -> JS handle
    JsHandle handle = allocate_handle();
    HANDLE_MAP_insert(handle, ptr);
    return handle;
}

@[wasm_export]
int32_t wasm_access(uint32_t offset) {
    // WASM offset -> actual access
    int* ptr = validate_wasm_ptr(offset);
    return *ptr;
}
```

## セキュリティ機能

### 1. Use-After-Free 防止

```c
// Temporal Safety
template<typename T>
struct TemporalPtr {
    T* ptr;
    uint64_t generation;  // 世代番号

    T& deref() {
        if (!is_valid_generation(this->ptr, this->generation)) {
            panic("Use after free detected");
        }
        return *this->ptr;
    }
};
```

### 2. Buffer Overflow 防止

```c
// Spatial Safety
template<typename T>
struct BoundedPtr {
    T* ptr;
    T* lower;
    T* upper;

    BoundedPtr offset(intptr_t n) {
        T* new_ptr = this->ptr + n;
        if (new_ptr < this->lower || new_ptr >= this->upper) {
            panic("Buffer overflow detected");
        }
        return BoundedPtr{new_ptr, this->lower, this->upper};
    }
};
```

### 3. Data Race 防止

```c
// Concurrency Safety
template<typename T>
struct SyncPtr {
    T* ptr;
    Mutex lock;

    template<typename R, typename F>
    R access(F func) {
        MutexGuard guard(this->lock);
        return func(this->ptr);
    }
};
```

## パフォーマンス最適化

```c
// Debug vs Release
#ifdef DEBUG
    template<typename T>
    using SafePtr = CheckedPtr<T>;  // 全チェック有効
#else
    template<typename T>
    using SafePtr = FastPtr<T>;  // 最小限のチェック
#endif

// プロファイルベースの最適化
@[hot_path]
void critical_function(const Data* ptr) {
    // インライン化とチェック省略
    unsafe { /* fast path */ }
}
```

## 静的解析統合

```c
// Cm Analyzer annotations
@[analyzer::non_null]
void process(const Data* ptr) {
    // Analyzerがnullチェック不要と判断
    ptr->field.access();
}

@[analyzer::lifetime("a > b")]
template<lifetime 'a, lifetime 'b>
void extend_lifetime(Data& x, Handle& y) {
    // ライフタイム関係の静的検証
}
```

## デバッグツール

```c
// Memory Sanitizer統合
#ifdef SANITIZE
void debug_access(Data* ptr) {
    __msan_check_mem_is_initialized(ptr, sizeof(Data));
    // ASan, TSan, MSanとの統合
}
#endif

// Valgrind annotations
@[valgrind_annotation]
void mark_memory_defined(uint8_t* ptr, size_t size) {
    VALGRIND_MAKE_MEM_DEFINED(ptr, size);
}
```

## 実装ロードマップ

| フェーズ | 機能 | Native | TypeScript | WASM |
|---------|------|--------|------------|------|
| Phase 1 | 基本ポインタ | ✅ | ✅ | ✅ |
| Phase 2 | スマートポインタ | ✅ | ✅ | ⬜ |
| Phase 3 | Pin API | ✅ | ⬜ | ⬜ |
| Phase 4 | ライフタイム | ⬜ | ⬜ | ⬜ |
| Phase 5 | 静的解析 | ⬜ | ⬜ | ⬜ |

## まとめ

Cmのメモリ安全性は：
1. **プラットフォーム適応型**: 各環境に最適な実装
2. **段階的保証**: デバッグで厳密、リリースで高速
3. **ツール統合**: 既存ツールとの相互運用
4. **将来拡張可能**: 新しい安全性機能の追加が容易

これにより、システムプログラミングからWeb開発まで、一貫したメモリ安全性を提供する。