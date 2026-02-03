---
title: スレッド
parent: Advanced
nav_order: 10
---

# スレッド (std::thread)

Cmでは`std::thread`モジュールを使用してマルチスレッドプログラミングを行います。

## 基本的な使い方

### スレッドの生成と待機

```cm
import std::thread::spawn;
import std::thread::join;
import std::thread::sleep_ms;

void* worker(void* arg) {
    println("Worker: 開始");
    sleep_ms(100);
    println("Worker: 完了");
    return 42 as void*;
}

int main() {
    // スレッドを生成
    ulong t = spawn(worker as void*);
    
    // スレッド完了を待機し、結果を取得
    int result = join(t);
    println("結果: {result}");  // 結果: 42
    
    return 0;
}
```

## API リファレンス

### spawn

```cm
ulong spawn(void* fn);
```

新しいスレッドを生成します。

- **引数**: `fn` - スレッドで実行する関数（`void* fn(void*)` 型）
- **戻り値**: スレッドハンドル

### join

```cm
int join(ulong handle);
```

スレッドの完了を待機し、結果を取得します。

- **引数**: `handle` - `spawn`で取得したスレッドハンドル
- **戻り値**: スレッド関数の戻り値（int）

### detach

```cm
void detach(ulong handle);
```

スレッドをデタッチします。デタッチされたスレッドはバックグラウンドで実行され、メインスレッド終了時に自動的に終了します。

### sleep_ms

```cm
void sleep_ms(int ms);
```

現在のスレッドを指定ミリ秒停止します。

## 並列処理の例

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
    // 並列にスレッド生成
    ulong t1 = spawn(calc10 as void*);
    ulong t2 = spawn(calc20 as void*);
    
    // 両方の完了を待機
    int r1 = join(t1);
    int r2 = join(t2);
    
    println("合計: {r1 + r2}");  // 合計: 30
    
    return 0;
}
```

## join vs detach

| 操作 | 動作 |
|-----|-----|
| `join(t)` | スレッド完了まで待機、結果を取得 |
| `detach(t)` | バックグラウンド実行、結果は取得不可 |

### detachの注意点

`detach`されたスレッドは、メインスレッドが終了すると強制終了されます：

```cm
void* long_task(void* arg) {
    // 1秒かかる処理
    sleep_ms(1000);
    println("完了");  // メインが先に終了すると出力されない
    return 0 as void*;
}

int main() {
    ulong t = spawn(long_task as void*);
    detach(t);
    
    // 100ms後にメイン終了
    sleep_ms(100);
    // → long_taskは途中で強制終了
    
    return 0;
}
```

## 内部実装

`std::thread`はPOSIXの`pthread`を直接使用しています：

- `spawn` → `pthread_create`
- `join` → `pthread_join`
- `detach` → `pthread_detach`
- `sleep_ms` → `usleep`
