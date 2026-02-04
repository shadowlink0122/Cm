# JS生成改善設計

## 概要
JavaScriptコード生成の品質と互換性を向上させる。

## 現状の課題

### 1. スキップテスト残存
- `string/string_slice` - 負のスライスインデックス未対応
- `slice/slice_subslice` - 出力不一致
- `interface/interface_as_param` - 動的ディスパッチ未対応

### 2. 構造制御フロー
- if/else再構築が不完全
- 一部のネストした制御フローで問題

### 3. インターフェース動的ディスパッチ
- vtable生成なし
- ポリモーフィズムが限定的

---

## 改善計画

### Phase 1: スライス修正
```javascript
// 現在: 負のインデックス未対応
arr.slice(-3)  // エラー

// 対応後
function cm_slice(arr, start, end) {
    if (start < 0) start = arr.length + start;
    if (end < 0) end = arr.length + end;
    return arr.slice(start, end);
}
```

### Phase 2: 動的ディスパッチ
```javascript
// Cmコード
interface Printable {
    fn print(self: *Self);
}

// 生成されるJS
function __dispatch_Printable_print(obj) {
    const vtable = obj.__vtable__;
    return vtable.print(obj);
}
```

### Phase 3: 制御フロー再構築
- CFG（制御フローグラフ）解析改善
- 構造化例外処理対応

---

## テスト改善
| テスト | 問題 | 修正方針 |
|--------|------|----------|
| `string_slice` | 負インデックス | ラッパー関数 |
| `slice_subslice` | 出力不一致 | 比較ロジック修正 |
| `interface_as_param` | 動的ディスパッチ | vtable生成 |

---

## 優先順位
1. 🔴 負インデックス対応 - 簡単
2. 🟠 動的ディスパッチ - 複雑だが重要
3. 🟡 制御フロー改善 - 長期的課題
