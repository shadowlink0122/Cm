# デバッグ出力の統一

**優先度**: 中  
**影響範囲**: パフォーマンス、出力品質  
**対象ファイル**: 複数

---

## 問題

`std::cerr` による直接デバッグ出力が本番コードに残っている。

---

## 影響を受けるファイル

| ファイル | 内容 |
|---------|------|
| `mir_to_llvm.cpp` | `[DEBUG]` プレフィックス出力 |
| `pass_debugger.hpp` | `[PASS_DEBUG]` 出力 |
| `monomorphization_impl.cpp` | `[MONO]` マクロ |
| `codegen.cpp` (native) | デバッグ出力 |
| 他20+ファイル | 各種std::cerr |

---

## 現状のパターン

### 1. 直接出力
```cpp
std::cerr << "[DEBUG] fieldType fallback to i32\n";
```

### 2. マクロ定義
```cpp
#ifdef CM_DEBUG_MONOMORPHIZATION
#define MONO_DEBUG(msg) std::cerr << "[MONO] " << msg << std::endl
#else
#define MONO_DEBUG(msg)
#endif
```

### 3. PASS_DEBUG
```cpp
llvm::errs() << "[PASS_DEBUG] Pass '" << passName << "'\n";
```

---

## 修正案

### 1. debug::log() への統一

```cpp
// 現状
std::cerr << "[DEBUG] some message\n";

// 修正後
debug::log(debug::Level::Debug, "some message");
```

### 2. コンパイル時無効化

```cpp
#ifdef NDEBUG
#define DEBUG_LOG(msg) ((void)0)
#else
#define DEBUG_LOG(msg) debug::log(debug::Level::Debug, msg)
#endif
```

### 3. エラー出力との分離

- エラー: `std::cerr` または `error()` 関数（ユーザー向け）
- デバッグ: `debug::log()` （開発者向け、リリースビルドで無効化）

---

## 影響

- リリースビルドのパフォーマンス向上
- 出力の一貫性
- ログレベル制御の容易化

