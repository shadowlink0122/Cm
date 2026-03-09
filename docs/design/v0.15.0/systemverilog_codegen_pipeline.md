# SystemVerilog コード生成パイプライン — 実装設計

> 対象: Cm v0.15.0 Phase 1
> 前提: [systemverilog_backend.md](systemverilog_backend.md)

## 1. ディレクトリ構成

```
src/codegen/sv/
├── codegen.hpp          # SVCodeGenerator クラス定義
├── codegen.cpp          # エントリポイント: MirProgram → SV文字列
├── module_emitter.cpp   # module/port/wire/reg 宣言の出力
├── always_emitter.cpp   # always_ff / always_comb ブロックの出力
├── expr_emitter.cpp     # MirOperand → SV式文字列
├── type_mapper.hpp      # Cm型 → SV型変換
├── type_mapper.cpp      # bit<N>, N'hXX リテラル対応
├── constraints.cpp      # #[sv::pin] → XDC/SDC 出力
├── fsm_builder.hpp      # FSMビルダー定義 (Phase 2)
└── fsm_builder.cpp      # await → FSMステートマシン合成 (Phase 2)
```

## 2. SVCodeGenerator クラス

```cpp
namespace cm::codegen::sv {

class SVCodeGenerator : public BufferedCodeGenerator {
public:
    struct SVConfig {
        bool generate_testbench = false;
        bool generate_constraints = false;     // XDC出力
        bool use_always_ff = true;             // always_ff (false→always @(posedge))
        std::string default_clock = "clk";
        std::string default_reset = "rst";
        std::string constraints_output_path;   // XDCファイルパス
    };

    std::string generate(const mir::MirProgram& program, const SVConfig& config);

private:
    // SVモジュール抽出
    std::vector<SVModule> collect_modules(const mir::MirProgram& program);
    
    // SV型制約チェック
    void validate_sv_types(const SVModule& mod);
    
    // 出力
    void emit_module(const SVModule& mod);
    void emit_port_declarations(const SVModule& mod);
    void emit_internal_signals(const SVModule& mod);
    void emit_always_ff_blocks(const SVModule& mod);
    void emit_always_comb_blocks(const SVModule& mod);
    void emit_submodule_instances(const SVModule& mod);
    
    // XDC制約出力
    std::string generate_constraints(const SVModule& mod);
    
    // 式変換
    std::string emit_expr(const mir::MirOperand& operand);
    std::string emit_rvalue(const mir::MirRvalue& rvalue);
    std::string emit_literal(const mir::MirConstant& constant);
    
    // 型変換
    std::string type_to_sv(const hir::Type& type);
    std::string bit_width_decl(const hir::Type& type);  // "[31:0]" 等
};

// === 中間表現 ===

struct SVPort {
    enum Direction { INPUT, OUTPUT, INOUT };
    Direction dir;
    std::string name;
    std::string sv_type;        // "logic [31:0]"
    std::string pin_assignment;  // "E3" (XDC用)
    std::string io_standard;     // "LVCMOS33"
};

struct SVSignal {
    std::string name;
    std::string sv_type;
    bool is_wire;  // true=wire, false=reg(logic)
};

struct SVAlwaysBlock {
    enum Kind { ALWAYS_FF, ALWAYS_COMB };
    Kind kind;
    std::string clock_signal;
    std::string reset_signal;
    bool reset_active_low;
    const mir::MirFunction* source_func;
};

struct SVSubmodule {
    std::string module_type;
    std::string instance_name;
    std::vector<std::pair<std::string, std::string>> port_connections;
};

struct SVModule {
    std::string name;
    std::vector<SVPort> ports;
    std::vector<SVSignal> signals;
    std::vector<SVAlwaysBlock> always_blocks;
    std::vector<SVSubmodule> submodules;
    std::string constraints_file;
};

} // namespace cm::codegen::sv
```

## 3. MIR → SV 変換ルール

### 3.1 代入 (MirStatement::Assign)

```
MIR:  Assign { place: local_3, rvalue: BinaryOp(Add, local_1, local_2) }

async func内 → count <= a + b;
func内       → result = a + b;
```

### 3.2 インクリメント/デクリメント展開

MIR loweringで `++`/`--` は以下に展開済み:

```
MIR:  _5 = Const(1)
      _6 = BinaryOp(Add, _3, _5)    // count + 1
      Assign(_3, Use(_6))            // count = count + 1
```

SVバックエンドは最適化パスで `count + 1` パターンを検出し簡潔に出力:

```systemverilog
count <= count + 32'd1;  // async func
```

### 3.3 制御フロー (MirTerminator)

| MIR Terminator | SV出力 |
|----------------|--------|
| `Branch { cond, then_bb, else_bb }` | `if (cond) begin ... end else begin ... end` |
| `SwitchInt { val, targets }` | `case (val) ... endcase` |
| `Goto { target }` | (次ブロックへフォールスルー) |
| `Return` | (ブロック終了) |

### 3.4 リテラル (MirConstant)

| MIR定数 | SV出力 |
|---------|--------|
| `Const(0, i32)` | `32'd0` |
| `Const(0xFF, u8)` | `8'hFF` |
| `Const(true, bool)` | `1'b1` |

> `bit<N>` の `N'hXX` リテラルはレキサーレベルで `BitLiteral(width, base, value)` トークンに変換し、MIR定数として伝搬。

## 4. CLI統合

### 4.1 Target enum拡張

```cpp
// src/common/target.hpp
enum class Target {
    Native,
    Wasm,
    JS,
    SV,         // ← 新規
    Baremetal,
};
```

### 4.2 コマンドライン

```bash
# 基本: SV出力
cm compile --target=sv counter.cm -o counter.sv

# XDC制約も出力
cm compile --target=sv --constraints top.cm -o top.sv

# テストベンチ生成 (Phase 3)
cm compile --target=sv --testbench counter.cm
```

## 5. bit<N> 型の実装

### 5.1 レキサー変更

`N'h`, `N'b`, `N'd` パターンを `BitLiteral` トークンとして認識:

```
TokenKind::BitLiteral {
    width: uint,       // N
    base: char,        // 'h', 'b', 'd', 'o'
    value: string,     // "FF", "1010", "255"
}
```

### 5.2 型チェッカー変更

```
TypeKind::BitVector {
    width: uint,  // N
}
```

`bit<N>` はジェネリクス構文を再利用。`N` は定数式で、`const_generics` として処理。

### 5.3 影響ファイル

| ファイル | 変更内容 |
|---------|---------|
| `src/frontend/lexer/lexer.cpp` | `N'[hbdo]` トークン認識 |
| `src/frontend/parser/parser_type.cpp` | `bit<N>` パース |
| `src/hir/types.hpp` | `TypeKind::BitVector` 追加 |
| `src/frontend/types/checking/expr.cpp` | `BitLiteral` 型チェック |

## 6. テスト計画

### 6.1 テストケース

```
tests/sv/
├── basic/
│   ├── counter.cm / counter.expect.sv      # カウンタ (async func)
│   ├── adder.cm / adder.expect.sv          # 組み合わせ加算器 (func)
│   ├── mux.cm / mux.expect.sv              # マルチプレクサ (if/else)
│   ├── alu.cm / alu.expect.sv              # ALU (match→case) Phase 2
│   └── bit_ops.cm / bit_ops.expect.sv      # bit<N> ビット操作
├── pin/
│   ├── nexys_top.cm / nexys_top.expect.xdc # ピン割当テスト
└── run_sv_tests.sh
```

### 6.2 テスト3段階

| Stage | 内容 | CI要件 | ツール |
|-------|------|--------|-------|
| 1. 出力比較 | `.sv` と `.expect.sv` の diff | **必須** | diff |
| 2. 構文検証 | SV lint | **推奨** | `verilator --lint-only` |
| 3. シミュレーション | テストベンチ実行 | optional | `verilator` / `iverilog` |

### 6.3 Makefile統合

```makefile
# Cm ルートMakefile に追加
test-sv:
	@echo "=== SystemVerilog バックエンドテスト ==="
	@cd tests/sv && ./run_sv_tests.sh

# run_sv_tests.sh
#!/bin/bash
PASS=0; FAIL=0
for cm_file in basic/*.cm; do
    base="${cm_file%.cm}"
    expect="${base}.expect.sv"
    [ ! -f "$expect" ] && continue
    
    out="/tmp/sv_test_$(basename $base).sv"
    cm compile --target=sv "$cm_file" -o "$out" 2>/dev/null
    
    if diff -q "$out" "$expect" > /dev/null 2>&1; then
        echo "PASS: $cm_file"
        ((PASS++))
        # Verilator lint (利用可能な場合)
        if command -v verilator &> /dev/null; then
            verilator --lint-only -Wall "$out" 2>/dev/null || echo "  LINT WARN: $cm_file"
        fi
    else
        echo "FAIL: $cm_file"
        diff "$out" "$expect"
        ((FAIL++))
    fi
done
echo "Results: $PASS passed, $FAIL failed"
[ $FAIL -eq 0 ]
```

## 7. 変更影響箇所まとめ

| ファイル | 変更内容 |
|---------|---------|
| `CMakeLists.txt` | `src/codegen/sv/*.cpp` 追加 |
| `src/main.cpp` | `--target=sv` / `--constraints` |
| `src/common/target.hpp` | `Target::SV` |
| `src/frontend/lexer/lexer.cpp` | `N'hXX` BitLiteral |
| `src/frontend/parser/parser_type.cpp` | `bit<N>` |
| `src/hir/types.hpp` | `TypeKind::BitVector` |
| `src/codegen/sv/` | 新規 (8ファイル) |
| `tests/sv/` | テストケース |
| `Makefile` | `test-sv` ターゲット |
