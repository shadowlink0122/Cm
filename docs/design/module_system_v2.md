# Cm言語 モジュールシステム v2 設計仕様

## 概要
モジュールシステムv2では、より直感的で柔軟な構文を提供し、冗長性を削減します。

## 基本原則

### 1. モジュール宣言ルール

#### 明示的なモジュール宣言が必要な場合
```cm
// library.cm - 名前空間として使用されるモジュール
module MathLib;  // トップレベルでモジュール宣言

// この宣言により、インポート側で MathLib:: プレフィックスが使用可能
int add(int a, int b) { return a + b; }
int multiply(int a, int b) { return a * b; }

// 使用側
import MathLib;
int result = MathLib::add(3, 4);
```

#### モジュール宣言が不要な場合
```cm
// utils.cm - ファイル名が暗黙のモジュール名として機能
// module宣言なし = ファイル名(utils)がモジュール名

export int helper(int x) { return x * 2; }

// 使用側
import utils;  // ファイル名をモジュール名として使用
int result = utils::helper(5);
```

### 2. Export構文の簡潔化

#### exportブロック - ブロック内は全て自動エクスポート
```cm
module Graphics;

export {
    // このブロック内の全てが自動的にエクスポートされる
    // exportキーワードの重複は不要

    struct Point {
        float x;
        float y;
    }

    struct Rectangle {
        Point topLeft;
        Point bottomRight;
    }

    float area(Rectangle r) {
        float width = r.bottomRight.x - r.topLeft.x;
        float height = r.bottomRight.y - r.topLeft.y;
        return width * height;
    }

    // ネストされた名前空間
    namespace Drawing {
        void drawPoint(Point p) { /* ... */ }
        void drawRect(Rectangle r) { /* ... */ }
    }
}

// プライベート（エクスポートされない）
float internalHelper() { return 0.0; }
```

### 3. Import構文とエイリアス

#### 基本的なインポート
```cm
// 1. 名前空間付きインポート
import Graphics;
Graphics::Point p = Graphics::Point{10.0, 20.0};

// 2. エイリアス付きインポート
import Graphics as G;
G::Point p = G::Point{10.0, 20.0};

// 3. 特定要素の選択的インポート
import Point, Rectangle from Graphics;
Point p = Point{10.0, 20.0};  // 名前空間不要

// 4. fromを使った選択的インポート + エイリアス
import { Point as P, Rectangle as R } from Graphics;
P p = P{10.0, 20.0};

// 5. ネストされた名前空間のインポート
import Graphics::Drawing;
Drawing::drawPoint(p);

// 6. ワイルドカードインポート（注意が必要）
import * from Graphics;  // 全てを現在の名前空間に展開
```

### 4. モジュール内のサブモジュール

#### 方法1: exportブロック内でのnamespace定義
```cm
module DataStructures;

export {
    namespace Lists {
        struct Node {
            int value;
            Node* next;
        }

        struct LinkedList {
            Node* head;
            int size;
        }

        void append(LinkedList* list, int value) { /* ... */ }
    }

    namespace Trees {
        struct TreeNode {
            int value;
            TreeNode* left;
            TreeNode* right;
        }

        struct BinaryTree {
            TreeNode* root;
        }

        void insert(BinaryTree* tree, int value) { /* ... */ }
    }
}

// 使用側
import DataStructures;
DataStructures::Lists::LinkedList list;
DataStructures::Trees::BinaryTree tree;

// または
import DataStructures::Lists as Lists;
Lists::LinkedList list;
```

#### 方法2: ファイルシステムベースの階層
```
modules/
├── graphics/
│   ├── mod.cm         // module Graphics;
│   ├── shapes.cm      // サブモジュール
│   └── rendering.cm   // サブモジュール
```

```cm
// graphics/mod.cm
module Graphics;

// サブモジュールの再エクスポート
export * from ./shapes;
export * from ./rendering;

// または選択的に
export { Circle, Rectangle } from ./shapes;
export { render, clear } from ./rendering;
```

### 5. デフォルトエクスポート

```cm
// vector.cm
module Vector;

// デフォルトエクスポート - モジュール名で直接アクセス可能
export default struct Vector3 {
    float x, y, z;
}

// 追加の名前付きエクスポート
export {
    float length(Vector3 v) {
        return sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
    }

    Vector3 normalize(Vector3 v) {
        float len = length(v);
        return Vector3{v.x/len, v.y/len, v.z/len};
    }
}

// 使用側
import Vector;  // デフォルトをインポート
Vector v = Vector{1.0, 2.0, 3.0};  // Vector3型として使用

import Vector, { length, normalize } from Vector;  // デフォルト + 名前付き
float len = length(v);
```

### 6. 条件付きエクスポート

```cm
module Platform;

export {
    // プラットフォーム別のエクスポート
    #if defined(WINDOWS)
        struct WindowsFile { /* ... */ }
        typedef WindowsFile File;
    #elif defined(LINUX)
        struct LinuxFile { /* ... */ }
        typedef LinuxFile File;
    #else
        struct GenericFile { /* ... */ }
        typedef GenericFile File;
    #endif

    // 共通インターフェース
    void open(File* f, const char* path) { /* ... */ }
    void close(File* f) { /* ... */ }
}
```

## 実装例

### 完全なモジュール例

```cm
// math_extended.cm
module MathExtended;

// 他のモジュールからのインポート
import std::math;  // 標準ライブラリ

export {
    // 構造体
    struct Complex {
        float real;
        float imag;
    }

    // インターフェース
    interface Arithmetic {
        Complex add(Complex other);
        Complex multiply(Complex other);
    }

    // 実装
    impl Arithmetic for Complex {
        Complex add(Complex other) {
            return Complex{
                self.real + other.real,
                self.imag + other.imag
            };
        }

        Complex multiply(Complex other) {
            return Complex{
                self.real * other.real - self.imag * other.imag,
                self.real * other.imag + self.imag * other.real
            };
        }
    }

    // 定数
    const Complex I = Complex{0.0, 1.0};
    const Complex ZERO = Complex{0.0, 0.0};

    // 関数
    float magnitude(Complex c) {
        return std::math::sqrt(c.real * c.real + c.imag * c.imag);
    }

    // ネストされた名前空間
    namespace Transforms {
        Complex conjugate(Complex c) {
            return Complex{c.real, -c.imag};
        }

        Complex rotate(Complex c, float angle) {
            float cos_a = std::math::cos(angle);
            float sin_a = std::math::sin(angle);
            return Complex{
                c.real * cos_a - c.imag * sin_a,
                c.real * sin_a + c.imag * cos_a
            };
        }
    }
}

// プライベートヘルパー（エクスポートされない）
void debugPrint(Complex c) {
    println("Complex({c.real}, {c.imag})");
}
```

### 使用例

```cm
// main.cm
import MathExtended as Math;
import { Complex, I, magnitude } from MathExtended;
import MathExtended::Transforms;

int main() {
    // エイリアス経由
    Math::Complex c1 = Math::Complex{3.0, 4.0};

    // 直接インポート
    Complex c2 = Complex{1.0, 2.0};

    // 定数の使用
    Complex c3 = I;

    // 関数の使用
    float mag = magnitude(c1);

    // ネストされた名前空間
    Complex conj = Transforms::conjugate(c2);

    // インターフェースメソッド
    Complex sum = c1.add(c2);

    return 0;
}
```

## プリプロセッサでの処理

### 処理フロー

1. **モジュール宣言の識別**
   - `module NAME;` があれば名前空間として処理
   - なければファイル名を暗黙のモジュール名として使用

2. **Exportブロックの処理**
   ```cm
   // 入力
   export {
       struct Point { float x, y; }
       float distance(Point a, Point b) { ... }
   }

   // プリプロセッサ出力（インポート側）
   namespace ModuleName {
       struct Point { float x, y; }
       float distance(Point a, Point b) { ... }
   }
   ```

3. **Import文の変換**
   ```cm
   // 入力
   import Graphics as G;

   // プリプロセッサ出力
   namespace G = Graphics;  // エイリアスの作成
   ```

4. **選択的インポートの処理**
   ```cm
   // 入力
   import { Point, Rectangle } from Graphics;

   // プリプロセッサ出力
   using Graphics::Point;
   using Graphics::Rectangle;
   ```

## メリット

1. **冗長性の削減**: exportブロック内で重複するexportキーワードが不要
2. **明確な名前空間管理**: module宣言で名前空間を明示的に制御
3. **柔軟なインポート**: as、from、ワイルドカードなど多様な選択肢
4. **階層的な構造**: ネストされた名前空間のサポート
5. **既存コードとの互換性**: プリプロセッサベースなので段階的な実装が可能

## 実装優先順位

### Phase 1: 基本機能
- [x] プリプロセッサベースの基本的なimport/export
- [ ] module宣言の認識と名前空間生成
- [ ] exportブロックの処理

### Phase 2: エイリアスとfrom構文
- [ ] asキーワードによるエイリアス
- [ ] from構文による選択的インポート
- [ ] 名前空間エイリアスの生成

### Phase 3: 高度な機能
- [ ] ネストされた名前空間
- [ ] デフォルトエクスポート
- [ ] ワイルドカードインポート
- [ ] 条件付きエクスポート

### Phase 4: 最適化
- [ ] 未使用インポートの検出
- [ ] 循環依存の詳細な診断
- [ ] インポートの遅延評価