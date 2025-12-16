# 効率的なC++コード生成の設計

## 現状の問題点

現在のC++コード生成は以下の問題があります：

1. **過剰なステートマシン化**
   - 単純な直線コードでも`while(true) + switch`パターンを使用
   - 例：Hello Worldが165行のコードに膨張

2. **不要な基本ブロック分割**
   - MIRが全てのコードを基本ブロック（bb0, bb1...）に分割
   - 直線コードでもCFG表現を使用

3. **過剰な変数宣言**
   - 不要な一時変数（_1, _2, _3...）
   - 単純な値の伝播に複数の変数を使用

## 改善アプローチ

### 1. CFG分析による制御フロー最適化

```cpp
// 現在の生成コード（非効率）
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

// 理想的な生成コード（効率的）
int main() {
    std::cout << "Hello, World!" << std::endl;
    return 0;
}
```

### 2. 基本ブロックの分類

基本ブロックを以下のパターンに分類：

- **LINEAR**: 単一の後続ブロックへの無条件ジャンプ
- **CONDITIONAL**: 条件分岐を含むブロック
- **LOOP**: ループバックを含むブロック
- **RETURN**: 関数からのリターン

### 3. パターン別コード生成戦略

#### LINEARパターン
```cpp
// MIR:
// bb0: { stmt1; stmt2; goto bb1; }
// bb1: { stmt3; return; }

// 生成コード:
stmt1;
stmt2;
stmt3;
return;
```

#### CONDITIONALパターン
```cpp
// MIR:
// bb0: { if (cond) goto bb1 else bb2; }

// 生成コード:
if (cond) {
    // bb1 の内容
} else {
    // bb2 の内容
}
```

#### LOOPパターン
```cpp
// MIR:
// bb0: { if (cond) goto bb1 else bb2; }
// bb1: { body; goto bb0; }

// 生成コード:
while (cond) {
    // body
}
```

### 4. 変数最適化

- **定数伝播**: 定数値を直接使用
- **デッドコード除去**: 未使用変数の除去
- **インライン化**: 単純な一時変数をインライン展開

## 実装計画

### フェーズ1: CFG分析器の実装
```cpp
class CFGAnalyzer {
    enum BlockPattern {
        LINEAR,
        CONDITIONAL,
        LOOP,
        RETURN
    };

    BlockPattern analyzeBlock(const MirBasicBlock& block);
    bool isLinearSequence(const std::vector<MirBasicBlock>& blocks);
    bool hasLoopBack(const MirBasicBlock& block);
};
```

### フェーズ2: 最適化コード生成器
```cpp
class OptimizedCppCodeGen {
    void generateLinearSequence(const std::vector<MirBasicBlock>& blocks);
    void generateConditional(const MirBasicBlock& block);
    void generateLoop(const MirBasicBlock& header, const MirBasicBlock& body);

    // 変数最適化
    bool canInline(const LocalId& var);
    std::string getInlinedValue(const MirOperand& operand);
};
```

### フェーズ3: println最適化
```cpp
// 特別な関数の直接変換
void generatePrintln(const std::vector<MirOperand>& args) {
    if (args.size() == 1 && isStringConstant(args[0])) {
        emit("std::cout << " + getConstantValue(args[0]) + " << std::endl;");
    } else {
        // フォーマット文字列処理
        generateFormattedPrint(args);
    }
}
```

## 期待される結果

- Hello Worldプログラム: 165行 → 10行以下
- 変数宣言: 4個 → 0個（必要な場合のみ）
- 実行時オーバーヘッド: ステートマシンなし
- コード可読性: 大幅に向上

## テスト戦略

1. 全ての既存テストが引き続きパスすること
2. 生成コードのサイズ削減を測定
3. 実行速度の改善を確認
4. デバッグ情報の保持を確認