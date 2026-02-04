[English](optimized_cpp_codegen.en.html)

# 最適化されたC++コード生成設計

## 核心原則
**効率性とシンプリさを最優先**

## 1. printf ベースの高速出力

### 理由
- `std::cout`より**3-5倍高速**
- バッファリング制御が容易
- フォーマット文字列の直接変換が可能

### 実装例
```cpp
// 現在の非効率なコード
std::cout << "Hello, " << name << "!" << std::endl;

// 最適化後
printf("Hello, %s!\n", name.c_str());
```

## 2. Cm文字列補間の専用実装

### 軽量ヘルパー関数
```cpp
// Cm言語のランタイムサポート（最小限のヘッダー）
#include <cstdio>
#include <cstring>
#include <string>

namespace cm {

// 高速文字列フォーマッタ
template<typename... Args>
inline void println(const char* fmt, Args... args) {
    printf(fmt, args...);
    putchar('\n');
}

// 文字列補間のための効率的な変換
inline const char* str(const std::string& s) {
    return s.c_str();
}

inline const char* str(int n) {
    static thread_local char buf[32];
    snprintf(buf, sizeof(buf), "%d", n);
    return buf;
}

inline const char* str(double d) {
    static thread_local char buf[64];
    snprintf(buf, sizeof(buf), "%g", d);
    return buf;
}

// フォーマット指定子の処理
inline const char* fmt_hex(int n) {
    static thread_local char buf[32];
    snprintf(buf, sizeof(buf), "%x", n);
    return buf;
}

inline const char* fmt_binary(int n) {
    static thread_local char buf[35];
    strcpy(buf, "0b");
    for (int i = 31; i >= 0; i--) {
        if (n & (1 << i)) {
            int pos = strlen(buf);
            for (int j = i; j >= 0; j--) {
                buf[pos++] = (n & (1 << j)) ? '1' : '0';
            }
            buf[pos] = '\0';
            break;
        }
    }
    if (strlen(buf) == 2) strcat(buf, "0");
    return buf;
}

} // namespace cm
```

## 3. 制御フロー最適化

### CFGパターン認識と直接変換

```cpp
class OptimizedCodeGen {
    // 線形ブロックチェーンの検出
    bool isLinearChain(const BasicBlock* bb) {
        return bb->terminator.kind == Terminator::Goto &&
               bb->successors.size() == 1 &&
               !hasBackEdge(bb);
    }

    // 直接コード生成
    void generateLinearChain(const BasicBlock* start) {
        const BasicBlock* current = start;
        while (isLinearChain(current)) {
            for (const auto& stmt : current->statements) {
                generateDirectStatement(stmt);
            }
            current = current->successors[0];
        }
        // 最後のブロック処理
        for (const auto& stmt : current->statements) {
            generateDirectStatement(stmt);
        }
    }

    // ステートメントの直接生成
    void generateDirectStatement(const Statement& stmt) {
        switch (stmt.kind) {
            case Call:
                if (stmt.func == "println") {
                    generateOptimizedPrintln(stmt.args);
                } else {
                    generateDirectCall(stmt);
                }
                break;
            case Assign:
                // 不要な一時変数は削除
                if (!isNecessaryVariable(stmt.dest)) {
                    return;
                }
                generateDirectAssign(stmt);
                break;
            case Return:
                emit("return " + getValue(stmt.value) + ";");
                break;
        }
    }
};
```

## 4. 具体的な変換例

### Hello World
```cm
// Cm コード
println("Hello, World!");
```

```cpp
// 生成される最適化C++コード
#include <cstdio>

int main() {
    printf("Hello, World!\n");
    return 0;
}
```

### 文字列補間
```cm
// Cm コード
let name = "Alice";
let age = 25;
println("Name: {name}, Age: {age}");
```

```cpp
// 生成される最適化C++コード
#include <cstdio>
#include <string>

int main() {
    std::string name = "Alice";
    int age = 25;
    printf("Name: %s, Age: %d\n", name.c_str(), age);
    return 0;
}
```

### フォーマット指定子
```cm
// Cm コード
let n = 255;
println("Hex: {n:x}, Binary: {n:b}");
```

```cpp
// 生成される最適化C++コード
#include <cstdio>

namespace cm {
    inline const char* fmt_binary(int n) {
        static char buf[35];
        // ... 実装 ...
        return buf;
    }
}

int main() {
    int n = 255;
    printf("Hex: %x, Binary: %s\n", n, cm::fmt_binary(n));
    return 0;
}
```

### 条件分岐（ステートマシン不要）
```cm
// Cm コード
if (x > 0) {
    println("positive");
} else {
    println("negative");
}
```

```cpp
// 生成される最適化C++コード
if (x > 0) {
    printf("positive\n");
} else {
    printf("negative\n");
}
```

### ループ（自然な形式）
```cm
// Cm コード
for (int i = 0; i < 10; i++) {
    println("{i}");
}
```

```cpp
// 生成される最適化C++コード
for (int i = 0; i < 10; i++) {
    printf("%d\n", i);
}
```

## 5. 最適化戦略

### 変数削減
- **不要な一時変数を削除**
- **定数の直接埋め込み**
- **単純な値の伝播**

### 関数呼び出し最適化
- **println → printf 直接変換**
- **print → printf（改行なし）**
- **文字列結合の事前計算**

### メモリ効率
- **スタック変数の優先使用**
- **不要なヒープアロケーション回避**
- **thread_local バッファの再利用**

## 6. パフォーマンス目標

| メトリック | 現在 | 目標 | 改善率 |
|---------|------|------|--------|
| Hello World サイズ | 165行 | 5行 | 97%削減 |
| 実行速度 | 基準 | 3倍 | 200%向上 |
| コンパイル時間 | 基準 | 同等 | 0%増加 |
| メモリ使用量 | 基準 | 50% | 50%削減 |

## 7. 実装優先順位

1. **フェーズ1**: printf変換とヘルパー関数
2. **フェーズ2**: 線形フロー最適化
3. **フェーズ3**: 変数削減と定数伝播
4. **フェーズ4**: ループとブランチ最適化

## 8. テスト保証

- 全既存テストのパス維持
- パフォーマンスベンチマーク追加
- 生成コードサイズの自動測定
- 実行速度の比較テスト