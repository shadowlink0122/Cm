# no_std/ベアメタル最適化実装提案

## 概要
CmコンパイラにOSレス環境（組み込み、カーネル、ブートローダ）向けの`no_std`ターゲットを追加し、
専用の最適化を実装する。

## 1. コンパイルターゲットの追加

### 現在のターゲット
- `native`: OS上で動作するネイティブバイナリ
- `wasm`: WebAssemblyモジュール
- `js`: Node.js/ブラウザ向けJavaScript
- `web`: ブラウザ専用JavaScript

### 追加提案
```
--target=no_std    # ベアメタル/組み込み向け
--target=kernel    # カーネル開発向け（将来）
```

## 2. no_std特有の最適化

### 2.1 スタック使用量最適化
```cpp
// src/mir/optimizations/stack_optimization.hpp
class StackOptimization : public OptimizationPass {
    // 各関数の最大スタック使用量を静的解析
    size_t analyze_stack_usage(const MirFunction& func);

    // アロケーションをスタティック領域に移動
    void move_to_static(MirFunction& func);

    // 再帰をループに変換（可能な場合）
    void eliminate_tail_recursion(MirFunction& func);
};
```

### 2.2 ヒープ使用の完全排除
```cpp
// src/mir/optimizations/heap_elimination.hpp
class HeapElimination : public OptimizationPass {
    // 動的配列を固定長配列に変換
    void convert_dynamic_to_fixed(MirFunction& func);

    // Box/Rc/Arc等をスタティック領域に配置
    void static_allocation(MirFunction& func);
};
```

### 2.3 割り込みハンドラ最適化
```cpp
// src/codegen/llvm/interrupt_optimization.hpp
class InterruptOptimization {
    // 最小限のレジスタ退避
    void minimize_register_save(llvm::Function* isr);

    // 割り込み優先度に基づく最適化
    void priority_based_optimization(llvm::Module* module);
};
```

## 3. CodeGen層の実装

### 3.1 LLVMバックエンド拡張
```cpp
// src/codegen/llvm/no_std_codegen.cpp
class NoStdCodeGen : public LLVMCodeGen {
public:
    void configure_target() override {
        // ベアメタルターゲット設定
        target_triple = "thumbv7m-none-eabi";  // ARM Cortex-M3/M4

        // 最適化設定
        pass_builder.registerPipelineStartEPCallback(
            [](llvm::ModulePassManager& mpm) {
                // スタックサイズ最小化
                mpm.addPass(StackSizeMinimizationPass());

                // ROM/RAM配置最適化
                mpm.addPass(MemorySectionOptimizationPass());

                // 割り込み応答時間最適化
                mpm.addPass(InterruptLatencyOptimizationPass());
            }
        );
    }

    void generate_startup_code() {
        // ベクタテーブル生成
        generate_vector_table();

        // スタートアップルーチン
        generate_reset_handler();

        // メモリ初期化
        generate_memory_init();
    }

    void generate_linker_script() {
        // メモリマップ定義
        // FLASH: 0x08000000 - 256KB
        // RAM:   0x20000000 - 64KB
    }
};
```

### 3.2 実行時サポート最小化
```cpp
// src/runtime/no_std_runtime.c
// 最小限のランタイムサポート（100行以下）

void* cm_memcpy_nostd(void* dst, const void* src, size_t n) {
    // 最適化されたmemcpy（アセンブリ実装）
}

void cm_panic_handler() {
    // パニック時の処理（無限ループ or リセット）
    while(1) { __asm__("wfi"); }
}

// アロケータは提供しない（静的メモリのみ）
```

## 4. 最適化パイプライン

### 4.1 no_std専用最適化パス
```cpp
// src/mir/optimizations/all_passes.hpp
inline std::vector<std::unique_ptr<OptimizationPass>>
create_no_std_passes() {
    std::vector<std::unique_ptr<OptimizationPass>> passes;

    // 通常の最適化（全て適用）
    passes = create_standard_passes(2);

    // no_std特有の追加最適化
    passes.push_back(std::make_unique<HeapElimination>());
    passes.push_back(std::make_unique<StackOptimization>());
    passes.push_back(std::make_unique<ROMPlacementOptimization>());
    passes.push_back(std::make_unique<PowerOptimization>());  // 省電力

    return passes;
}
```

### 4.2 サイズ最適化の優先
```cpp
if (opts.target == "no_std") {
    // コードサイズ優先（-Os相当）
    builder.setOptLevel(llvm::OptimizationLevel::Os);

    // インライン化の閾値を下げる
    builder.setInlinerThreshold(25);  // 通常は225

    // ループアンロールを抑制
    builder.setLoopUnrollThreshold(4);  // 通常は150
}
```

## 5. メタデータと診断

### 5.1 スタック使用量レポート
```
関数別スタック使用量:
  main:           48 bytes
  interrupt_handler: 16 bytes
  process_data:   128 bytes

最大コールチェーン:
  main -> init -> process_data: 256 bytes

警告: 利用可能スタック(4KB)の6.25%を使用
```

### 5.2 メモリセクション配置
```
セクション配置:
  .text   (FLASH): 12,456 bytes (4.8%)
  .rodata (FLASH):  2,048 bytes (0.8%)
  .data   (RAM):      512 bytes (0.8%)
  .bss    (RAM):    4,096 bytes (6.3%)

最適化提案:
  - 10個の文字列定数をFLASHに配置可能
  - 未使用関数 'debug_print' を削除可能（-248 bytes）
```

## 6. テスト戦略

### 6.1 ユニットテスト
```cm
// tests/no_std/stack_usage.cm
#[no_std]
#[stack_limit(256)]  // 256バイト制限
int recursive_function(int n) {
    if (n == 0) return 1;
    int local_array[10];  // 40バイト
    return recursive_function(n - 1) + 1;
}

// コンパイルエラーが期待される（スタックオーバーフロー）
```

### 6.2 統合テスト
- QEMU エミュレータでの動作確認
- 実機（STM32, ESP32）での検証
- メモリ使用量の測定

## 7. 実装優先順位

### Phase 1（1ヶ月）
1. `--target=no_std` オプション追加
2. 基本的なベアメタルコード生成
3. スタートアップコード自動生成

### Phase 2（2ヶ月）
1. スタック使用量静的解析
2. ヒープ排除最適化
3. ROM/RAM配置最適化

### Phase 3（3ヶ月）
1. 割り込みハンドラ最適化
2. 省電力最適化
3. リアルタイム制約対応

## 8. 他ターゲットとの比較

| 最適化項目 | Native | WASM | JS | no_std |
|-----------|--------|------|----|--------|
| レジスタ割り当て | ✅ | ✅ | ❌ | ✅✅ |
| インライン化 | ✅ | ✅ | ✅ | ⚠️ |
| ループ最適化 | ✅ | ✅ | ⚠️ | ✅ |
| スタック最小化 | ⚠️ | ⚠️ | ❌ | ✅✅ |
| ヒープ排除 | ❌ | ❌ | ❌ | ✅✅ |
| ROM配置 | ❌ | ❌ | ❌ | ✅✅ |
| サイズ優先 | ⚠️ | ✅ | ⚠️ | ✅✅ |
| 電力最適化 | ❌ | ❌ | ❌ | ✅ |

凡例: ✅✅=最重要, ✅=実装済み, ⚠️=部分的, ❌=未対応/不要

## 9. まとめ

no_std ターゲットは、既存のMIR最適化に加えて、以下の特有最適化が必要：

1. **スタック/ヒープの厳密な管理**
2. **ROM/RAM配置の最適化**
3. **割り込み応答時間の最小化**
4. **消費電力の削減**

これらは他のターゲットでは不要または異なるアプローチが必要なため、
専用の最適化パスとCodeGen実装が必須となる。