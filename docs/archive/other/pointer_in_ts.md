[English](pointer_in_ts.en.html)

# TypeScriptにおけるポインタ表現

## 概要

TypeScript/JavaScriptは直接的なメモリアクセスを持たないため、ポインタのセマンティクスをエミュレートする必要がある。

## 実装戦略

### 1. Boxed Reference Pattern

```typescript
// Cmコード
int* ptr = &value;
*ptr = 42;

// TypeScript変換
class Ptr<T> {
  constructor(private container: { value: T }) {}

  deref(): T {
    return this.container.value;
  }

  assign(val: T): void {
    this.container.value = val;
  }

  // アドレス演算のエミュレート
  offset(n: number): Ptr<T> {
    // 配列の場合のみ有効
    throw new Error("Pointer arithmetic not supported for single values");
  }
}

let ptr = new Ptr({ value: value });
ptr.assign(42);
```

### 2. Array-based Pointer System

```typescript
// メモリプールをエミュレート
class MemoryPool {
  private static memory: Map<number, any> = new Map();
  private static nextAddr = 0x1000;

  static alloc<T>(value: T): number {
    const addr = this.nextAddr++;
    this.memory.set(addr, value);
    return addr;
  }

  static read<T>(addr: number): T {
    if (!this.memory.has(addr)) {
      throw new Error(`Segmentation fault: ${addr}`);
    }
    return this.memory.get(addr);
  }

  static write<T>(addr: number, value: T): void {
    if (!this.memory.has(addr)) {
      throw new Error(`Segmentation fault: ${addr}`);
    }
    this.memory.set(addr, value);
  }
}

// Cmコード: int* p = malloc(sizeof(int));
// TypeScript:
let p: number = MemoryPool.alloc(0);
```

### 3. Proxy-based Smart Pointers

```typescript
class SmartPtr<T extends object> {
  private target: T;
  private isValid = true;

  constructor(target: T) {
    this.target = new Proxy(target, {
      get: (obj, prop) => {
        if (!this.isValid) throw new Error("Use after free");
        return obj[prop];
      },
      set: (obj, prop, value) => {
        if (!this.isValid) throw new Error("Use after free");
        obj[prop] = value;
        return true;
      }
    });
  }

  deref(): T { return this.target; }

  free(): void {
    this.isValid = false;
  }
}
```

## 配列とポインタ演算

```typescript
// Cmコード
int arr[10];
int* p = arr;
p += 5;
*p = 100;

// TypeScript変換
class ArrayPtr<T> {
  constructor(
    private array: T[],
    private offset: number = 0
  ) {}

  add(n: number): ArrayPtr<T> {
    return new ArrayPtr(this.array, this.offset + n);
  }

  deref(): T {
    if (this.offset < 0 || this.offset >= this.array.length) {
      throw new Error("Array bounds violation");
    }
    return this.array[this.offset];
  }

  assign(value: T): void {
    if (this.offset < 0 || this.offset >= this.array.length) {
      throw new Error("Array bounds violation");
    }
    this.array[this.offset] = value;
  }
}

let arr = new Array(10);
let p = new ArrayPtr(arr, 0);
p = p.add(5);
p.assign(100);
```

## 関数ポインタ

```typescript
// Cmコード
int (*fp)(int, int) = &add;
int result = fp(1, 2);

// TypeScript変換
type FuncPtr<T extends Function> = {
  invoke: T;
  address: number;  // シミュレート用
};

let fp: FuncPtr<(a: number, b: number) => number> = {
  invoke: add,
  address: 0x2000  // 仮想アドレス
};
let result = fp.invoke(1, 2);
```

## Null安全性

```typescript
class SafePtr<T> {
  constructor(private value: T | null) {}

  deref(): T {
    if (this.value === null) {
      throw new Error("Null pointer dereference");
    }
    return this.value;
  }

  isNull(): boolean {
    return this.value === null;
  }
}
```

## WebAssembly連携時の本物のポインタ

```typescript
// WASMメモリと連携する場合
class WasmPtr {
  constructor(
    private memory: WebAssembly.Memory,
    private offset: number
  ) {}

  readInt32(): number {
    const view = new Int32Array(this.memory.buffer);
    return view[this.offset / 4];
  }

  writeInt32(value: number): void {
    const view = new Int32Array(this.memory.buffer);
    view[this.offset / 4] = value;
  }

  add(bytes: number): WasmPtr {
    return new WasmPtr(this.memory, this.offset + bytes);
  }
}
```

## 推奨アプローチ

1. **単純な参照**: Boxed Reference Pattern
2. **配列操作**: ArrayPtr with bounds checking
3. **WASM統合時**: WasmPtr for real memory access
4. **デバッグビルド**: 全ポインタ操作を追跡
5. **リリースビルド**: 最適化されたアクセス

この設計により、TypeScriptでも安全にポインタセマンティクスを表現できる。