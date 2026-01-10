# ビルトイン関数とイテレータの統合戦略

作成日: 2026-01-10
対象バージョン: v0.11.0

## エグゼクティブサマリー

Cm言語には既に配列/スライス用のビルトインmap/filter/reduce機能が実装されています。本文書では、これらの既存機能と新規STLイテレータシステムを統合し、両方の長所を活かす戦略を提案します。

## 1. 現状分析

### 1.1 既存ビルトイン機能の実装詳細

#### コンパイラ内部での処理フロー

```
ソースコード: arr.map(f)
    ↓
[Parser] メンバー関数呼び出しとして認識
    ↓
[Type Checker] call.cpp:434-478
    - "map"を検出
    - コールバックの型チェック
    - 戻り値型を推論
    ↓
[HIR Lowering] expr.cpp:696-874
    - __builtin_array_map に変換
    - 引数: (配列ポインタ, サイズ, コールバック)
    ↓
[Backend]
    ├─ LLVM: ネイティブループ生成
    ├─ JavaScript: Array.map()に変換
    └─ Interpreter: ビルトイン実装呼び出し
```

#### ビルトイン関数一覧

| 関数 | シグネチャ | LLVM関数名 | 実装状況 |
|------|-----------|------------|----------|
| map | `T[].map(U*(T)) -> U[]` | `__builtin_array_map` | ✅ 完全実装 |
| filter | `T[].filter(bool*(T)) -> T[]` | `__builtin_array_filter` | ✅ 完全実装 |
| reduce | `T[].reduce(U*(U,T), U) -> U` | `__builtin_array_reduce` | ✅ 完全実装 |
| forEach | `T[].forEach(void*(T)) -> void` | `__builtin_array_forEach` | ✅ 完全実装 |
| some | `T[].some(bool*(T)) -> bool` | `__builtin_array_some` | ✅ 完全実装 |
| every | `T[].every(bool*(T)) -> bool` | `__builtin_array_every` | ✅ 完全実装 |

### 1.2 パフォーマンス特性

```cm
// ビルトイン版（現在）
int[] arr = [1, 2, 3, 4, 5];
int[] doubled = arr.map((int x) => { return x * 2; });

// コンパイラは以下のように最適化：
// - LLVMバックエンド: ベクトル化可能なループ
// - JavaScriptバックエンド: ネイティブArray.map()
// - 中間配列の生成は避けられない
```

**利点:**
- コンパイラレベルの最適化
- プラットフォーム固有の最適化（SIMD、JavaScript最適化）
- シンプルな構文

**欠点:**
- 中間配列の生成（メモリオーバーヘッド）
- 遅延評価不可
- カスタムコンテナに適用不可

## 2. 統合アーキテクチャ

### 2.1 二層アプローチ

```
┌─────────────────────────────────────────┐
│           ユーザーAPI層                   │
├─────────────────────────────────────────┤
│  配列/スライス  │  STLコンテナ            │
│  ・ビルトイン   │  ・イテレータメソッド     │
│  ・高速         │  ・汎用的               │
├─────────────────────────────────────────┤
│           実装層                         │
├─────────────────────────────────────────┤
│ __builtin_*    │  イテレータ実装          │
│ (コンパイラ)    │  (ライブラリ)           │
└─────────────────────────────────────────┘
```

### 2.2 統一インターフェース設計

```cm
// src/std/core/functional_traits.cm

// 関数型操作の統一インターフェース
interface Mappable<T> {
    <U> /* container of U */ map(U (*f)(T));
}

interface Filterable<T> {
    /* same container */ filter(bool (*pred)(T));
}

interface Reducible<T> {
    <U> U reduce(U (*f)(U, T), U initial);
}

// 配列拡張（コンパイラが自動実装）
impl<T> T[] for Mappable<T> {
    // ビルトイン関数を使用（コンパイラが生成）
    <U> U[] map(U (*f)(T)) {
        return __builtin_array_map(self, self.length, f);
    }
}

// Vector実装（ライブラリ）
impl<T> Vec<T> for Mappable<T> {
    <U> Vec<U> map(U (*f)(T)) {
        Vec<U> result;
        result.reserve(self.len);
        for (auto it = self.begin(); it != self.end(); ++it) {
            result.push(f(*it));
        }
        return result;
    }
}
```

## 3. 実装戦略

### 3.1 ハイブリッド実装

```cm
// src/std/collections/vec_hybrid.cm

impl<T> Vec<T> {
    // 方法1: 内部配列を活用してビルトイン関数を使用
    <U> Vec<U> map_builtin(U (*f)(T)) {
        // Vectorの内部配列を取得
        T* arr_ptr = self.data;
        int arr_len = self.len;

        // ビルトイン関数呼び出し
        U* result_arr = __builtin_array_map(arr_ptr, arr_len, f);

        // 結果をVectorにラップ
        Vec<U> result;
        result.data = result_arr;
        result.len = arr_len;
        result.capacity = arr_len;

        return result;
    }

    // 方法2: イテレータベース実装
    <U> Vec<U> map_iterator(U (*f)(T)) {
        Vec<U> result;
        result.reserve(self.len);

        for (auto it = self.begin(); it != self.end(); ++it) {
            result.push(f(*it));
        }

        return result;
    }

    // 方法3: 条件付き選択
    <U> Vec<U> map(U (*f)(T)) {
        #ifdef PREFER_BUILTIN_OPTIMIZATION
            // 単純な変換はビルトイン使用
            if (!has_side_effects(f)) {
                return self.map_builtin(f);
            }
        #endif

        // 複雑な場合やデバッグ時はイテレータ版
        return self.map_iterator(f);
    }
}
```

### 3.2 遅延評価の実装

```cm
// src/std/iter/lazy.cm

// 遅延評価版map（イテレータチェーン）
struct LazyMap<T> {
    Vec<T>* source;
    /* function chain */
}

impl<T> Vec<T> {
    // 遅延評価版を返す
    LazyMap<T> lazy() {
        LazyMap<T> lazy;
        lazy.source = &self;
        return lazy;
    }
}

impl<T> LazyMap<T> {
    // 遅延map
    <U> LazyMap<U> map(U (*f)(T)) {
        // 関数をチェーンに追加（実行しない）
        // ...
        return /* new lazy map */;
    }

    // 遅延filter
    LazyMap<T> filter(bool (*pred)(T)) {
        // 述語をチェーンに追加
        // ...
        return /* new lazy map */;
    }

    // 実行してVectorに収集
    Vec<T> collect() {
        // ここで初めて実行
        Vec<T> result;

        // チェーンされた操作を順次適用
        for (auto it = self.source->begin();
             it != self.source->end();
             ++it) {
            T value = *it;
            // apply transformations...
            result.push(value);
        }

        return result;
    }
}

// 使用例
Vec<int> numbers;
// ... 初期化

// 遅延評価（メモリ効率的）
Vec<int> result = numbers.lazy()
    .filter((int x) => { return x % 2 == 0; })
    .map((int x) => { return x * x; })
    .filter((int x) => { return x < 100; })
    .collect();  // ここで実行

// 即時評価（ビルトイン使用）
Vec<int> result2 = numbers
    .filter((int x) => { return x % 2 == 0; })  // 中間配列生成
    .map((int x) => { return x * x; })          // 中間配列生成
    .filter((int x) => { return x < 100; });    // 中間配列生成
```

## 4. コンパイラ拡張提案

### 4.1 ビルトイン関数の拡張

```cpp
// src/frontend/types/checking/call.cpp の修正案

if (member.member == "map") {
    // 既存の配列処理
    if (obj_type->is_array() || obj_type->is_slice()) {
        // 既存のビルトイン処理
        return handle_builtin_map(member);
    }

    // 新規：Vectorや他のコンテナ
    if (obj_type->is_vector() ||
        obj_type->implements("Mappable")) {
        // ライブラリ関数呼び出しとして処理
        return handle_library_map(member);
    }
}

// 新しいヘルパー関数
TypePtr handle_library_map(const MemberCall& member) {
    // Vec<T>::map のような通常のメソッド呼び出しとして処理
    // 型チェックとオーバーロード解決
    // ...
}
```

### 4.2 最適化パス

```cpp
// src/mir/optimization/container_opts.cpp (新規)

class ContainerOptimizationPass : public OptimizationPass {
public:
    void optimize(MIRFunction& func) override {
        // Vector.map → __builtin_array_map 変換
        for (auto& block : func.blocks) {
            for (auto& inst : block.instructions) {
                if (auto call = dynamic_cast<CallInst*>(inst.get())) {
                    optimize_container_call(call);
                }
            }
        }
    }

private:
    void optimize_container_call(CallInst* call) {
        // Vec<T>::map を検出
        if (call->function_name == "Vec_map") {
            // 条件を満たす場合、ビルトイン版に置換
            if (can_use_builtin_map(call)) {
                replace_with_builtin_map(call);
            }
        }
    }

    bool can_use_builtin_map(CallInst* call) {
        // 以下の条件をチェック:
        // 1. コールバックが純粋関数
        // 2. 例外を投げない
        // 3. メモリ割り当てしない
        return is_pure_function(call->callback) &&
               !may_throw(call->callback) &&
               !allocates_memory(call->callback);
    }
};
```

## 5. 移行パス

### 5.1 段階的移行計画

#### Phase 1: 基盤構築（Week 1）
```cm
// 既存コードは変更なし
int[] arr = [1, 2, 3];
int[] doubled = arr.map((int x) => x * 2);  // ビルトイン使用

// 新規Vectorサポート
Vec<int> vec;
vec.push(1); vec.push(2); vec.push(3);
Vec<int> vec_doubled = vec.map((int x) => x * 2);  // イテレータ版
```

#### Phase 2: 統一API（Week 2）
```cm
// 統一インターフェース
interface Container<T> {
    <U> Container<U> map(U (*f)(T));
    Container<T> filter(bool (*p)(T));
}

// 配列もVectorも同じインターフェース
Container<int> c1 = arr;     // 配列
Container<int> c2 = vec;     // Vector
Container<int> result = c1.map(double).filter(is_even);
```

#### Phase 3: 最適化（Week 3）
```cm
// コンパイラが自動選択
Vec<int> vec;
// 単純な操作 → ビルトイン使用
Vec<int> doubled = vec.map((int x) => x * 2);

// 複雑な操作 → イテレータ使用
Vec<int> complex = vec.map((int x) => {
    // 複雑な処理
    return compute_complex(x);
});
```

#### Phase 4: 遅延評価（Week 4）
```cm
// 明示的な遅延評価
auto lazy_result = vec.lazy()
    .map(f)
    .filter(g)
    .take(10);

// 必要時に評価
Vec<int> result = lazy_result.collect();
```

### 5.2 互換性維持

```cm
// src/std/compat/array_extensions.cm

// 既存の配列APIを拡張
impl<T> T[] {
    // イテレータを取得（新規）
    ArrayIterator<T> begin() {
        return ArrayIterator<T>(&self[0], &self[0] + self.length);
    }

    ArrayIterator<T> end() {
        return ArrayIterator<T>(&self[0] + self.length,
                                &self[0] + self.length);
    }

    // Vectorへの変換（新規）
    Vec<T> to_vec() {
        Vec<T> result;
        result.reserve(self.length);
        for (int i = 0; i < self.length; i++) {
            result.push(self[i]);
        }
        return result;
    }
}

// スライスも同様
impl<T> T[] /* slice */ {
    SliceIterator<T> begin() { /* ... */ }
    SliceIterator<T> end() { /* ... */ }
    Vec<T> to_vec() { /* ... */ }
}
```

## 6. パフォーマンス最適化

### 6.1 ベンチマーク設計

```cm
// benchmarks/builtin_vs_iterator.cm

struct BenchmarkResults {
    double builtin_time;
    double iterator_time;
    double lazy_time;
    double ratio;
}

BenchmarkResults benchmark_map(int size) {
    // データ準備
    int[] arr = new int[size];
    Vec<int> vec;
    for (int i = 0; i < size; i++) {
        arr[i] = i;
        vec.push(i);
    }

    // ビルトイン版
    double t1 = get_time();
    int[] result1 = arr.map((int x) => x * 2);
    double builtin_time = get_time() - t1;

    // イテレータ版
    double t2 = get_time();
    Vec<int> result2 = vec.map((int x) => x * 2);
    double iterator_time = get_time() - t2;

    // 遅延評価版
    double t3 = get_time();
    Vec<int> result3 = vec.lazy()
        .map((int x) => x * 2)
        .collect();
    double lazy_time = get_time() - t3;

    return BenchmarkResults{
        builtin_time,
        iterator_time,
        lazy_time,
        iterator_time / builtin_time
    };
}

// 期待される結果：
// - 小配列（< 1000要素）: ビルトイン最速
// - 大配列（> 10000要素）: 遅延評価有利（メモリ効率）
// - 複雑な操作チェーン: 遅延評価が大幅に有利
```

### 6.2 最適化テクニック

```cm
// src/std/optimization/fusion.cm

// ループ融合の例
impl<T> Vec<T> {
    // 悪い例：2回のループ
    Vec<T> map_then_filter_bad(T (*f)(T), bool (*p)(T)) {
        Vec<T> mapped = self.map(f);      // 1回目のループ
        return mapped.filter(p);          // 2回目のループ
    }

    // 良い例：1回のループに融合
    Vec<T> map_then_filter_good(T (*f)(T), bool (*p)(T)) {
        Vec<T> result;
        for (auto it = self.begin(); it != self.end(); ++it) {
            T transformed = f(*it);
            if (p(transformed)) {
                result.push(transformed);
            }
        }
        return result;
    }

    // コンパイラが自動融合（将来的な拡張）
    #[fuse_loops]
    Vec<T> map_then_filter_auto(T (*f)(T), bool (*p)(T)) {
        return self.map(f).filter(p);
        // コンパイラが自動的に融合版を生成
    }
}
```

## 7. エラー処理とデバッグ

### 7.1 エラーメッセージの改善

```cm
// src/std/debug/iterator_debug.cm

struct DebugIterator<T> {
    T* ptr;
    T* begin_ptr;
    T* end_ptr;
    string container_name;
    int operation_count;
}

impl<T> DebugIterator<T> {
    T operator*() {
        if (self.ptr < self.begin_ptr) {
            panic("Iterator {} underflow: attempted to access before begin (operation #{})",
                  self.container_name, self.operation_count);
        }
        if (self.ptr >= self.end_ptr) {
            panic("Iterator {} overflow: attempted to access past end (operation #{})",
                  self.container_name, self.operation_count);
        }
        self.operation_count = self.operation_count + 1;
        return *self.ptr;
    }
}

// デバッグビルドで自動的に使用
#ifdef DEBUG
    typedef DebugIterator<T> VecIterator<T>;
#else
    typedef PointerIterator<T> VecIterator<T>;
#endif
```

### 7.2 プロファイリングサポート

```cm
// src/std/profiling/iterator_profiling.cm

struct IteratorStats {
    int total_iterations;
    int cache_misses;
    double total_time;
    HashMap<string, int> operation_counts;
}

static HashMap<string, IteratorStats> global_stats;

struct ProfilingIterator<T> {
    PointerIterator<T> inner;
    string name;
    double start_time;
}

impl<T> ProfilingIterator<T> {
    void advance() {
        global_stats.get(self.name)->total_iterations++;
        self.inner.advance();
    }

    ~self() {
        double elapsed = get_time() - self.start_time;
        global_stats.get(self.name)->total_time += elapsed;
    }
}

// プロファイリング結果の表示
void print_iterator_stats() {
    println("Iterator Performance Statistics:");
    for (auto& [name, stats] : global_stats) {
        println("  {}: {} iterations, {:.2f}ms total",
                name, stats.total_iterations, stats.total_time);
    }
}
```

## 8. 実装チェックリスト

### コンパイラ側の変更

- [ ] Vec<T>型の認識（type checker）
- [ ] Vec<T>::mapの型推論
- [ ] 最適化パスの追加
- [ ] デバッグ情報の生成

### ライブラリ側の実装

- [ ] Vec<T>::map/filter/reduce実装
- [ ] イテレータアダプタ
- [ ] 遅延評価サポート
- [ ] デバッグイテレータ

### テスト

- [ ] ビルトイン互換性テスト
- [ ] パフォーマンステスト
- [ ] エッジケーステスト
- [ ] メモリリークテスト

### ドキュメント

- [ ] 移行ガイド
- [ ] パフォーマンスガイドライン
- [ ] APIリファレンス
- [ ] サンプルコード

## 9. リスクと緩和策

| リスク | 影響 | 緩和策 |
|--------|------|--------|
| 既存コードの破壊 | 高 | 完全な後方互換性維持 |
| パフォーマンス劣化 | 中 | ベンチマークによる継続的監視 |
| API の複雑化 | 中 | 段階的導入、明確なドキュメント |
| コンパイル時間増加 | 低 | テンプレート実体化の最適化 |

## 10. まとめと推奨事項

### 推奨アーキテクチャ

1. **ビルトイン関数は維持**: 配列/スライスの高速処理
2. **イテレータは新規追加**: STLコンテナの汎用処理
3. **統一インターフェース**: 両者を抽象化
4. **遅延評価オプション**: メモリ効率重視の処理

### 実装優先順位

1. **最優先**: Vec<T>のイテレータ実装
2. **高**: ビルトイン互換のmap/filter/reduce
3. **中**: 遅延評価サポート
4. **低**: 自動最適化

### 期待される成果

- **パフォーマンス**: ビルトインと同等（単純な操作）
- **柔軟性**: 任意のコンテナに適用可能
- **メモリ効率**: 遅延評価で中間配列削減
- **互換性**: 既存コード完全動作

この統合により、Cm言語はC++のパワーとJavaScript/Rustの使いやすさを併せ持つ、真にモダンなシステムプログラミング言語となります。