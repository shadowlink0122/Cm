# イテレータシステム設計書

## 概要

Cm言語の既存interface/impl機構を活用した、拡張性の高いイテレータシステムを設計する。

---

## 1. コア設計

### 1.1 Iterable インターフェース

```cm
interface Iterable<T> {
    Iterator<T> iter();
}
```

### 1.2 Iterator 構造体

```cm
// イテレータ結果を表す構造体
struct IterItem<T> {
    T item;      // 現在の要素
    int index;   // 現在のインデックス
}

// オプション：終端判定用
struct IterResult<T> {
    bool done;       // 終了フラグ
    T item;          // 現在の要素（done=falseの場合有効）
    int index;       // 現在のインデックス
}
```

### 1.3 Iterator インターフェース

```cm
interface Iterator<T> {
    // 次の要素を取得（終端ならnullまたはdone=true）
    IterResult<T> next();
    
    // まだ要素があるか
    bool has_next();
    
    // 内部状態: インスタンスごとに独立
    private int _index;    // 🆕 インターフェース内部状態
}
```

---

## 2. 新機能：インターフェース内部状態

### 2.1 設計要件

```cm
interface Iterator<T> {
    private int _index;      // インスタンス固有の状態
    private T* _data;        // データへのポインタ
    private int _length;     // データ長
    
    IterResult<T> next();
}
```

### 2.2 実装メカニズム

| 要素 | 説明 |
|------|------|
| **インスタンス状態** | `impl`時に構造体に埋め込まれる |
| **初期化** | `iter()`呼び出し時にコンストラクタで初期化 |
| **ライフタイム** | イテレータオブジェクトのスコープに一致 |

### 2.3 生成コード例

```cm
// ユーザーが書くコード
interface Iterator<T> {
    private int _index;
    IterResult<T> next();
}

// コンパイラが内部で生成
struct __Iterator_int {
    int _index;           // インターフェース内部状態
    int* _data;           // 追加のコンテキスト
    int _length;
}

fn __Iterator_int__next(__Iterator_int* self) -> IterResult_int {
    if (self->_index >= self->_length) {
        return IterResult_int { done: true, ... };
    }
    int idx = self->_index;
    self->_index++;
    return IterResult_int { done: false, item: self->_data[idx], index: idx };
}
```

---

## 3. 配列/スライスへの自動実装

### 3.1 組み込み自動実装

```cm
// コンパイラが暗黙的に生成
impl<T> Iterable<T> for T[] {
    Iterator<T> iter() {
        // ArrayIterator<T>を返す
        return ArrayIterator<T> {
            _data: &this[0],
            _length: this.len(),
            _index: 0
        };
    }
}
```

### 3.2 使用例

```cm
int[5] arr = {1, 2, 3, 4, 5};

// 単純なfor-in（item only）
for (item in arr) {
    println("{}", item);
}

// インデックス付きfor-in
for ((index, item) in arr.iter()) {
    println("{}: {}", index, item);
}

// 範囲イテレータ
for (i in 0..10) {
    println("{}", i);
}
```

---

## 4. 範囲イテレータ (Range)

### 4.1 Range構造体

```cm
struct Range {
    int start;
    int end;
    int step;    // デフォルト: 1
}

impl Iterable<int> for Range {
    Iterator<int> iter() {
        return RangeIterator {
            _current: this.start,
            _end: this.end,
            _step: this.step
        };
    }
}
```

### 4.2 構文

| 構文 | 意味 |
|------|------|
| `0..10` | 0から9まで（endは含まない） |
| `0..=10` | 0から10まで（endを含む） |
| `0..10:2` | 0から9まで、ステップ2 |

---

## 5. 実装フェーズ

| Phase | 内容 | 優先度 |
|-------|------|--------|
| **1** | Range構造体 + `0..10`構文 | 🔴高 |
| **2** | Iterable/Iterator インターフェース定義 | 🔴高 |
| **3** | 配列/スライスへの自動実装 | 🟠中 |
| **4** | インターフェース内部状態 | 🟠中 |
| **5** | `(index, item)` 分解構文 | 🟡低 |

---

## 6. 検討事項

### 6.1 インターフェース内部状態の選択肢

| オプション | 説明 | 複雑度 |
|------------|------|--------|
| **A: 完全サポート** | interface内でprivate fieldを定義可能 | 高 |
| **B: Iterator専用** | Iterator系のみ内部状態を許可 | 中 |
| **C: 外部構造体** | Iterator状態は別構造体で管理 | 低 |

### 6.2 推奨

**オプションC（外部構造体）** を最初に実装し、必要に応じてオプションAに拡張することを推奨。

理由:
- 既存のinterface/impl機構への変更が最小
- 段階的な実装が可能
- 将来の拡張パスが明確

---

## ユーザー確認事項

1. **インターフェース内部状態**: 完全サポート(A)と外部構造体(C)、どちらを優先しますか？
2. **範囲構文**: `0..10` と `0..=10` の両方が必要ですか？
3. **分解構文**: `(index, item)` の構文を許可しますか？それとも `.index` `.item` フィールドアクセスのみ？
