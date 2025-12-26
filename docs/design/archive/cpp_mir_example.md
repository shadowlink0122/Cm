# CPP-MIR実装例

## 現在のパイプライン vs 提案するパイプライン

### 入力: Hello World (Cm)
```cm
import std::io::println;
int main() {
    println("Hello, World!");
    return 0;
}
```

## 現在のパイプライン（問題あり）

### HIR → MIR（既に基本ブロック化）
```
fn main() -> int {
    bb0: {
        _1 = const "Hello, World!";
        _2 = call println(_1) -> bb1;
    }
    bb1: {
        _3 = const 0;
        _0 = _3;
        return;
    }
}
```

### MIR → CPP（ステートマシン生成）
```cpp
int cm_main() {
    std::string _1;
    int _2 = 0;
    int _3 = 0;
    int _0 = 0;

    int __bb = 0;
    while (true) {
        switch (__bb) {
            case 0:
                _1 = "Hello, World!";
                println(_1);
                __bb = 1;
                break;
            case 1:
                _3 = 0;
                _0 = _3;
                return _0;
        }
    }
}
```

## 提案するパイプライン（効率的）

### HIR（汎用的・シンプル）
```
Function main() -> int {
    Statement: Call(println, ["Hello, World!"])
    Statement: Return(0)
}
```

### MIR（汎用的中間表現）
```
fn main() -> int {
    %1 = const "Hello, World!"
    call println(%1)
    return 0
}
```

### CPP-MIR（C++最適化済み）
```
fn main() -> int {
    // printlnをprintfに最適化
    printf("Hello, World!\n")
    return 0
}
```

### CPP-Codegen（単純な変換）
```cpp
#include <cstdio>

int main() {
    printf("Hello, World!\n");
    return 0;
}
```

## より複雑な例: 文字列補間

### 入力（Cm）
```cm
int main() {
    let name = "Alice";
    let age = 25;
    println("Name: {name}, Age: {age:02}");
    return 0;
}
```

### 現在のMIR → CPP（非効率）
```cpp
// 165行以上の複雑なコード...
```

### 提案: CPP-MIR（最適化済み）
```
fn main() -> int {
    name = "Alice"
    age = 25
    // 文字列補間を事前解析
    printf_format = "Name: %s, Age: %02d\n"
    printf(printf_format, name, age)
    return 0
}
```

### CPP-Codegen（シンプル）
```cpp
#include <cstdio>
#include <string>

int main() {
    std::string name = "Alice";
    int age = 25;
    printf("Name: %s, Age: %02d\n", name.c_str(), age);
    return 0;
}
```

## 条件分岐の例

### 入力（Cm）
```cm
int main() {
    let x = 10;
    if (x > 0) {
        println("positive");
    } else {
        println("negative");
    }
    return 0;
}
```

### 提案: CPP-MIR（線形フロー検出）
```
fn main() -> int {
    x = 10
    if (x > 0) {
        printf("positive\n")
    } else {
        printf("negative\n")
    }
    return 0
}
// メタデータ: is_linear = false, has_branch = true
```

### CPP-Codegen（自然なC++）
```cpp
#include <cstdio>

int main() {
    int x = 10;
    if (x > 0) {
        printf("positive\n");
    } else {
        printf("negative\n");
    }
    return 0;
}
```

## CPP-MIR最適化パスの実装

```cpp
namespace cpp_mir {

// CPP-MIR命令
struct Instruction {
    enum Type {
        ASSIGN,
        PRINTF,      // printlnから変換済み
        IF_ELSE,
        WHILE,
        FOR,
        RETURN
    };
    Type type;
    std::vector<std::string> args;
};

// CPP-MIR関数
struct Function {
    std::string name;
    std::vector<Instruction> instructions;

    // メタデータ
    bool is_linear = true;
    bool uses_string = false;
    bool uses_format = false;
};

// MIR → CPP-MIR変換器
class CppMirConverter {
public:
    Function convert(const mir::MirFunction& mir_func) {
        Function cpp_func;
        cpp_func.name = mir_func.name;

        // 線形フロー検出
        cpp_func.is_linear = analyzeFlow(mir_func);

        // 命令変換
        for (const auto& bb : mir_func.basic_blocks) {
            convertBlock(bb, cpp_func);
        }

        // 最適化パス
        optimizePrintln(cpp_func);
        inlineConstants(cpp_func);
        removeDeadVariables(cpp_func);

        return cpp_func;
    }

private:
    void optimizePrintln(Function& func) {
        for (auto& inst : func.instructions) {
            if (inst.type == CALL && inst.args[0] == "println") {
                // println → printf変換
                inst.type = PRINTF;
                auto format = analyzeFormat(inst.args[1]);
                inst.args = buildPrintfArgs(format);
            }
        }
    }

    void inlineConstants(Function& func) {
        // 定数を直接埋め込み
        std::map<std::string, std::string> constants;

        auto it = func.instructions.begin();
        while (it != func.instructions.end()) {
            if (it->type == ASSIGN && isConstant(it->args[1])) {
                constants[it->args[0]] = it->args[1];
                it = func.instructions.erase(it);
            } else {
                // 定数を使用している箇所を置換
                for (auto& arg : it->args) {
                    if (constants.count(arg)) {
                        arg = constants[arg];
                    }
                }
                ++it;
            }
        }
    }
};

// CPP-Codegen（単純な変換器）
class CppCodegen {
public:
    void generate(const cpp_mir::Function& func) {
        // ヘッダー（必要なもののみ）
        emit("#include <cstdio>");
        if (func.uses_string) {
            emit("#include <string>");
        }
        emit("");

        // 関数生成
        emit("int " + func.name + "() {");
        indent++;

        // 線形コードは直接生成
        for (const auto& inst : func.instructions) {
            generateInstruction(inst);
        }

        indent--;
        emit("}");
    }

private:
    void generateInstruction(const Instruction& inst) {
        switch (inst.type) {
            case PRINTF:
                emit("printf(" + join(inst.args, ", ") + ");");
                break;
            case IF_ELSE:
                generateIfElse(inst);
                break;
            case RETURN:
                emit("return " + inst.args[0] + ";");
                break;
            // ...
        }
    }
};

} // namespace cpp_mir
```

## メリット

1. **各層の責任が明確**
   - HIR: 言語の意味保持
   - MIR: 汎用最適化
   - CPP-MIR: C++専用最適化
   - Codegen: 単純変換

2. **効率的なコード生成**
   - ステートマシン不要
   - 自然なC++コード
   - 最小限の変数

3. **保守性**
   - 各層が独立
   - テストが容易
   - 新機能追加が簡単