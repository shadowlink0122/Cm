# v0.13.0 言語拡張設計

## 概要

v0.13.0では以下の基盤拡張を行い、並行処理ライブラリを実現する：

1. **型付きマクロ** - 型安全なマクロシステム
2. **ブロック引数** - 関数にブロック/コールバックを渡す構文
3. **Result型拡張** - ok()/err() メソッドとmatch対応
4. **並行処理ライブラリ** - std::sync モジュール

---

## 1. 型付きマクロ

### 構文

```cm
// 基本構文
macro <type> NAME = value;

// エクスポート
export macro <type> NAME = value;

// 型なし（void）
macro NAME = value;

// 関数マクロ
macro <return_type> NAME(params...) {
    return expr;
}
```

### 例

```cm
macro int VERSION = 13;
macro string APP_NAME = "MyApp";
macro float PI = 3.14159;

// 型チェックが有効
int x = VERSION;      // OK
int y = APP_NAME;     // エラー: string を int に代入できません
```

### 実装

```cpp
// parser.hpp
struct MacroDef {
    std::string name;
    TypePtr type;                    // NEW: マクロの型
    std::vector<std::string> params;
    std::vector<Token> body;
    bool is_exported = false;
};
```

---

## 2. ブロック引数（コールバック関数）

### 目的

関数にコードブロックを渡して、コールバックや遅延実行を実現する。

### 関数ポインタ型の構文（現行維持）

```cm
// 引数なし、戻り値なし
void*() callback;

// 引数あり
void*(int, int) operation;

// 戻り値あり
int*() getter;
int*(int) transform;

// 完全な形式
R*(T1, T2, ...) func_ptr;
```

### ブロック引数を取る関数

```cm
// ブロック引数を取る関数の定義
void with_lock(Mutex<T> mutex, void*() body) {
    MutexGuard guard = mutex.lock();
    body();
    // guard は自動解放
}

// 呼び出し
with_lock(mutex, () => {
    // 排他処理
    counter += 1;
});
```

### クロージャ構文

```cm
// 引数なしクロージャ
|| { 
    println("hello"); 
}

// 引数ありクロージャ
|int x, int y| { 
    return x + y; 
}

// 型推論されたクロージャ
|x, y| { return x + y; }
```

### 実装変更

1. **AST拡張**
   - `FunctionPointerType` ノード追加
   - クロージャリテラル `|| { ... }` のパース

2. **型システム**
   - 関数ポインタ型の型チェック
   - クロージャのキャプチャ推論

---

## 3. Result型とenum拡張

### 目的

実行時エラーを例外ではなくResult型で処理し、型安全なエラーハンドリングを実現。

### Result型定義

```cm
// std/core/result.cm
enum Result<T, E> {
    Ok(T),
    Err(E)
}

impl<T, E> Result<T, E> {
    // 成功かどうか
    bool is_ok() {
        match this {
            Ok(_) => return true;
            Err(_) => return false;
        }
    }

    // 失敗かどうか
    bool is_err() {
        return !this.is_ok();
    }

    // 値を取得（失敗時はpanic）
    T unwrap() {
        match this {
            Ok(v) => return v;
            Err(e) => panic("Called unwrap on Err value");
        }
    }

    // デフォルト値で取得
    T unwrap_or(T default_val) {
        match this {
            Ok(v) => return v;
            Err(_) => return default_val;
        }
    }

    // エラーを取得
    E unwrap_err() {
        match this {
            Ok(_) => panic("Called unwrap_err on Ok value");
            Err(e) => return e;
        }
    }

    // マップ変換
    Result<U, E> map<U>(U*(T) f) {
        match this {
            Ok(v) => return Ok(f(v));
            Err(e) => return Err(e);
        }
    }

    // エラー変換
    Result<T, F> map_err<F>(F*(E) f) {
        match this {
            Ok(v) => return Ok(v);
            Err(e) => return Err(f(e));
        }
    }
}
```

### コンストラクタ関数

```cm
// ヘルパー関数（グローバル）
Result<T, E> ok<T, E>(T value) {
    return Result.Ok(value);
}

Result<T, E> err<T, E>(E error) {
    return Result.Err(error);
}
```

### 使用例

```cm
import std::core::Result;
import std::io::File;

Result<string, Error> read_file(string path) {
    File file = File.open(path);
    if (file.is_null()) {
        return err(Error.new("Failed to open file"));
    }
    
    string content = file.read_all();
    file.close();
    
    return ok(content);
}

int main() {
    Result<string, Error> result = read_file("config.txt");
    
    match result {
        Ok(content) => {
            println("Config: " + content);
        }
        Err(e) => {
            println("Error: " + e.message());
        }
    }
    
    // または
    if (result.is_ok()) {
        string content = result.unwrap();
        println(content);
    }
    
    // デフォルト値
    string content = result.unwrap_or("default config");
    
    return 0;
}
```

### enum値にデータを持たせる（Associated Data）

```cm
// 現在
enum Status {
    Ok,
    Error
}

// 拡張後（Associated Data）
enum Status {
    Ok(int code),           // データ付きバリアント
    Error(string message),
    Pending
}

// 使用
Status s = Status.Ok(200);

match s {
    Ok(code) => println("Code: " + code);
    Error(msg) => println("Error: " + msg);
    Pending => println("Pending...");
}
```

### 実装変更

1. **AST拡張**
   - `EnumVariant` に `fields` 追加
   - `MatchArm` でパターン分解対応

2. **型システム**
   - ジェネリックenum対応
   - バリアントのコンストラクタ型推論

---

## 4. 並行処理ライブラリ（std::sync）

### モジュール構成

```
std/sync/
├── mod.cm          # メインモジュール
├── thread.cm       # スレッド管理
├── mutex.cm        # 排他制御
├── rwlock.cm       # 読み書きロック
├── channel.cm      # チャネル通信
├── atomic.cm       # アトミック操作
├── future.cm       # 非同期結果
└── task.cm         # タスク抽象化
```

### 4.1 Thread

```cm
// std/sync/thread.cm
struct Thread<T> {
    // 内部ハンドル
}

impl<T> Thread<T> {
    // スレッド生成
    static Thread<T> spawn(T*() work) {
        // 内部実装
    }
    
    // 完了待機と結果取得
    Result<T, ThreadError> join() {
        // ブロッキング待機
    }
    
    // 実行中かどうか
    bool is_running() { ... }
    
    // キャンセル要求
    void cancel() { ... }
}

// マクロで簡潔な構文
export macro Thread<T> spawn_async(body) {
    return Thread.spawn(|| { body });
}
```

### 4.2 Mutex

```cm
// std/sync/mutex.cm
struct Mutex<T> {
    // 内部データとロック
}

struct MutexGuard<T> {
    // RAIIガード
}

impl<T> Mutex<T> {
    static Mutex<T> new(T value) { ... }
    
    Result<MutexGuard<T>, LockError> lock() { ... }
    Result<MutexGuard<T>, LockError> try_lock() { ... }
}

impl<T> MutexGuard<T> {
    // デリファレンスで値にアクセス
    T* deref() { ... }
    T* deref_mut() { ... }
}

// マクロ
export macro void with_lock(mutex, body) {
    Result<MutexGuard, LockError> guard_result = mutex.lock();
    match guard_result {
        Ok(guard) => {
            body;
            // guard は自動解放
        }
        Err(e) => {
            panic("Lock failed: " + e.message());
        }
    }
}
```

### 4.3 Channel

```cm
// std/sync/channel.cm
struct Channel<T> { ... }
struct Sender<T> { ... }
struct Receiver<T> { ... }

impl<T> Channel<T> {
    static Channel<T> new() { ... }
    static Channel<T> bounded(int capacity) { ... }
    
    Sender<T> sender() { ... }
    Receiver<T> receiver() { ... }
}

impl<T> Sender<T> {
    Result<void, SendError> send(T value) { ... }
    bool is_closed() { ... }
}

impl<T> Receiver<T> {
    Result<T, RecvError> recv() { ... }           // ブロッキング
    Result<T, RecvError> try_recv() { ... }       // 非ブロッキング
    Result<T, RecvError> recv_timeout(Duration d) { ... }
}
```

### 4.4 Future/Promise

```cm
// std/sync/future.cm
struct Future<T> { ... }
struct Promise<T> { ... }

impl<T> Promise<T> {
    static Promise<T> new() { ... }
    
    void resolve(T value) { ... }
    void reject(Error e) { ... }
    
    Future<T> future() { ... }
}

impl<T> Future<T> {
    Result<T, Error> get() { ... }        // ブロッキング
    Result<T, Error> get_timeout(Duration d) { ... }
    
    bool is_ready() { ... }
    bool is_pending() { ... }
    
    // 変換
    Future<U> map<U>(U*(T) f) { ... }
    Future<T> on_complete(void(Result<T, Error>) callback) { ... }
}

// マクロ
export macro Future<T> async_run(body) {
    Promise<T> promise = Promise.new();
    Thread.spawn(|| {
        Result<T, Error> result = body;
        match result {
            Ok(v) => promise.resolve(v);
            Err(e) => promise.reject(e);
        }
    });
    return promise.future();
}
```

### 4.5 Atomic

```cm
// std/sync/atomic.cm
struct Atomic<T> { ... }

impl<T> Atomic<T> {
    static Atomic<T> new(T value) { ... }
    
    T load() { ... }
    void store(T value) { ... }
    T exchange(T new_value) { ... }
    
    // Compare and Swap
    bool compare_exchange(T expected, T desired) { ... }
    
    // 数値型専用
    T fetch_add(T delta) { ... }
    T fetch_sub(T delta) { ... }
}
```

### 4.6 Task（状態管理付き）

```cm
// std/sync/task.cm
enum TaskState {
    Pending,
    Running,
    Suspended,
    Completed(Result<T, Error>),
    Cancelled
}

struct Task<T> { ... }
struct CancellationToken { ... }

impl CancellationToken {
    static CancellationToken new() { ... }
    void cancel() { ... }
    bool is_cancelled() { ... }
}

impl<T> Task<T> {
    static Task<T> run(CancellationToken token, Result<T, Error>() work) { ... }
    
    TaskState state() { ... }
    
    void suspend() { ... }
    void resume() { ... }
    
    Result<T, Error> wait() { ... }
    Result<T, Error> wait_timeout(Duration d) { ... }
}
```

---

## 5. エラー型

```cm
// std/core/error.cm
struct Error {
    string message;
    int code;
    Error* cause;  // チェーン用
}

impl Error {
    static Error new(string message) { ... }
    static Error with_code(string message, int code) { ... }
    static Error wrap(string message, Error cause) { ... }
    
    string message() { return this.message; }
    int code() { return this.code; }
    Error* cause() { return this.cause; }
}

// 特化エラー型
struct ThreadError : Error { ... }
struct LockError : Error { ... }
struct SendError : Error { ... }
struct RecvError : Error { ... }
struct TimeoutError : Error { ... }
```

---

## 6. 実装ロードマップ

### Phase 1: 基盤拡張（必須）
- [ ] 型付きマクロ
- [ ] ブロック/クロージャ引数
- [ ] enum拡張（Associated Data）
- [ ] Result型実装

### Phase 2: 並行処理基盤
- [ ] std::sync::Thread
- [ ] std::sync::Mutex
- [ ] spawn_async マクロ
- [ ] with_lock マクロ

### Phase 3: 高度な同期
- [ ] Channel
- [ ] Future/Promise
- [ ] Atomic

### Phase 4: タスク抽象化
- [ ] Task
- [ ] CancellationToken
- [ ] suspend/resume

---

## 7. 依存関係

```
Result<T,E> ← 全ての並行処理API
    ↓
Error ← ThreadError, LockError, etc.
    ↓
R*(T...) ← Thread.spawn, callbacks
    ↓
enum拡張 ← Result, TaskState
```

