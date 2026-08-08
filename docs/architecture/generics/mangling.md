# シンボルマングリング規約と衝突検出

Cmはメソッド・自由関数・モジュール修飾名・ジェネリック特殊化を、いずれも`__`を含むフラットなシンボル名へマングルし、単一のシンボル空間（LLVMモジュールの関数名空間）へ発行する。生成規則は`cm::mangle`ヘルパー（`src/internal/base/mangle.hpp`）に集約され、型検査・HIR lowering・MIR loweringが同一の規則を共有する。`__`はユーザー識別子にも使える文字列であるためこのキー空間は本質的に非単射であり、その縮退は2つの防御で無害化する。型検査時の単一シンボルテーブルによる衝突のハードエラー化と、特殊化名がユーザー定義型と同名になる場合の`$`エンコード名への退避である。

## 概要

マングル名のキー空間は次の規則で構成される。

| 由来 | 規則 | 例 |
|---|---|---|
| implメソッド | `Type__method` | `Point__distance` |
| コンストラクタ | `Type__ctor`（オーバーロードは`Type__ctor_N`） | `Vector__ctor_1` |
| デストラクタ | `Type__dtor` | `Vector__dtor` |
| モジュール修飾名 | `A::b` → `A__b` | `math__utils__clamp` |
| ジェネリック特殊化（関数） | `base__arg...`（型引数を順に連結） | `max_of__int` |
| ジェネリック特殊化（implメソッド） | `Base<T>__m` + 型引数 → `Base__arg__m` | `Vector__int__push` |
| ジェネリック特殊化（構造体） | `base__arg...`、ユーザー型と衝突時のみ`$`エンコード | `Pair__int__string` / `Box$1$3$int` |
| 演算子実装 | `Type__op_xx` | `Point__op_eq` |
| ポインタ型引数 | `*int` → `ptr_int` | `PtrContainer__ptr_int__init` |

これらのシンボルは同じリンク名前空間へ発行されるため、規則が1箇所でも複製・逸脱すると「本体が黙って上書きされて消える」「呼び出し先が見つからずリンク失敗する」という誤コンパイルのクラスに直結する。

## データ構造とアルゴリズム

### 生成規則の集約（cm::mangle）

生成規則は`src/internal/base/mangle.hpp`の4関数に集約されている。

```cpp
// mangle.hpp:15-17
inline std::string method_name(const std::string& type_name, const std::string& method) {
    return type_name + "__" + method;
}
// mangle.hpp:34 モジュール修飾名のフラット化（A::b → A__b）
inline std::string flatten_qualified(const std::string& qualified);
```

利用側は、メソッド呼び出し名の生成（`src/internal/hir/lowering/expr_member.cpp:1071`）、implメソッド・演算子の定義名生成（`src/internal/mir/lowering/impl.cpp:75`）、ctor/dtorの定義名生成（`src/internal/hir/lowering/decl.cpp`）、型検査の登録側（`src/internal/types/checking/decl.cpp:311`）である。ジェネリックなレシーバ型はHIR loweringが`Vector<int>` → `Vector__int`へ平坦化してからメソッド名を連結する（`expr_member.cpp:949-1006`）。vtableエントリの実装関数名も同じ規則`type_name + "__" + method.name`で生成される（`src/internal/mir/lowering/lowering.cpp:436`）。

特殊化名のサフィックスは単相化側の`make_specialized_struct_name`/`make_specialized_name`（`src/internal/mir/lowering/mono/typeinfo.cpp:80` / `:90`）が生成し、型引数1個分のキーは`arg_symbol_key`（`typeinfo.cpp:120`）がポインタを`ptr_`接頭辞、参照を`$R`、固定長配列を`$A<N>$`マーカーで表現する。

### 単一シンボルテーブルと衝突のハードエラー化

型検査器は最終マングル名をキーとする単一シンボルテーブル`mangled_symbols_`を持つ（`src/internal/types/checking/checker.hpp:253-263`）。値は由来の表示名・シグネチャ・宣言位置である。

```cpp
// decl.cpp:242-253
void TypeChecker::register_mangled_symbol(const std::string& name, const std::string& origin,
                                          const std::string& sig, Span span) {
    auto [it, inserted] = mangled_symbols_.emplace(name, MangledSymbolInfo{origin, sig, span});
    if (inserted) {
        return;
    }
    if (it->second.origin == origin && it->second.sig == sig) {
        return;
    }
    error(span,
          i18n::msgf(i18n::MsgId::TypeMangledSymbolCollision, name, it->second.origin, origin));
}
```

登録対象は、本体を持つ非ジェネリック自由関数（モジュール修飾名は`flatten_qualified`でフラット化して登録、`decl.cpp:308-313`）、コンストラクタ（`decl.cpp:640`）、デストラクタ（`decl.cpp:649`）、implメソッド（`decl.cpp:756`）である。由来とシグネチャが完全一致する再登録はモジュールフラット化による同一定義の複数出現として許容し、別由来・別シグネチャの同名は`TypeMangledSymbolCollision`（`src/internal/base/messages/message_ids.hpp:318`）の診断付きハードエラーにする。これにより、メソッド`Holder.method`と自由関数`Holder__method()`のように同一リンク名へ縮退する定義は、後勝ちの黙った上書きではなくコンパイルエラーになる。

### 特殊化名とユーザー識別子の分離（$退避）

構造体特殊化のシンボルキーは`struct_symbol_key`（`typeinfo.cpp:152-179`）が生成する。通常は関数特殊化名・呼び出し名と整合するフラット連結を使い、フラット名が`hir_struct_defs`に存在するユーザー定義構造体と同名になる場合のみ`$`区切りの可逆エンコード名（`Box$1$3$int`形式）へ退避する。`$`はユーザー識別子に使えないため逆方向の衝突は原理的に起こらず、シンボルテーブルへの特殊化名の登録は不要である（ユーザー定義`Box__int`とジェネリック`Box<int>`は別シンボルとして共存する）。エンコード仕様の詳細は[monomorphization.md](monomorphization.md)を参照。

### 衝突検出の動作例

```cm
struct Holder { int x; }

impl Holder {
    int method() { return self.x; }   // マングル名: Holder__method
}

int Holder__method() { return 0; }    // 自由関数が同一リンク名へ縮退
```

このプログラムは型検査で次の形の診断を出して非ゼロexitする。

```
error: symbol 'Holder__method' is defined more than once: method 'Holder.method' conflicts with function 'Holder__method' (both lower to the same linkage name)
```

モジュール修飾でも同様に、モジュール`holder2`内の関数`method`（`holder2::method` → `holder2__method`）と自由関数`holder2__method`は同一キーへ縮退し、ハードエラーになる。一方、モジュールのフラット化によって同一定義が複数回登録される場合は由来・シグネチャが一致するため許容される。

## 実装箇所

| ファイル | 役割 |
|---|---|
| `src/internal/base/mangle.hpp` | マングル規則の共有ヘルパー（method/ctor/dtor/モジュール修飾） |
| `src/internal/types/checking/checker.hpp` | `mangled_symbols_`テーブルと`MangledSymbolInfo`の定義 |
| `src/internal/types/checking/decl.cpp` | シンボル登録と衝突検出（自由関数・ctor/dtor・メソッド） |
| `src/internal/hir/lowering/expr_member.cpp` | メソッド呼び出し名の生成（ジェネリックレシーバの平坦化を含む） |
| `src/internal/mir/lowering/impl.cpp` | implメソッド・演算子（`Type__op_xx`）の定義名生成 |
| `src/internal/mir/lowering/mono/typeinfo.cpp` | 特殊化名の生成と`$`退避判定 |
| `src/internal/base/messages/message_ids.hpp` | `TypeMangledSymbolCollision`診断ID |

## 落とし穴とケア

- 防ぐバグのクラス: 同名リンクシンボルへの黙った上書き。`std::unordered_map`の同名キー再代入は無警告であり、シンボルテーブルによる事前検出がなければ、メソッドと自由関数、モジュール修飾名と自由関数の縮退で一方の本体が消える（全バックエンド共通の誤コンパイル）。新しいシンボル発行経路（新種の自動生成関数等）を追加する際は`register_mangled_symbol`への登録を検討すること。
- 防ぐバグのクラス: 生成規則の複製ずれによるリンク失敗。呼び出し側（HIR）と定義側（MIR）が別々の文字列連結を持つと、規則変更時に片側だけ変わって未定義シンボルになる。マングル名の文字列連結を直接書かず、必ず`cm::mangle`ヘルパーを経由すること。
- 既知の非対称: 演算子実装名（`Type__op_eq`等）はシンボルテーブルへ登録されない（衝突の発生確率が低く、誤検出リスクの方が高いため）。また`sanitizeIdentifier`縮退後キーの二次登録も行っていないため、この2系統の衝突は検出されない。ここへ検出を広げる場合は、まず警告として導入し誤検出が無いことを確認してから昇格する方針を守ること。
- 維持すべき不変条件: MIR loweringのインターフェイス呼び出し判定は関数名を最初の`__`で分割して前半をインターフェイス名集合と照合する（`src/internal/mir/lowering/expr_call.cpp:323-333`）。マングル規則を変えるとこの判定も同時に壊れるため、動的ディスパッチのテストを併せて確認すること。
- 回帰テスト: `tests/common/errors/mangle_method_free_fn_collision.cm`（メソッド対自由関数の衝突検出）、`tests/common/errors/mangle_module_method_collision.cm`（モジュール修飾名の衝突検出）、`tests/common/generics/user_type_name_collision.cm`（`$`退避による共存）、既存の正当なプログラムが誤検出されないことは`tests/common/impl/`・`tests/common/advanced_modules/`のスイートが担保する。

## 関連資料

- [マングリング名の衝突検出](../../archive/v0.17.0/modules/mangling-collision-detection.md) — 単一シンボルテーブル導入の設計文書
- [型同一性の構造化（再帰的型キー）](../../archive/v0.17.0/type-system/type-identity-recursive-keys.md) — `$`退避方式の設計文書
- [monomorphization.md](monomorphization.md) — 特殊化名を消費する単相化側の設計
- [../interface/dynamic-dispatch.md](../interface/dynamic-dispatch.md) — マングル名に依存するインターフェイス呼び出し判定
