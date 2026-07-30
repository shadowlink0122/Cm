# ジェネリック関数のLLVM IR対訳

Cmのジェネリック関数は実行時の型消去を行わず、MIRの単相化パスが呼び出し点の型ごとに`関数名__型名`形式の具象関数を生成するため、LLVM IRにはジェネリックの痕跡が一切残らない。
本書はジェネリック関数の定義・暗黙/明示の特殊化呼び出し・制約付き定義が-O3でどのようなシンボルと本体に落ちるかの対訳を示す。

### ジェネリック関数定義と暗黙特殊化

```cm
<T> T identity(T value) {
    return value;
}

<T: Ord> T max2(T a, T b) {
    if (a > b) {
        return a;
    }
    return b;
}

int main() {
    const int a = identity(42);
    const int m1 = max2(10, 20);
    const double m2 = max2(3.14, 2.71);
    ...
}
```

```llvm
define i32 @identity__int(i32 returned %arg0) local_unnamed_addr #1 {
entry:
  ret i32 %arg0
}

define i32 @max2__int(i32 %arg0, i32 %arg1) local_unnamed_addr #2 {
entry:
  %arg0.arg1 = tail call i32 @llvm.smax.i32(i32 %arg0, i32 %arg1)
  ret i32 %arg0.arg1
}

define double @max2__double(double %arg0, double %arg1) local_unnamed_addr #1 {
entry:
  %fgt = fcmp ogt double %arg0, %arg1
  %arg0.arg1 = select i1 %fgt, double %arg0, double %arg1
  ret double %arg0.arg1
}
```

実引数の型から型引数が推論され、使用された型ごとに`identity__int`・`max2__int`・`max2__double`という独立した具象関数が生成される。
ジェネリック原本の`identity`や`max2`はLLVMに存在せず、単相化後にMIRから削除される。
-O3では各特殊化が型に最適な命令へ縮約され、int版の比較分岐は`llvm.smax`イントリンシック、double版は`fcmp`+`select`になる。
特殊化の発見と生成の仕組みは[ジェネリクスの単相化](../../generics/monomorphization.md)を参照。

### 明示特殊化呼び出し

```cm
int main() {
    const int a = identity(42);       // 暗黙特殊化（型推論）
    const int b = identity<int>(7);   // 明示特殊化
    ...
}
```

```llvm
; 最適化前IR（CM_DUMP_IR=1）の呼び出し部
%2 = call i32 @identity__int(i32 %load)
%3 = call i32 @identity__int(i32 %load2)
```

`identity<int>(7)`の明示特殊化は型推論による暗黙特殊化と同じシンボル`identity__int`へ解決され、両者はIR上で区別できない。
-O3では小さな特殊化がすべて呼び出し側へインライン化され、この例では`identity__int`の呼び出し自体が定数に畳み込まれる。

### 制約付きジェネリック関数と制約付き呼び出し

```cm
<T: Ord> T max_of(T a, T b) {
    if (a < b) {
        return b;
    }
    return a;
}

<T: Eq> bool same(T a, T b) {
    return a == b;
}
```

関数の型パラメータ制約は`<T: Ord>`・`<T: Eq>`・`<T: Ord + Clone>`のように型パラメータリスト側へ書き、比較演算子や等値演算子の使用可否を型検査が制約として扱う。
制約は特殊化ごとの型検査で強制されるだけで、IRには痕跡を残さず、生成される特殊化シンボルは無制約の場合と同じ`max_of__int`形式である。
自由関数の後置where句（`<T> T f(T x) where T: Ord`）は構文エラーになり、where句は次節の構造体宣言・impl宣言専用の構文である。

### where句付きimplとジェネリックimplメソッドの呼び出し

```cm
struct Wrap<T> {
    T value;
}

interface Getter<T> {
    T get();
}

impl<T> Wrap<T> for Getter<T> where T: Ord {
    T get() {
        return self.value;
    }
}

int main() {
    Wrap<int> w;
    w.value = 7;
    println("{w.get()}");
    return 0;
}
```

```llvm
define i32 @Wrap__int__get(ptr %arg0) {
  ; self（Wrap<int>へのポインタ）からvalueフィールドをロードして返す
}
```

impl宣言のwhere句（`where T: Ord`、`+`/`|`によるAND/OR結合可）は型引数への制約検査にのみ使われ、メソッドの単相化結果には影響しない。
`w.get()`の呼び出しはレシーバ型`Wrap<int>`で単相化された`Wrap__int__get`への直接呼び出しになり、selfはポインタで渡される。
特殊化シンボル名の規則（フラット連結と`$`エンコードの使い分け）は[名前マングリング](../../generics/mangling.md)が詳しい。

## 関連資料

- [ジェネリクスの単相化](../../generics/monomorphization.md) — 特殊化の発見・不動点反復・型キーによる同一性管理
- [名前マングリング](../../generics/mangling.md) — `f__int`形式のシンボル名生成規則
- [インスタンス化診断](../../generics/instantiation-diagnostics.md) — 制約違反時のエラー報告
- [メソッド呼び出しのIR対訳](method-calls.md) — 非ジェネリックimplメソッドの呼び出し形
