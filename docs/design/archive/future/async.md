[English](async.en.html)

# 非同期処理 (async/await) 設計

## 概要

Cmのasync/awaitは **Future型** をベースにし、他言語のランタイムと互換性を持たせます。

## 基本構文

```cpp
// Cm
async String fetchData(String url) {
    Response res = await http::get(url);
    return res.body;
}

// 戻り値型は暗黙的に Future<T> でラップ
// 実際の型: Future<String>
```

## Future型

```cpp
// Cm - Future定義
struct Future<T> {
    enum State { Pending, Ready, Error };
    State state;
    Option<T> value;
    Option<Error> error;
    
    // ポーリング（ランタイムが呼び出し）
    State poll();
    
    // 結果取得
    T get();
}
```

## 各ランタイムへの変換

### Rust (tokio)

```cpp
// Cm
async String fetch(String url) {
    return await http::get(url);
}

// → Rust
async fn fetch(url: String) -> String {
    http::get(&url).await
}
// Future<String> → impl Future<Output = String>
```

### TypeScript

```cpp
// Cm
async String fetch(String url) {
    return await http::get(url);
}

// → TypeScript
async function fetch(url: string): Promise<string> {
    return await http.get(url);
}
// Future<String> → Promise<string>
```

### インタプリタ

```cpp
// 内部実装
class AsyncRuntime {
    std::queue<Future<Value>> pending;
    
    void run_until_complete() {
        while (!pending.empty()) {
            auto& fut = pending.front();
            if (fut.poll() == State::Ready) {
                pending.pop();
            }
        }
    }
};
```

## ランタイム互換性

| Cm | Rust | TypeScript | C++ |
|----|------|------------|-----|
| `Future<T>` | `impl Future<Output=T>` | `Promise<T>` | `std::future<T>` |
| `await` | `.await` | `await` | `.get()` |
| `async fn` | `async fn` | `async function` | `std::async` |

## 並行処理

```cpp
// Cm - 複数のFutureを並行実行
async void fetchAll() {
    Future<String> f1 = fetch("url1");
    Future<String> f2 = fetch("url2");
    
    // 両方完了を待つ
    (String r1, String r2) = await all(f1, f2);
    
    // 最初の完了を待つ
    String first = await race(f1, f2);
}
```

## 制限事項

### Bare-metal での async

Bare-metalでもasync/await構文は使用可能です。ただし、**カスタムエグゼキューター**が必要です。

```cpp
// Cm (baremetal)
import cm_core::async;

// カスタムエグゼキューター
struct SimpleExecutor {
    Queue<Future<void>> tasks;
    
    void spawn(Future<void> task) {
        this.tasks.push(task);
    }
    
    void run() {
        while (!this.tasks.empty()) {
            auto task = this.tasks.pop();
            if (task.poll() == State::Pending) {
                this.tasks.push(task);  // 再キュー
            }
            yield();  // 協調的マルチタスク
        }
    }
}

// 使用例
async void task1() {
    // 非同期処理
    await sleep_ticks(100);
}

void kernel_main() {
    SimpleExecutor executor;
    executor.spawn(task1());
    executor.run();
}
```

### ランタイム対応表

| ターゲット | async構文 | ランタイム | 備考 |
|-----------|----------|-----------|------|
| native | ✅ | tokio | 標準 |
| web | ✅ | JS Promise | 自動 |
| baremetal | ✅ | **カスタム** | 自作必要 |