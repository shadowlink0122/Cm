# 型付きマクロと並行処理ライブラリ設計

## v0.13.0 設計ドキュメント

---

## 1. 型付きマクロ

### 構文

```cm
// 型付きオブジェクトマクロ
macro <type> NAME = value;

// エクスポート可能
export macro <type> NAME = value;

// 型なし（void）
macro NAME = value;  // void型として扱う

// 関数マクロ
macro <return_type> NAME(params...) {
    return expr;
}
```

### 例

```cm
macro int VERSION = 13;
macro string MSG = "hello";
macro float PI = 3.14159;

// 型チェック：コンパイルエラー
int x = MSG;  // エラー: string を int に代入できません
```

### 実装変更

1. **MacroDef 構造体**
   ```cpp
   struct MacroDef {
       std::string name;
       TypePtr type;                    // 追加: マクロの型
       std::vector<std::string> params;
       std::vector<Token> body;
       bool is_exported = false;
   };
   ```

2. **parse_macro_definition**
   - 型をパース: `macro <type> NAME = ...`

---

## 2. 並行処理ライブラリ（マクロベース）

### 設計理念

- **言語機能ではなくライブラリ**として提供
- **マクロで構文糖衣**を提供し、内部は標準関数で実装
- **明示的な型宣言**を必須とする（`let`禁止）

### std::sync モジュール構成

```
std/sync/
├── mod.cm          # メインモジュール
├── thread.cm       # スレッド管理
├── mutex.cm        # 排他制御
├── channel.cm      # チャネル通信
├── atomic.cm       # アトミック操作
└── future.cm       # 非同期結果
```

### API設計

#### スレッド

```cm
import std::sync::{Thread, spawn};

// マクロで簡潔な構文を提供
export macro Thread spawn_async(body) {
    return Thread.spawn(|| { body });
}

// 使用例
Thread handle = spawn_async({
    // 非同期処理
    return 42;
});

int result = handle.join();
```

#### ミューテックス

```cm
import std::sync::{Mutex, MutexGuard};

// 排他制御マクロ
export macro void with_lock(mutex, body) {
    MutexGuard guard = mutex.lock();
    body;
    // guard は自動解放
}

// 使用例
Mutex<int> counter = Mutex.new(0);

with_lock(counter, {
    *counter += 1;
});
```

#### チャネル

```cm
import std::sync::{Channel, Sender, Receiver};

// チャネル作成
Channel<int> ch = Channel.new();
Sender<int> tx = ch.sender();
Receiver<int> rx = ch.receiver();

// 送受信
tx.send(42);
int value = rx.recv();
```

#### Future（非同期結果）

```cm
import std::sync::{Future, Promise};

// 状態管理マクロ
export macro Future<T> async_run(body) {
    Promise<T> promise = Promise.new();
    Thread.spawn(|| {
        T result = body;
        promise.resolve(result);
    });
    return promise.future();
}

// 使用例
Future<int> fut = async_run({
    // 長時間処理
    return compute();
});

// 結果取得（ブロッキング）
int result = fut.get();

// 非ブロッキング
if (fut.is_ready()) {
    int result = fut.get();
}
```

---

## 3. 状態管理と停止

### スレッドの状態

```cm
enum ThreadState {
    Running,
    Suspended,
    Completed,
    Cancelled
}

interface Suspendable {
    void suspend();
    void resume();
    bool is_suspended();
}
```

### キャンセル可能なタスク

```cm
import std::sync::{Task, CancellationToken};

CancellationToken token = CancellationToken.new();

Task<int> task = Task.run(token, || {
    while (!token.is_cancelled()) {
        // 処理
    }
    return result;
});

// キャンセル
token.cancel();
task.wait();
```

---

## 4. データ競合防止

### Read-Write Lock

```cm
import std::sync::RwLock;

RwLock<Data> data = RwLock.new(initial_data);

// 読み取り（複数許可）
{
    ReadGuard<Data> guard = data.read();
    // guard->field を読み取り
}

// 書き込み（排他）
{
    WriteGuard<Data> guard = data.write();
    guard->field = new_value;
}
```

### アトミック操作

```cm
import std::sync::Atomic;

Atomic<int> counter = Atomic.new(0);

counter.fetch_add(1);
int val = counter.load();
counter.store(100);

// Compare and Swap
bool success = counter.compare_exchange(expected, desired);
```

---

## 5. 実装ロードマップ

### Phase 1: 型付きマクロ
- [ ] MacroDef に型情報追加
- [ ] parse_macro_definition で型パース
- [ ] 型チェック追加

### Phase 2: 基本並行処理
- [ ] std::sync::Thread 実装
- [ ] std::sync::Mutex 実装
- [ ] spawn_async マクロ

### Phase 3: 高度な同期
- [ ] Channel 実装
- [ ] Future/Promise 実装
- [ ] with_lock マクロ

### Phase 4: 状態管理
- [ ] CancellationToken 実装
- [ ] Task 抽象化
- [ ] suspend/resume
