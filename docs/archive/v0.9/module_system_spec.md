[English](module_system_spec.en.html)

# Cm言語 モジュールシステム仕様書

## 概要
Cm言語のモジュールシステムは、コードの再利用性と保守性を向上させるための機能です。

## 現在の実装状況

### 基本的なインポート/エクスポート
```cm
// math.cm
export int add(int a, int b) {
    return a + b;
}

// main.cm
import math;
int result = math::add(3, 4);
```

### 選択的インポート
```cm
import { add, subtract } from math;
int result = add(3, 4);  // math:: プレフィックス不要
```

### 標準ライブラリのインポート
```cm
import std::io::println;
println("Hello, World!");
```

## 実装済み機能

### ✅ 動作する機能
1. **基本的なexport/import**
   - 関数のエクスポート
   - モジュール全体のインポート
   - 名前空間付きアクセス（`module::function`）

2. **選択的インポート**
   - 特定の関数のみインポート
   - 名前空間なしでの直接アクセス

3. **インタープリタサポート**
   - インタープリタモードで完全動作
   - モジュール解決と実行

### ⚠️ 制限事項
1. **LLVMコンパイル時の問題**
   - モジュール修飾名の解決に問題あり
   - 一部のケースでハングする

2. **未実装機能**
   - default export
   - エイリアス（`import ... as ...`）
   - 再エクスポート
   - 変数のエクスポート

## 提案する拡張仕様

### 1. Default Export

#### 関数のデフォルトエクスポート
```cm
// utils.cm
export default int process(int x) {
    return x * 2;
}

// main.cm
import process from utils;
int result = process(10);  // 20
```

#### インターフェースのデフォルトエクスポート
```cm
// calculator_interface.cm
export default interface ICalculator {
    int add(int x);
    int multiply(int x);
};

// 使用側では実装が必要
import ICalculator from calculator_interface;  // インターフェースをインポート
```

#### 構造体のデフォルトエクスポート（関連実装含む）
```cm
// calculator.cm
export default struct Calculator {
    int value;
};

// インターフェース定義と実装（暗黙的にエクスポート）
interface ICalculator {
    int add(int x);
    int multiply(int x);
}

impl ICalculator for Calculator {
    int add(int x) {
        this.value += x;
        return this.value;
    }

    int multiply(int x) {
        this.value *= x;
        return this.value;
    }
}

// main.cm
import Calculator from calculator;  // 構造体と関連実装も含む
Calculator calc = { value: 10 };
int result = calc.add(5);  // 15
```

#### 複合デフォルトエクスポート（提案）
```cm
// math_lib.cm
export default {
    struct Calculator {
        int value;
    };

    // この中に関連する全ての実装を記述
    impl Calculator {
        int add(int x) {
            this.value += x;
            return this.value;
        }
    }

    // 関連する関数も含められる
    Calculator create(int initial) {
        return { value: initial };
    }
}

// main.cm
import MathLib from math_lib;
MathLib::Calculator calc = MathLib::create(10);
```

#### 混合エクスポート
```cm
// math.cm
export default struct MathLib {
    // デフォルトエクスポート構造体
};

export int add(int a, int b) {
    return a + b;
}

export int multiply(int a, int b) {
    return a * b;
}

// 使用側
import MathLib, { add, multiply } from math;
```

### 2. エイリアス機能

```cm
// 個別エイリアス
import { add as addition } from math;
int result = addition(3, 4);

// モジュール全体のエイリアス
import math as Mathematics;
int result = Mathematics::add(3, 4);

// デフォルトエクスポートのエイリアス
import { default as Calc } from calculator;
```

### 3. 再エクスポート

```cm
// math_extended.cm
export { add, multiply } from math;  // 再エクスポート
export { subtract as minus } from math;  // エイリアス付き再エクスポート

// 独自の追加関数
export int divide(int a, int b) {
    return a / b;
}
```

### 4. 静的変数とコンスタントのエクスポート

```cm
// constants.cm
export static const float PI = 3.14159;        // 静的定数
export static const int MAX_SIZE = 1024;        // 静的定数
export static int global_counter = 0;           // 静的変数（可変）

// main.cm
import { PI, MAX_SIZE, global_counter } from constants;
float circumference = 2 * PI * radius;
global_counter++;  // グローバル静的変数の操作
```

### 5. ワイルドカードインポート

```cm
// すべてをインポート
import * from math;
int result = add(3, 4);  // 名前空間なしで使用

// 名前空間付きワイルドカード
import * as MathUtils from math;
int result = MathUtils::add(3, 4);
```

## モジュール解決アルゴリズム

### 現在の実装
1. カレントディレクトリを検索
2. 標準ライブラリパスを検索
3. 環境変数 `CM_MODULE_PATH` のパスを検索

### 提案する改善
```
1. 相対パス解決
   - "./module" → 同じディレクトリ
   - "../module" → 親ディレクトリ

2. 絶対パス解決
   - "std::io" → 標準ライブラリ
   - "@company/lib" → サードパーティ

3. パッケージマネージャー統合
   - cm.toml でバージョン管理
   - 依存関係の自動解決
```

## 実装計画

### Phase 1: Default Export（優先度：高）
```cm
// 実装ステップ
1. ASTに default フラグを追加
2. パーサーで "export default" を認識
3. シンボルテーブルにデフォルトエクスポートを記録
4. import文でデフォルトインポートを解決
```

### Phase 2: 変数エクスポート（優先度：中）
```cm
// 実装ステップ
1. エクスポート可能な要素を拡張
2. 変数の初期化順序を管理
3. グローバル変数のリンケージ処理
```

### Phase 3: エイリアスと再エクスポート（優先度：低）
```cm
// 実装ステップ
1. ASTにエイリアス情報を追加
2. シンボル解決時にエイリアスを処理
3. 再エクスポートのチェーン解決
```

## テスト例

### Default Exportのテスト
```cm
// test_default.cm
export default struct Vector {
    float x, y, z;
};

// Vector用のメソッドを別途定義
float vectorLength(Vector* v) {
    return sqrt(v->x * v->x + v->y * v->y + v->z * v->z);
}

export Vector create(float x, float y, float z) {
    Vector v;
    v.x = x; v.y = y; v.z = z;
    return v;
}

// test_main.cm
import Vector, { create } from test_default;

int main() {
    Vector v = create(1.0, 2.0, 3.0);
    float len = vectorLength(&v);
    println("Length: {len}");
    return 0;
}
```

## LLVM実装の修正案

### 現在の問題
```cpp
// mir_to_llvm.cpp の問題箇所
const std::string& funcName = std::get<std::string>(operand.data);
auto func = module->getFunction(funcName);  // "module::func" が見つからない
```

### 修正案
```cpp
// モジュール修飾名の処理
std::string resolvedName = funcName;
if (funcName.find("::") != std::string::npos) {
    // 1. モジュール修飾を除去
    // 2. マングル名を生成
    // 3. 関数を検索
    resolvedName = mangleModuleName(funcName);
}
auto func = module->getFunction(resolvedName);
```

## セキュリティと可視性

### 循環参照の検出
```cm
// a.cm
import b;

// b.cm
import a;  // エラー：循環参照
```

## パフォーマンス考慮事項

1. **遅延読み込み**: 使用時まで実際のロードを遅延
2. **キャッシング**: 一度ロードしたモジュールをキャッシュ
3. **ツリーシェイキング**: 未使用のエクスポートを削除

## 既知の問題と TODO

### 高優先度
- [ ] Default export の実装
- [ ] LLVMバックエンドのモジュール名解決修正
- [ ] 変数エクスポートのサポート

### 中優先度
- [ ] エイリアス機能の実装
- [ ] 再エクスポートのサポート
- [ ] モジュールのバージョニング

### 低優先度
- [ ] パッケージマネージャーの設計
- [ ] 動的インポート