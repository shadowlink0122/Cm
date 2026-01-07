[English](method_chaining.en.html)

# 真のメソッドチェーン機能設計書 v1.1

## 概要

関数呼び出しの戻り値に対して、連続的にメンバアクセス、メソッド呼び出し、配列アクセスを行う機能を実装する。

### 目標構文

```cm
// メソッドチェーン
obj.method1().method2().method3()

// 戻り値のフィールドアクセス
obj.getPoint().x

// 戻り値の配列アクセス
obj.getArray()[0]

// 複合チェーン
container.getItems()[0].process().value
```

## 設計方針

| 項目 | 決定 | 理由 |
|------|------|------|
| 直接メソッド定義 | ❌ | インターフェース経由で明示的な契約を強制 |
| ポインタ自動デリファレンス | ❌ | `ptr->field`を明示的に使用、一貫性重視 |
| メソッド対象 | インターフェース実装のみ | `with`自動実装含む |

### ポインタアクセス規則

```cm
Point p;     // 値型
Point* ptr;  // ポインタ型

p.x          // ✅ 値型のフィールド
p.move()     // ✅ 値型のメソッド
ptr->x       // ✅ ポインタのフィールド
ptr->move()  // ✅ ポインタのメソッド
ptr.x        // ❌ エラー
ptr.move()   // ❌ エラー
```

## 現状分析

### パーサー ✅ 対応済み

`parse_postfix()`（parser_expr.cpp:343-556）で既にチェーン構文をサポート。

### 型チェッカー ⚠️ 修正必要

関数呼び出しの戻り値型がメンバアクセスに伝播されていない。

### HIR/MIR Lowering ⚠️ 検証必要

複雑な式からのメンバアクセスが正しく処理されるか要確認。

## 実装計画

### Phase 1: 型チェッカー拡張

#### 1.1 戻り値型の追跡
```cpp
// call.cpp: check_call()
// 戻り値型を式の型情報として正しく設定
```

#### 1.2 メンバアクセスの型解決
```cpp
// expr.cpp: check_member()
// オブジェクト式の型を再帰的に解決
```

### Phase 2: 検証と修正（必要に応じて）

HIR/MIRでの動作検証。問題があれば修正。

## テストケース

```cm
// tests/test_programs/chaining/true_method_chain.cm
interface Movable {
    Point move(int dx, int dy);
}

impl Point for Movable {
    Point move(int dx, int dy) {
        return Point { x: self.x + dx, y: self.y + dy };
    }
}

int main() {
    Point p = Point { x: 1, y: 2 };
    
    // チェーン: p.move(5,5).move(10,10).x
    int x = p.move(5, 5).move(10, 10).x;
    assert(x == 16);  // 1 + 5 + 10
    
    return 0;
}
```

## 見積もり

| フェーズ | 内容 | 時間 |
|----------|------|------|
| Phase 1 | 型チェッカー拡張 | 3-5時間 |
| Phase 2 | 検証・修正 | 1-2時間 |
| **合計** | | **4-7時間** |