[English](memory_pinning.en.html)

# Cm言語のメモリピンニング機構設計

## 背景

Rust's `Pin<T>`の問題点：
1. `PhantomPinned`は単なるマーカー型で、実際の保護機能はない
2. `unsafe`コードでポインタ経由の書き換えが可能
3. コンパイル時チェックのみで、ランタイム保護がない

## Cmの拡張Pin設計

### 1. Hardware-Assisted Pin (HAP)

```c
// Intel MPX (Memory Protection Extensions) を活用
@[hardware_protected]
pinned struct SelfReferential {
    int value;
    int* ptr;  // selfを指す

    // コンパイラが自動生成
    struct {
        void* lower;
        void* upper;
    } __mpx_bounds;
};

// 使用例
pinned SelfReferential* data = SelfReferential_new();
// data のアドレスは MPX によって保護される
// 不正なポインタアクセスは CPU レベルで検出
```

### 2. Memory Region Protection

```c
// メモリ領域を3つのカテゴリに分類
enum MemoryRegion {
    Movable,      // 通常のヒープ
    Pinned,       // ピン留めされた領域
    Frozen        // 完全に不変な領域
};

// ピン留め領域の作成
MyStruct* data = alloc_pinned(MyStruct, size);
// この領域へのポインタは特別な型を持つ
PinnedPtr<MyStruct>* ptr = get_pinned_ptr(data);

// コンパイル時チェック
void move_value(MyStruct x) { /* ... */ }  // OK
void move_pinned(pinned MyStruct x) { /* ... */ }  // コンパイルエラー

// ランタイムチェック
// すべてのポインタアクセスは領域チェックを通る
void write_memory(uintptr_t addr, uint8_t value) {
    if (is_in_pinned_region(addr)) {
        panic("Cannot modify pinned memory");
    }
    unsafe {
        *((uint8_t*)addr) = value;
    }
}
```

### 3. Cryptographic Memory Sealing

```c
// 暗号学的にメモリを封印
sealed struct CriticalData {
private:
    uint8_t key[32];
    string data;

    // コンパイラが自動生成
    uint8_t __seal_hash[32];  // SHA-256 of memory content
    uint64_t __seal_nonce;     // Prevent replay attacks

public:
    // 封印されたメモリの作成
    static sealed CriticalData* seal(string data) {
        CriticalData* instance = new CriticalData();
        instance->data = data;
        sha256(instance, instance->__seal_hash);
        memory_protect(instance, sizeof(CriticalData));
        return instance;
    }

    // 検証付きアクセス
    Result<void, TamperError> unseal() {
        uint8_t current_hash[32];
        sha256(this, current_hash);
        if (memcmp(current_hash, this->__seal_hash, 32) != 0) {
            return Result::Err(TamperError::MemoryTampered);
        }
        return Result::Ok();
    }
};
```

### 4. Capability-Based Pin System

```c
// ケイパビリティベースのアクセス制御
struct PinCap {
    bool can_read;
    bool can_write;
    bool can_move;
    Timestamp valid_until;
};

pinned struct SecureData {
    int value;

    // アクセスにはケイパビリティが必要
    int read(const PinCap* cap) {
        if (!cap->can_read || is_expired(cap)) {
            panic("Invalid capability");
        }
        return this->value;
    }

    void write(PinCap* cap, int val) {
        if (!cap->can_write || is_expired(cap)) {
            panic("Invalid capability");
        }
        this->value = val;
        cap->can_write = false; // One-time write
    }
};

// 使用例
SecureData* data = SecureData_new();
PinCap* read_cap = PinCap_read_only();
PinCap* write_cap = PinCap_write_once();

data->read(read_cap);     // OK
data->write(write_cap, 42); // OK
data->write(write_cap, 43); // パニック: already used
```

### 5. Type-State Pin Pattern

```c
// 型状態でピン状態を追跡
template<typename State>
struct Data {
    int value;
    // State is a phantom type parameter
};

// 状態定義
struct Unpinned {};
struct Pinned {};
struct Frozen {};

// Unpinned状態の操作
Data<Pinned>* pin(Data<Unpinned>* self) {
    // メモリをピン留め
    pin_memory(self);
    Data<Pinned>* pinned = (Data<Pinned>*)self;
    return pinned;
}

// Pinned状態の操作
Data<Frozen>* freeze(Data<Pinned>* self) {
    // メモリを完全に不変化
    freeze_memory(self);
    Data<Frozen>* frozen = (Data<Frozen>*)self;
    return frozen;
}

// Pinnedでのみ可能な操作
const int* get_stable_address(Data<Pinned>* self) {
    return &self->value;
}

// Frozenでは読み取りのみ
int read(const Data<Frozen>* self) {
    return self->value;
}
// write関数は定義されない（コンパイルエラー）
```

### 6. Shadow Memory Tracking

```c
// シャドウメモリでピン状態を追跡
struct PinTracker {
    // 各アドレスのピン状態を記録
    HashMap<uintptr_t, PinState>* shadow_map;
};

enum PinState {
    NotPinned,
    Pinned,
    DoublePinned  // エラー状態
};

struct PinnedInfo {
    Timestamp since;
    ThreadId by;
    StackFrame* stack_trace;
    int trace_size;
};

// すべてのメモリアクセスをインターセプト
@[memory_hook]
void on_memory_access(uintptr_t addr, AccessKind kind) {
    PinState* state = PIN_TRACKER_get(addr);
    if (state != NULL) {
        if (*state == PinState::Pinned && kind == AccessKind::Move) {
            panic("Attempted to move pinned memory at 0x%lx", addr);
        }
    }
}
```

### 7. Contract-Based Pinning

```c
// 契約ベースのピンニング
@[contract(
    requires: "!is_pinned(self)",
    ensures: "is_pinned(self) && address(self) == old_address",
    invariant: "is_pinned(self) implies address(self) == initial_address"
)]
void pin(void* self) {
    mark_as_pinned(self);
}

// 契約違反は静的解析とランタイムチェックの両方で検出
```

## 実装優先度

1. **Phase 1**: Type-State Pattern（型安全性重視）
2. **Phase 2**: Memory Region Protection（基本的な保護）
3. **Phase 3**: Shadow Memory Tracking（デバッグ支援）
4. **Phase 4**: Capability System（細かい制御）
5. **Phase 5**: Hardware-Assisted（パフォーマンス）

## まとめ

Cmのピンニングシステムは、Rustの`Pin`の弱点を以下で克服：

1. **多層防御**: コンパイル時、リンク時、ランタイムの3段階チェック
2. **ハードウェア支援**: 可能な場合はCPU機能を活用
3. **暗号学的保護**: 改ざん検出機能
4. **型システム統合**: 状態遷移を型で表現
5. **デバッグ支援**: 問題発生時の詳細なトレース

これにより、真に安全なメモリピンニングを実現する。