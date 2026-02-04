[English](interpreter.en.html)

# インタプリタ設計

## 概要

Cmインタプリタは、HIRを直接解釈実行します。将来的にはJITコンパイラへの移行を想定しています。

## アーキテクチャ

```
┌─────────────────────────────────────────────────────────────┐
│                      Interpreter                            │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐     │
│  │    HIR      │ →  │   Eval      │ →  │   Value     │     │
│  │  (入力)     │    │   Engine    │    │  (結果)     │     │
│  └─────────────┘    └─────────────┘    └─────────────┘     │
│                           │                                 │
│                           ▼                                 │
│                    ┌─────────────┐                          │
│                    │ Environment │                          │
│                    │ (変数束縛)  │                          │
│                    └─────────────┘                          │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

## 実装

### 値の表現

```cpp
// src/backend/interpreter/value.hpp
struct Value {
    enum class Type {
        Null,
        Int,
        Float,
        Bool,
        Char,
        String,
        Array,
        Struct,
        Function,
        Reference,
    };
    
    Type type;
    std::variant<
        std::monostate,           // Null
        int64_t,                  // Int
        double,                   // Float
        bool,                     // Bool
        char,                     // Char
        std::string,              // String
        std::vector<Value>,       // Array
        std::map<std::string, Value>, // Struct
        FunctionRef,              // Function
        std::shared_ptr<Value>    // Reference
    > data;
};
```

### 環境（スコープ）

```cpp
// src/backend/interpreter/environment.hpp
class Environment {
public:
    void define(const std::string& name, Value value);
    Value get(const std::string& name) const;
    void set(const std::string& name, Value value);
    
    std::shared_ptr<Environment> push_scope();
    void pop_scope();
    
private:
    std::map<std::string, Value> variables_;
    std::shared_ptr<Environment> parent_;
};
```

### 評価エンジン

```cpp
// src/backend/interpreter/interpreter.hpp
class Interpreter {
public:
    Interpreter();
    
    Value eval(const HirModule& module);
    Value eval(const HirExpr& expr);
    Value eval(const HirStmt& stmt);
    
private:
    Value eval_literal(const HirLiteral& lit);
    Value eval_binary(const HirBinaryExpr& bin);
    Value eval_call(const HirCall& call);
    Value eval_match(const HirMatch& match);
    Value eval_if(const HirIf& if_stmt);
    Value eval_loop(const HirLoop& loop);
    
    std::shared_ptr<Environment> env_;
    std::map<std::string, HirFunction> functions_;
};
```

---

## 実行フロー

### 関数呼び出し

```cpp
// Cm
int factorial(int n) {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

int main() {
    return factorial(5);
}
```

```
1. main() 呼び出し
2. factorial(5) 呼び出し
   - 新しいスコープ作成
   - n = 5 を束縛
   - if (5 <= 1) → false
   - factorial(4) 再帰呼び出し
   - ...
   - 5 * 4 * 3 * 2 * 1 = 120
3. 結果: 120
```

---

## デバッグモード対応

```cpp
class Interpreter {
    void eval_with_debug(const HirStmt& stmt) {
        LOG_TRACE("Eval: {}", stmt.to_string());
        
        auto result = eval(stmt);
        
        LOG_TRACE("Result: {}", result.to_string());
    }
};
```

出力例:
```
[TRACE] Eval: LetStmt { name: "x", init: Literal(5) }
[TRACE] Result: ()
[TRACE] Eval: BinaryExpr { op: Add, lhs: VarRef("x"), rhs: Literal(3) }
[TRACE] Result: Int(8)
```

---

## 将来: JITコンパイラ

### Phase 1: トレーシングJIT

```
┌─────────────────────────────────────────────────────────────┐
│                   Tracing JIT                               │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌─────────────┐   ホットパス検出   ┌─────────────┐        │
│  │ Interpreter │ ───────────────→  │  Compiler   │        │
│  └─────────────┘                    └─────────────┘        │
│        ↓                                  ↓                │
│   通常実行                           ネイティブコード        │
│   (遅い)                             (速い)                │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 検出ルール

```cpp
struct HotSpotDetector {
    static constexpr int THRESHOLD = 1000;  // 実行回数閾値
    
    std::map<FunctionId, int> call_counts_;
    
    bool is_hot(FunctionId id) {
        return ++call_counts_[id] >= THRESHOLD;
    }
};
```

### Phase 2: MethodJIT

```
HIR → MIR → 最適化 → Cranelift → ネイティブコード
```

---

## 非同期サポート

### async/await実装

```cpp
// Cm
async String fetchData(String url) {
    Response res = await http::get(url);
    return res.body;
}
```

インタプリタ内部:
```cpp
struct AsyncState {
    enum class Status { Pending, Ready, Error };
    Status status;
    std::optional<Value> result;
    std::function<void()> continuation;
};

class AsyncRuntime {
    std::queue<AsyncState> pending_tasks_;
    
    void run_until_complete();
    void spawn(AsyncState task);
};
```

---

## 制限事項

| 項目 | 状態 |
|------|------|
| 基本実行 | ✅ Phase 1で実装 |
| 再帰 | ✅ スタックベース |
| クロージャ | ✅ 環境キャプチャ |
| async/await | ⚠️ 簡易実装 |
| JIT | 📋 Phase 2以降 |