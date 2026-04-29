# SVエラーコードの統一

**優先度**: 高  
**影響範囲**: エラーメッセージ  
**対象ファイル**: `src/codegen/sv/codegen.cpp`

---

## 問題

`error[SV002]` が複数箇所で異なるメッセージで使用されている。

### 現状

```cpp
// Line ~2517 (グローバル変数チェック)
std::cerr << "error[SV002]: Pointer types are not supported in SV target: "

// Line ~2527 (関数ローカル変数チェック)  
std::cerr << "error[SV002]: Pointer types not supported in SV target: "
```

微妙にメッセージが異なる（"are not" vs "not"）。

---

## 修正案

### 方法A: メッセージの統一

```cpp
constexpr const char* SV002_MSG = "error[SV002]: Pointer types are not supported in SV target";

// 使用箇所
std::cerr << SV002_MSG << ": " << var_name << "\n";
```

### 方法B: エラーヘルパー関数

```cpp
void SVCodeGen::reportError(const std::string& code, const std::string& msg, 
                            const std::string& context) {
    std::cerr << "error[" << code << "]: " << msg;
    if (!context.empty()) {
        std::cerr << ": " << context;
    }
    std::cerr << "\n";
}

// 使用
reportError("SV002", "Pointer types are not supported in SV target", gv->name);
```

---

## 現在のSVエラーコード

| コード | 説明 |
|-------|------|
| SV002 | ポインタ型非対応 |
| SV003 | 文字列型非合成 |

---

## 影響

- エラーメッセージの一貫性
- 将来のエラーコード追加の容易化

