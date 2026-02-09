---
title: Threads
parent: Advanced
nav_order: 10
---

# Threads (std::thread)

Cm uses the `std::thread` module for multi-threaded programming.

## Basic Usage

### Creating and Joining Threads

```cm
import std::thread::spawn;
import std::thread::join;
import std::thread::sleep_ms;

void* worker(void* arg) {
    println("Worker: started");
    sleep_ms(100);
    println("Worker: done");
    return 42 as void*;
}

int main() {
    // Spawn a thread
    ulong t = spawn(worker as void*);
    
    // Wait for completion and get result
    int result = join(t);
    println("Result: {result}");  // Result: 42
    
    return 0;
}
```

## API Reference

### spawn

```cm
ulong spawn(void* fn);
```

Creates a new thread.

- **Parameter**: `fn` - Function to execute (`void* fn(void*)` type)
- **Returns**: Thread handle

### join

```cm
int join(ulong handle);
```

Waits for thread completion and retrieves the result.

- **Parameter**: `handle` - Thread handle from `spawn`
- **Returns**: Return value from thread function (int)

### detach

```cm
void detach(ulong handle);
```

Detaches a thread. Detached threads run in the background and are terminated when the main thread exits.

### sleep_ms

```cm
void sleep_ms(int ms);
```

Suspends the current thread for the specified milliseconds.

## Parallel Processing Example

```cm
import std::thread::spawn;
import std::thread::join;
import std::thread::sleep_ms;

void* calc10(void* arg) {
    sleep_ms(50);
    return 10 as void*;
}

void* calc20(void* arg) {
    sleep_ms(50);
    return 20 as void*;
}

int main() {
    // Spawn threads in parallel
    ulong t1 = spawn(calc10 as void*);
    ulong t2 = spawn(calc20 as void*);
    
    // Wait for both to complete
    int r1 = join(t1);
    int r2 = join(t2);
    
    println("Total: {r1 + r2}");  // Total: 30
    
    return 0;
}
```

## join vs detach

| Operation | Behavior |
|-----------|----------|
| `join(t)` | Waits for completion, retrieves result |
| `detach(t)` | Runs in background, result not retrievable |

### detach Caveats

Detached threads are forcefully terminated when the main thread exits:

```cm
void* long_task(void* arg) {
    // Takes 1 second
    sleep_ms(1000);
    println("Done");  // Won't print if main exits first
    return 0 as void*;
}

int main() {
    ulong t = spawn(long_task as void*);
    detach(t);
    
    // Main exits after 100ms
    sleep_ms(100);
    // → long_task is forcefully terminated
    
    return 0;
}
```

## Internal Implementation

`std::thread` directly uses POSIX `pthread`:

- `spawn` → `pthread_create`
- `join` → `pthread_join`
- `detach` → `pthread_detach`
- `sleep_ms` → `usleep`

---

**Last Updated:** 2026-02-08
