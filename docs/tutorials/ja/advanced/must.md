---
title: must キーワード
parent: Advanced
nav_order: 8
---

# must キーワード

`must`キーワードは、コードがコンパイラの最適化（デッドコード削除）から保護されることを保証します。

## 基本的な使い方

```cm
int main() {
    int x = 0;
    
    // 通常の代入（最適化で削除される可能性あり）
    x = 100;
    
    // must{}で囲むと削除されない
    must { x = 200; }
    
    return 0;
}
```

## 使用場面

### 1. ベンチマーク

計算結果を使わないベンチマークコードを保護：

```cm
import std::thread::sleep_ms;

int main() {
    int result = 0;
    
    // このループは最適化で削除される可能性あり
    for (int i = 0; i < 1000000; i++) {
        result = result + i;
    }
    
    // must{}で囲むと確実に実行される
    for (int i = 0; i < 1000000; i++) {
        must { result = result + i; }
    }
    
    return 0;
}
```

### 2. スレッドテスト

スレッドのタイミングテストで副作用のない処理を保護：

```cm
import std::thread::spawn;
import std::thread::join;
import std::thread::sleep_ms;

void* worker(void* arg) {
    int count = 0;
    
    // ビジーループを保護
    for (int i = 0; i < 100; i++) {
        must { count = i; }
        sleep_ms(10);
    }
    
    return count as void*;
}

int main() {
    ulong t = spawn(worker as void*);
    int result = join(t);
    return 0;
}
```

### 3. セキュリティ（メモリクリア）

機密データのクリアが最適化で削除されるのを防ぐ：

```cm
void clear_password(char* buffer, int size) {
    // 最適化で削除される可能性あり
    // for (int i = 0; i < size; i++) { buffer[i] = 0; }
    
    // must{}で確実にクリア
    for (int i = 0; i < size; i++) {
        must { buffer[i] = 0; }
    }
}
```

## 内部実装

`must{}`で囲まれたコードは、LLVM IRで`volatile`としてマークされます：

```llvm
; 通常の代入
store i32 100, ptr %x

; must{}内の代入
store volatile i32 200, ptr %x
```

`volatile`はコンパイラに「この操作は外部から観測可能であり、削除してはならない」と伝えます。

## 確認方法

`--lir-opt`オプションで最適化後のLLVM IRを確認できます：

```bash
./cm compile --lir-opt src/main.cm
```

## 注意事項

- `must{}`はパフォーマンスへの影響がある可能性があります
- 本当に必要な場合のみ使用してください
- 通常のコードでは不要です

## 関連項目

- [スレッド](thread.md) - マルチスレッドプログラミング
