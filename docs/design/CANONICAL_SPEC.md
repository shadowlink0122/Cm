# Cm言語 正式仕様（矛盾解決版）

このドキュメントはCm言語の**唯一の正式仕様**です。他のドキュメントと矛盾がある場合、この仕様が優先されます。
本仕様は言語を単一に定義します。構文・機能がどのバックエンド（JIT/Native/WASM/JS/SV/UEFI/baremetal）で使えるかは [バックエンド対応マトリクス](backend_support_matrix.html) を参照してください。

## 1. 関数定義構文

**正式構文：C++スタイル（fnキーワードなし）**

```cm
// 正しい
int add(int a, int b) {
    return a + b;
}

void print(string message) {
    println(message);
}

// 間違い（Rust風は使用しない）
fn add(a: int, b: int) -> int {  // ❌
    return a + b;
}
```

## 2. ジェネリック関数構文

**正式構文：先頭に型パラメータ宣言**

```cm
// 正しい
<T: Ord> T max(T a, T b) {
    return a > b ? a : b;
}

<T, U> U convert(T from, U default_val) {
    // 実装
}

// 間違い
fn max<T: Ord>(a: T, b: T) -> T { }  // ❌ Rust風
T max(T a, T b) { }                   // ❌ 暗黙的推論
```

## 2.1 インターフェース境界

**すべての制約はインターフェース境界として解釈される**

インターフェース境界は「型Tがインターフェースを実装している」ことを要求します。具体的な型を直接指定することはできません。

```cm
// 単一インターフェース境界
// TはEqインターフェースを実装している型
<T: Eq> bool equals(T a, T b) {
    return a == b;
}

// AND境界（複合実装要求）
// TはEqとOrdの両方を実装している型
<T: Eq + Ord> T max_if_equal(T a, T b) {
    if (a == b) { return a; }
    return a > b ? a : b;
}

// OR境界（いずれかの実装要求）
// TはEqまたはHashのいずれかを実装している型
<T: Eq | Hash> void process(T value) {
    // EqかHashのどちらかが使える
}
```

### where句による境界指定

where句は、関数シグネチャの後でインターフェース境界を指定します。

```cm
// where句での境界指定
<T, U> T combine(T a, U b) where T: Eq, U: Clone {
    // TはEqを実装、UはCloneを実装
}

// 複合境界をwhere句で指定
<T> T process(T value) where T: Eq + Ord + Clone {
    // TはEq, Ord, Cloneのすべてを実装
}
```

### 境界の種類

| 構文 | 意味 |
|------|------|
| `T: I` | TはインターフェースIを実装している |
| `T: I + J` | TはIとJの両方を実装している（AND） |
| `T: I \| J` | TはIまたはJを実装している（OR） |

**注意**: 境界はインターフェースを指定します。具体的な型（int, stringなど）を直接境界として使用することはできません。

## 3. オーバーロード（2026-07-06 実装準拠改訂）

### 3.0 自由関数のオーバーロード: 未対応

**自由関数のオーバーロードは実装されていない。** 同名で異なるシグネチャの自由関数を定義するとコンパイルエラーになる（v0.15.1で診断を追加。それ以前は診断なしで不正なコード生成に至っていた）:

```cm
int process(int x) { return x; }
double process(double x) { return x; }
// エラー: 関数 'process' は既に異なるシグネチャで定義されています
```

旧仕様にあった自由関数への `overload` キーワードはパーサが受理しない。将来オーバーロードを導入する場合の構文として予約する。

### 3.1 コンストラクタのオーバーロード（実装済み）

コンストラクタのみ `overload` キーワードによる多重定義が可能:

```cm
struct Resource { int value; }

impl Resource {
    // デフォルトコンストラクタ（overload不要）
    self() {
        self.value = 0;
    }

    // 追加コンストラクタ（overload必須）
    overload self(int v) {
        self.value = v;
    }

    // デストラクタ（オーバーロード不可）
    ~self() {
        // 解放処理
    }
}
```

メモリ管理は `malloc`/`free`（`use libc`）とスライスを使用する。旧仕様の `new T[n]` / `delete[]` 構文は存在しない。

### 3.2 演算子オーバーロード（実装済み・interface経由）

演算子はinterface内で `operator 戻り値型 演算子(引数)` として宣言し、`with` または `impl ... for` で実装する:

```cm
interface Eq<T> {
    operator bool ==(T other);
}

struct Point with Eq {
    int x;
    int y;
}
```

旧仕様の自由関数形式 `Complex operator+(const Complex& a, ...)` は存在しない（`const T&` 参照型も未実装。ポインタ `T*` を使用する）。

**計画**: 自動実装の指定として `#[derive(...)]` 属性を追加予定（`with` はそのまま有効。第6節の計画注記を参照）。

## 4. impl ブロック構文

**2つの形式：**

```cm
// 形式1：コンストラクタ・デストラクタ専用
impl<T> Vec<T> {
    self() { }                    // コンストラクタ
    overload self(size_t) { }     // オーバーロード
    ~self() { }                   // デストラクタ
}

// 形式2：メソッド実装（for インターフェース）
impl<T> Vec<T> for Container<T> {
    void push(T item) { }         // publicメソッド（デフォルト）
    T pop() { }

    // privateメソッド（impl内からのみ呼び出し可能）
    private void grow() {
        // 内部ヘルパー関数
    }
}
```

メソッドのオーバーロード（`overload`修飾子つきメソッド）は未対応であり、専用診断で拒否される（`overload`が使えるのは第3.1節のコンストラクタのみ）。

### 4.1 privateメソッド

`private`修飾子をメソッドの前に付けると、そのメソッドは同じimplブロック内からのみ呼び出し可能になります。

```cm
interface Calculator {
    int calculate(int x);
}

struct MyCalc {
    int base;
}

impl MyCalc for Calculator {
    // privateメソッド：外部から呼び出し不可
    private int helper(int n) {
        return n * 2;
    }

    // publicメソッド：外部から呼び出し可能
    int calculate(int x) {
        return self.helper(x) + self.base;  // impl内からはprivateを呼べる
    }
}

void main() {
    MyCalc c;
    c.base = 10;
    int result = c.calculate(5);  // OK: publicメソッド
    // c.helper(5);                // エラー: privateメソッドは外部から呼べない
}
```

## 5. 型エイリアス

**正式キーワード：`typedef`（C++互換）**

```cm
// 正しい
typedef Int32 = int;
typedef StringList = Vec<string>;
typedef Result<T> = Ok(T) | Err(string);

// リテラル型・ユニオン型
typedef HttpMethod = "GET" | "POST" | "PUT" | "DELETE";
typedef Primitive = int | double | string | bool;

// 間違い（typeは使用しない）
type Int32 = int;  // ❌
```

## 6. 構造体定義

```cm
// 構造体
struct Point {
    double x;
    double y;
}

// withキーワードで自動トレイト実装
struct Point3D with Debug, Clone {
    double x;
    double y;
    double z;
}

// #[derive(...)] 属性でも同じ自動実装を指定できる（withと等価な別記法）
#[derive(Debug, Clone)]
struct Point3D2 {
    double x;
    double y;
    double z;
}
```

**v0.16.0で実装済み**: `#[derive(...)]` 属性は `with` と完全に等価な自動実装記法である（`with` は破壊的変更なしでそのまま有効）。詳細は [設計10](../archive/v0.16.0/10_derive_attribute.html) を参照。なお `with` を構造体メンバ埋め込みへ転用する案は見送りとした（[経緯](../archive/unimplemented/with_struct_embedding.html)）。

### 6.1 メンバ修飾子

#### private修飾子
`private`修飾子を持つメンバは直接アクセス不可。コンストラクタ・デストラクタ内の`this`ポインタからのみアクセス可能。

```cm
struct Person {
    string name;        // 外部からアクセス可能
    private int age;    // コンストラクタ/デストラクタ内からのみアクセス可能
}

impl Person {
    self(string name, int age) {
        this.name = name;
        this.age = age;   // ✓ コンストラクタ内からアクセス可能
    }
}

// アクセサはフリー関数として定義
int getAge(const Person& p);  // 実装はfriend関数等で別途提供

void main() {
    Person p("Alice", 30);
    p.name = "Bob";     // ✓ OK
    // p.age = 30;      // ❌ エラー: privateメンバへの直接アクセス
}
```

#### default修飾子
`default`修飾子はデフォルトメンバを指定。構造体に1つだけ設定可能。メンバ名を省略した場合にこのメンバにアクセス。

```cm
struct Wrapper {
    default int value;  // デフォルトメンバ
    string tag;
}

void main() {
    Wrapper w;
    w.value = 10;  // 通常のアクセス
    w = 20;        // デフォルトメンバへのアクセス（w.value = 20と同等）
    int x = w;     // デフォルトメンバの取得（x = w.valueと同等）
}
```

**制約：**
- `default`修飾子は構造体に1つのメンバにのみ指定可能
- 複数のdefaultメンバはコンパイルエラー

### 6.2 ネスト型宣言（v0.17.1）

struct/enum本体の中に別のstruct/enum型を宣言できる。
ネスト型は外側の型の名前空間に属する独立した型（C++のネストクラス相当）で、外側のインスタンスとメモリ上の関係は持たない。

```cm
struct Outer {
    struct Inner {
        int mem;
    };
    enum Mode {
        FAST,
        SLOW,
    }
    Inner inner;
    Mode mode;
}

enum Category {
    enum Sub {
        MEM = 10,
        REG,
    },
    A = 5,   // ネスト宣言は値スロットを消費しない（A=5, B=6）
    B,
}
```

**意味論：**
- 正準名はフラット名 `Outer::Inner`。ネスト深度は任意（`Outer::Mid::Inner`）
- 外側の型本体の中では非修飾名（`Inner`）・部分修飾名（`Mid::Inner`）で参照でき、外側からは完全修飾パスで参照する
- ネストenumの値は `Outer::Inner::MEM` の修飾チェーンで参照する（variantは常に最終セグメント）
- ネスト型の可視性は外側の型に追従する
- `impl Outer::Inner { ... }` によるメソッド・静的メソッド定義が可能

**制約：**
- ジェネリックstruct/enumの本体にはネスト型を宣言できない
- ネスト型宣言自身にジェネリックパラメータは付けられない
- extern struct内・匿名ネスト（`struct { ... } field;`）は未対応

## 7. マクロ定義

**正式構文：`#macro`キーワード**

```cm
// 正しい
#macro void LOG(string msg) {
    println("[LOG] " + msg);
}

#macro <T> void SWAP(T& a, T& b) {
    T temp = a;
    a = b;
    b = temp;
}

// 間違い（バックスラッシュ継続は使わない）
#define LOG(msg) \     // ❌
    println(msg)
```

## 8. 関数ディレクティブ

```cm
#test
void test_addition() {
    assert(add(1, 2) == 3);
}

#bench
void bench_sort() {
    // ベンチマークコード
}

// inline修飾子はキーワード形式（LLVMのinlinehint属性としてコード生成へ伝搬する）
inline int square(int x) {
    return x * x;
}

#deprecated("Use new_function instead")
void old_function() { }
```

## 9. モジュールシステム

```cm
// インポート
import std::io;
import std::collections::{Vec, HashMap};
import math::*;

// エクスポート
export struct MyStruct { }
export void my_function() { }
export typedef MyType = int;
```

## 10. 型推論規則

**明示的宣言必須（暗黙的推論なし）**

```cm
// 正しい：型パラメータを明示的に宣言
<T> T identity(T value) {
    return value;
}

// 間違い：暗黙的な型推論
T identity(T value) {  // ❌ Tが未宣言
    return value;
}
```

### 例外：単一大文字の自動認識（検討中）

```cm
// 将来的に許可される可能性
T max(T a, T b) {  // Tは自動的にジェネリックと認識
    return a > b ? a : b;
}
```

## 10.1 シフト演算の境界意味論（v0.17.0確定）

シフト量は左オペランドの型幅でmodを取る（`int`値 `<< 32` は `<< 0`、`long`値 `<< 65` は `<< 1` と等価）。
C言語のような未定義動作にはせず、全バックエンド（native/jit/js/wasm）・全最適化レベルで同一結果を保証する（定数畳み込みとコード生成の両方が同じマスクを適用する）。

## 10.2 混合数値二項演算の昇格（v0.17.0確定）

算術（`+ - * / %`）・比較（`== != < > <= >=`）の二項演算で浮動小数と整数が混在する場合、整数オペランドは浮動小数側の型へ暗黙昇格する（`int × double → double`、`float × double → double`。C言語のusual arithmetic conversionsの浮動小数規則に準拠）。
複合代入（`+= -= *= /= %=`）は宛先型（左辺）へ右辺を揃える（`double += int` はsitofp、`int += double` はfptosi切り詰め）。
昇格Castの挿入は型検査（`infer_binary`）が唯一の判断点であり、MIR loweringは「二項演算のオペランドは同型」を前提に混合到達を診断で停止する。
整数同士の幅混在（`int × long` 等）は従来どおりコード生成の幅合わせによる（結果型はより広い方）。
floatオペランドと浮動小数リテラルの混合（`f / 2.0`）はdoubleへ昇格せず、リテラル側をfloatへ適合させて演算をfloat幅で行う（リテラルの型は文脈で決まる）。

## 10.3 数値変換の暗黙/明示の境界（v0.17.0確定・段階導入）

代入的文脈（let初期化・代入・複合代入・return）の数値変換は、次の分類表で扱いを決める（型検査`classify_numeric_conversion`が唯一の定義点）。

| 分類 | 例 | 扱い |
|---|---|---|
| 拡大（値を保存） | `int→long`・`short→int`・`uint→long`・`int→float/double`・`float→double` | 暗黙可・無診断 |
| 縮小（情報を失いうる） | `long→int`・`int→short`・`double→float`・`double/float→int` | 受理するが警告（`as`の付与を提案）。`check/lint --strict`ではエラー |
| 符号解釈の変化 | `int→uint`・`int→ulong` | 受理するが警告。`--strict`ではエラー |
| 符号なし→符号付き整数 | `uint→int`・`usize→int` | 現段階は暗黙可・無診断（`len()`/`cap()`/`sizeof`等の読み出しイディオムを維持。2^31超の縮小リスクは--strictでの診断化を将来検討） |
| 意味変化 | `int↔char`・`int↔bool`・`数値↔string` | `as`必須（従来どおり型エラー） |

宛先に適合するリテラル（`short s = 5;`・`uint u = 7;`・`float f = 2.5;`・`ulong u = 0xFFFFFFFFFFFFFFFF;`）は縮小・符号変化に該当しても診断しない（明示的な負値リテラルの符号なし宛先は診断する）。
受理された変換のうち浮動小数が絡むもの（整数→浮動小数・浮動小数幅違い・浮動小数→整数）は、MIR loweringの`coerce_numeric_context`がlet/代入/引数/デフォルト引数/return/構造体フィールドの各文脈で変換Cast（sitofp/fptrunc/fptosi相当）を挿入し、「受理したのに未変換」のビット再解釈を構造的に排除する。
整数同士の幅違いは従来どおりコード生成の幅合わせ（2の補数ラップ）による。

## 10.4 文字列APIの添字単位（v0.17.0確定）

文字列APIは「コードポイント系」と「バイト系」の2系統に分離し、系統内で長さと添字の単位を一致させる（混用が非ASCII文字列の走査を壊すため）。

| 系統 | 長さ | 要素アクセス | 部分列・検索 |
|---|---|---|---|
| コードポイント系 | `len()`（コードポイント数） | `codepoint_at(i)`（uintスカラ・範囲外0）・`charAt(i)`/`at(i)`（char・ASCIIのみ値を返し非ASCIIコードポイントと範囲外は`'\0'`）・`chars()`（uint[]実体化） | `substring(a,b)`/`slice(a,b)`・`indexOf(t)`（いずれもコードポイント添字） |
| バイト系 | `byte_len()`（UTF-8バイト数） | `byte_at(i)`（int・生バイト0..255・範囲外0） | （バイト添字の部分列APIは未提供。バイト列が必要な場合は`std::strings::from_bytes`と組で扱う） |

`charAt`/`at`の戻り型`char`は1バイトでありASCII範囲のコードポイントのみ忠実に表現できるため、非ASCIIの値取得には`codepoint_at`を使う。生バイト走査（プロトコル解析等）は`byte_len()`+`byte_at()`を対で使う。全バックエンド（native/jit/wasm/js/ts）で同一の値を返す（jsはUTF-16内部表現だがAPI境界でUTF-8バイト/コードポイントへ変換して揃える）。

## 優先順位

このドキュメントの仕様が最優先。矛盾がある場合の優先順位：

1. **CANONICAL_SPEC.md**（このファイル）
2. overload_unified.md
3. impl_blocks.md
4. その他の設計ドキュメント

## 更新履歴

- 2025-12-XX: 初版作成（矛盾解決版）
- 2026-07-06: 第3節を実装準拠に全面改訂（自由関数オーバーロード未対応の明記、new/delete・const参照・自由関数operator構文の削除、実在構文への差し替え）
- 2026-08-14: 6.2節ネスト型宣言を追加（v0.17.1）
