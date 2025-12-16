# チュートリアル修正サマリー

**実施日:** 2024-12-16  
**理由:** 正式仕様との齟齬を修正

## 🔧 修正内容

### 1. switch文の構文修正

**問題:** C++風の`case :`構文を使用していた  
**修正:** Cm正式構文`case()`に修正

#### 修正前（誤り）
```cm
switch (value) {
    case 1:
        println("One");
        break;
    default:
        println("Other");
        break;
}
```

#### 修正後（正しい）
```cm
switch (value) {
    case(1) {
        println("One");
    }
    else {
        println("Other");
    }
}
```

**修正ファイル:**
- `basics/control-flow.md`
- `basics/functions.md`
- `types/enums.md`
- `advanced/match.md`

### 2. フォールスルーの説明削除

**問題:** Cmにはフォールスルーがないのに説明していた  
**修正:** フォールスルーの説明を削除し、自動breakを明記

**変更点:**
- ✅ `break`不要の説明を追加
- ✅ `case()`は自動的にbreakすることを明記
- ✅ フォールスルーの例を削除
- ✅ ORパターン`case(1 | 2 | 3)`の説明を追加

**修正ファイル:**
- `basics/control-flow.md`

### 3. 構造体リテラル初期化の削除

**問題:** 未実装の構造体リテラル初期化を説明していた  
**修正:** 削除し、フィールド初期化のみに統一

#### 修正前（誤り - 未実装）
```cm
Rectangle rect = Rectangle{100, 50, "blue"};
Person alice = Person{"Alice", 25, 165.5};
```

#### 修正後（正しい - 実装済み）
```cm
Point p1;
p1.x = 10;
p1.y = 20;

// またはコンストラクタ
Point p2(10, 20);
```

**修正ファイル:**
- `types/structs.md`
- `types/enums.md`

### 4. this → self の修正

**問題:** コンストラクタ内で`this`を使用していた  
**修正:** `self`に修正

#### 修正前（誤り）
```cm
impl Point {
    self() {
        this.x = 0;
        this.y = 0;
    }
}
```

#### 修正後（正しい）
```cm
impl Point {
    self() {
        self.x = 0;
        self.y = 0;
    }
}
```

**修正ファイル:**
- `types/structs.md`

### 5. print()関数の削除

**問題:** 存在しない`print()`関数を使用していた  
**修正:** `println()`のみに統一

**注意:** Cmには改行なしの`print()`関数は標準ライブラリにありません。

**修正ファイル:**
- `basics/control-flow.md`

### 6. default → else の修正

**問題:** switchで`default`を使用していた  
**修正:** `else`に修正

#### 修正前（誤り）
```cm
switch (x) {
    case(1) { println("One"); }
    default:
        println("Other");
}
```

#### 修正後（正しい）
```cm
switch (x) {
    case(1) { println("One"); }
    else {
        println("Other");
    }
}
```

**修正ファイル:**
- `basics/control-flow.md`
- `basics/functions.md`
- `types/enums.md`
- `advanced/match.md`

## ✅ 確認済みの正しい構文

### switch文の完全な構文

```cm
switch (value) {
    case(1) { /* ... */ }           // 単一値
    case(2 | 3) { /* ... */ }       // ORパターン
    case(10...20) { /* ... */ }     // 範囲パターン
    case(1...5 | 10) { /* ... */ }  // 複合パターン
    else { /* ... */ }              // デフォルトケース
}
```

**特徴:**
- `case(値)` - 括弧で囲む
- `{ }` - 各caseはブロック
- `break不要` - 自動的にbreak
- `else` - デフォルトケース
- `|` - ORパターン
- `...` - 範囲パターン

### コンストラクタ

```cm
impl StructName {
    self() {
        self.field = value;  // selfを使う
    }
    
    overload self(int x) {
        self.field = x;
    }
}
```

### 構造体初期化

```cm
// フィールド初期化
Point p;
p.x = 10;
p.y = 20;

// コンストラクタ
Point p2(10, 20);
```

### match式

```cm
match (value) {
    0 => println("zero"),
    1 => println("one"),
    _ => println("other"),
}
```

**違い:**
- match: `0 =>` (矢印)
- switch: `case(0) { }` (caseとブロック)

## 📊 修正統計

| カテゴリ | 修正項目 | 影響ファイル数 |
|---------|---------|---------------|
| switch構文 | case: → case() | 4 |
| フォールスルー | 説明削除 | 1 |
| 構造体リテラル | 削除 | 2 |
| this/self | 修正 | 1 |
| print() | 削除 | 1 |
| default/else | 修正 | 4 |

**合計修正:** 6カテゴリ、延べ13箇所

## 🎯 残存する正しい記述

以下は確認済みで正しい：

- ✅ ジェネリクス構文: `<T> T func(T x)`
- ✅ インターフェース: `impl Type for Interface`
- ✅ match式: `match (x) { 0 => ..., }`
- ✅ defer文: `defer println("cleanup");`
- ✅ for-in構文: `for (n in arr)`
- ✅ enum定義: `enum Status { Ok = 0, Error = 1 }`
- ✅ 型制約: `<T: Eq + Ord>`
- ✅ with自動実装: `struct Point with Eq + Clone`
- ✅ typedef: `typedef Integer = int;`

## 📝 今後の注意点

1. **仕様確認** - 実装前に`CANONICAL_SPEC.md`を確認
2. **テストケース参照** - `tests/test_programs/`の実際の構文を参照
3. **未実装機能の明記** - 未実装機能は記載しない（将来実装予定も含む）
4. **実装状況の更新** - v0.9.0で何が実装されているか確認

## 🚫 実装されない機能

- **var キーワード** - 型推論は実装されません。型は常に明示的に指定します。

---

**更新者:** AI Assistant  
**確認方法:** CANONICAL_SPEC.md + テストケース検証  
**対象バージョン:** v0.9.0
