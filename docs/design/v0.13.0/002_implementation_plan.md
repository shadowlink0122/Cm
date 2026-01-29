# v0.13.0 実装計画

## 概要

v0.13.0は**マクロシステム、非同期処理、同期プリミティブ**を中心とした大規模リリースです。

---

## 機能一覧

### コンパイラ実装

| 機能 | 優先度 | 状態 |
|------|--------|------|
| `#macro` 構文 | P0 | ⬜ |
| `asm()` 組み込み関数 | P0 | ⬜ |
| `async 戻り値型` / `async {}` | P0 | ⬜ |
| `await` 式 | P0 | ⬜ |
| ステートマシン変換 | P0 | ⬜ |
| `?` 演算子のasync対応 | P0 | ⬜ |
| `yield` 式 | P1 | ⬜ |
| オプショナルフィールド (`T?`) | P1 | ⬜ |

### ライブラリ実装

| モジュール | 優先度 | 状態 |
|-----------|--------|------|
| `lib/core/future.cm` | P0 | ⬜ |
| `lib/std/sync/` | P0 | ⬜ |
| `lib/std/async/` | P0 | ⬜ |
| `lib/std/net/tcp.cm` | P1 | ⬜ |
| `lib/std/net/http.cm` | P1 | ⬜ |

---

## Phase 1: マクロシステム（2週間）

### 1.1 `#macro` 構文

```cm
#macro void LOG(string message) {
    printf("[%s:%d] %s\n", __FILE__, __LINE__, message);
}

#macro auto MAX(auto a, auto b) {
    return (a > b) ? a : b;
}
```

**実装タスク**:
- [ ] パーサー: `#macro` キーワード認識
- [ ] AST: `MacroDefNode` 追加
- [ ] 既存 `src/macro/` コードとの統合
- [ ] テスト追加

### 1.2 `asm()` 組み込み関数

```cm
int result = asm(
    "mov rax, 1; syscall",
    in: { rdi = fd, rsi = buf, rdx = len },
    out: rax
);
```

**実装タスク**:
- [ ] パーサー: `asm()` 呼び出し認識
- [ ] LLVM IR生成: `call asm` 命令出力
- [ ] 制約文字列生成
- [ ] x86_64テスト

---

## Phase 2: 非同期基盤（3週間）

### 2.1 コンパイラ機能

```cm
// Cm言語の構文: async 戻り値型 関数名(引数)
async Result<int, Error> fetch_data(string url) {
    TcpStream stream = await TcpStream::connect(url)?;
    string body = await stream.read_to_string()?;
    return Ok(body.len());
}

// void戻り値の場合
async void process_request(Request req) {
    await handle(req);
}
```

**実装タスク**:
- [ ] パーサー: `async` 修飾子（戻り値型の前）
- [ ] 型チェッカー: `Future<T>` 戻り値型推論
- [ ] MIR: ステートマシン変換パス
- [ ] `?` 演算子の async 対応
- [ ] テスト追加

### 2.2 Core Future型（`lib/core/future.cm`）

```cm
enum Poll<T> {
    Ready(T),
    Pending
}

interface Future<T> {
    Poll<T> poll(Context* cx);
}

struct Context {
    Waker* waker;
}
```

---

## Phase 3: 同期プリミティブ（2週間）

### 3.1 `std::sync`（OSレベル）

```cm
// Mutex
struct Mutex<T> {
    T value;
    pthread_mutex_t raw;
}

// Atomic
struct Atomic<T> {
    T value;
}
```

### 3.2 `std::async`（非同期版）

```cm
// AsyncMutex
struct AsyncMutex<T> {
    T value;
    WaitQueue waiters;
}

impl<T> AsyncMutex<T> {
    async MutexGuard<T> lock() {
        // 非ブロッキングでロック待機
    }
}

// Channel
(Sender<T>, Receiver<T>) = mpsc::channel();
```

---

## Phase 4: ネットワーク（2週間）

### 4.1 TCP

```cm
// サーバー
async void server() {
    TcpListener listener = await TcpListener::bind("0.0.0.0:8080");
    while (true) {
        Result<TcpStream, Error> result = await listener.accept();
        if (result.is_ok()) {
            spawn(handle_client(result.unwrap()));
        }
    }
}
```

### 4.2 HTTP

```cm
async Result<HttpResponse, HttpError> fetch(string url) {
    HttpClient client = HttpClient::new();
    return await client.get(url);
}
```

---

## Phase 5: オプショナルフィールド（1週間）

### 構文

```cm
struct User {
    string name;
    int? age;              // オプショナル（null許容）
    string? email;
}

// 使用
User user = User{ name: "Alice" };  // age, emailは自動でnull
if (user.age != null) {
    println("Age: {}", user.age);
}
```

**実装タスク**:
- [ ] パーサー: `T?` 型構文
- [ ] 型チェッカー: `Option<T>` への変換
- [ ] コンストラクタでのデフォルト値

---

## テスト計画

| カテゴリ | テスト内容 |
|---------|-----------|
| マクロ | 展開、衛生性、エラー |
| asm | x86_64基本命令、レジスタ入出力 |
| async | ステートマシン変換、await |
| sync | Mutex競合、Channel送受信 |
| net | TCP接続、HTTP GET/POST |

---

## タイムライン

| 週 | 内容 |
|----|------|
| 1-2 | Phase 1: マクロシステム |
| 3-5 | Phase 2: 非同期基盤 |
| 6-7 | Phase 3: 同期プリミティブ |
| 8-9 | Phase 4: ネットワーク |
| 10 | Phase 5: オプショナルフィールド + 仕上げ |

---

## 変更ファイル予定

### コンパイラ

```
src/
├── frontend/
│   ├── parser/
│   │   ├── expr.cpp       # asm(), await
│   │   └── decl.cpp       # async fn, #macro
│   └── types/
│       └── checking/      # Future<T> 型推論
├── hir/
│   └── async_lowering.cpp # async→通常関数変換
├── mir/
│   └── passes/
│       └── state_machine.hpp  # ステートマシン変換
└── codegen/
    └── llvm/
        └── inline_asm.cpp # asm() -> LLVM asm
```

### ライブラリ

```
lib/
├── core/
│   ├── future.cm
│   ├── poll.cm
│   └── pin.cm
└── std/
    ├── sync/
    │   ├── mutex.cm
    │   ├── rwlock.cm
    │   └── atomic.cm
    ├── async/
    │   ├── executor.cm
    │   ├── mutex.cm
    │   ├── channel.cm
    │   └── waker.cm
    ├── net/
    │   ├── tcp.cm
    │   ├── udp.cm
    │   └── http.cm
    └── io/
        └── poll.cm
```
