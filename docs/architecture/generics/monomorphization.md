# ジェネリクスの単相化（モノモーフィゼーション）

Cmのジェネリクスは実行時の型消去を行わず、MIRレベルの単相化パスが使用箇所ごとに具象型で特殊化した構造体と関数を生成する。特殊化の発見は「MIR全体の型走査（構造体）」と「Call終端命令の走査（関数）」の2系統で行い、関数側は新規生成された特殊化の本体だけを再走査する不動点反復で連鎖的な依存（`A<T>`のメソッドが`B<T>`を呼ぶ等）を深さに依らず解決する。型の同一性は`hir::Type`ツリー（`name` + `type_args`の再帰構造）を唯一の真実とし、ネストした型引数（`Box<Pair<int,string>>`等）はフラットな`__`連結名の再パースではなく、`$`区切りの長さプレフィックス付き可逆エンコーディング（typekeyモジュール）で一意に識別する。

## 概要

単相化はMIR loweringのパイプライン中で1回実行される（`src/internal/mir/lowering/lowering.cpp:48`のPass 4）。エントリポイントは`Monomorphization::monomorphize`（`src/internal/mir/lowering/mono/driver.cpp:22`）で、処理は次の順序で進む。

1. 構造体の単相化を関数より先に1回だけ実行する（`driver.cpp:30` → `monomorphize_structs`）。
2. ジェネリック関数を特定する。`generic_params`が非空、または関数名に`<`を含む（ジェネリックimplメソッド`Vector<T>__push`等）ものが対象になる（`driver.cpp:37`）。
3. 関数特殊化を不動点まで反復する（`driver.cpp:63-104`）。1パス目は全関数をスキャンし、以降のパスは直前に新規生成された特殊化のみをスキャンして、その本体から芋づる式に必要になる特殊化を追加生成する。新規特殊化が無くなればループを抜け、`MAX_PASSES = 64`は暴走防止の安全弁である（`driver.cpp:61`）。
4. 最後に構造体メソッドのself引数修正と、ジェネリック原本関数の削除を行う（`driver.cpp:106-107`）。

クラス定義と全メンバの宣言は`src/internal/mir/lowering/monomorphization.hpp:21`にある。

## データ構造とアルゴリズム

### インスタンス化の発見

構造体側は`collect_struct_specializations`（`src/internal/mir/lowering/mono_structs.cpp:51`）が非ジェネリック関数のローカル変数型を走査する。`type_args`を持つ構造体型は型引数ツリーをそのまま（文字列化せずに）収集し、未解決の型パラメータが残るものは`tree_has_generic_param`（`src/internal/mir/lowering/mono/typeinfo.cpp:250`）でスキップする（`mono_structs.cpp:99-112`）。型検査やHIR loweringが既にフラット名（`Node__int`等）へマングル済みの型は、ユーザー定義構造体に同名が存在しないことを確認したうえで`parse_flat_type_args`により型引数へ復元する（`mono_structs.cpp:114-137`）。

関数側は`scan_generic_calls`（`src/internal/mir/lowering/mono/scan.cpp:22`）が各基本ブロックのCall終端命令を調べる。ジェネリック自由関数への直接呼び出しは`infer_type_args`（`scan.cpp:288`）で実引数のローカル型から型引数を推論し、`Container<int>__print`のような呼び出し名は`Container<T>__print`形式のジェネリックimplメソッド名とパターンマッチして型引数を抽出する（`scan.cpp:66-112`）。コンストラクタ/デストラクタ（`HashMap<int,int>__ctor_1`等）も同じ方式で対応付ける（`scan.cpp:114`以降）。

### 特殊化の生成

構造体特殊化は`generate_specialized_struct`（`mono_structs.cpp:143`）が行う。基底定義の`generic_params`と型引数ツリーから置換マップを作り、各フィールドを`substitute_type_tree`（`typeinfo.cpp:223`）で構造を保ったまま置換する。置換後のツリーに残るネストしたジェネリックインスタンス（`Box<Pair<int,string>>`のフィールド`Pair<int,string>`等）は`to_symbol_type`（`mono_structs.cpp:209`）が再帰的に特殊化を生成しつつシンボル名参照へ正規化する。再帰的なネスト生成の無限ループは、生成前にシンボルキーを`generated_struct_specializations`へ登録することで防ぐ（`mono_structs.cpp:165-166`）。生成後は`update_type_references`（`mono_structs.cpp:276`）がMIR内の型参照を特殊化名へ書き換える。

関数特殊化は`generate_generic_specializations`（`src/internal/mir/lowering/mono/specialize.cpp:22`）がMIR関数をコピーして型を置換する。型パラメータ名は自由関数なら`generic_params`から、implメソッドなら関数名`Vector<T>__method`の`<...>`部分から取得し、`TypePtr`置換マップと呼び出し名書き換え用の文字列置換マップの両方を作る（`specialize.cpp:76-135`）。本体の複製時は`clone_terminator_with_subst`（`src/internal/mir/lowering/monomorphization_utils.cpp:120`）が呼び出し名内の型パラメータを「先頭の`T__method`」と「途中の`__T__`」の2パターンで具象型名へ書き換える（`monomorphization_utils.cpp:156` / `:164`）。最後に`rewrite_generic_calls`（`src/internal/mir/lowering/mono/rewrite.cpp:36`）が呼び出し側のFunctionRefを特殊化名へ差し替える。

### 再帰的型キーによる型同一性管理

フラットな「基底名 + `__` + 型引数」連結は非単射であり、`Box<Box<int>>`・2引数の`Box<Box, int>`・ユーザー定義構造体`Box__Box__int`が同一文字列へ縮退する。この縮退を排除する可逆エンコーディングが`typekey`モジュール（`src/internal/mir/lowering/mono/typekey.hpp:27`）である。

```
// typekey.hpp:19-24 エンコード仕様
// - ジェネリック特殊化: base '$' 引数個数 '$' の後に、各引数を「<エンコード長>'$'<エンコード>」で連結
//   例: Pair<int,string> -> "Pair$2$3$int6$string"
//       Box<Pair<int,string>> -> "Box$1$20$Pair$2$3$int6$string"
// - ポインタ: "$P" + 要素のエンコード（例: *int -> "$Pint"）
// - 参照:     "$R" + 要素のエンコード
// - 固定長配列: "$A" サイズ '$' 要素（例: int[4] -> "$A4$int"）
```

`$`はCmの識別子に出現しないため、エンコード名はユーザー型名と原理的に衝突しない。`encode_type_key`（`typekey.cpp:182`）と`decode_type_key`は往復不変（`decode(encode(t))`が構造一致）であり、`tests/unit/typekey_test.cpp`で保証する。

実際のシンボル名はハイブリッド方式で決まる。`struct_symbol_key`（`typeinfo.cpp:152-179`）は通常、関数特殊化名や呼び出し名と同一規則の`base__arg`フラット連結を用い、フラット名がユーザー定義構造体と同名になる場合のみ`$`エンコード名へ退避する。

```cpp
// typeinfo.cpp:166-178
if (simple) {
    std::string flat = base_name;
    for (const auto& k : keys)
        flat += "__" + k;
    // フラット名がユーザー定義構造体と同名になる場合のみエンコード名へ退避する
    if (!hir_struct_defs || hir_struct_defs->find(flat) == hir_struct_defs->end())
        return flat;
}
std::string out = base_name + "$" + std::to_string(keys.size()) + "$";
```

逆方向の復元は`resolve_struct_field_types`（`typeinfo.cpp:270`）に一本化されており、完全一致名・`$`エンコード名・フラット特殊化名の順で基底定義を解決し、置換済みフィールド型を返す。フラット名の復元`parse_flat_type_args`（`typeinfo.cpp:182`）は、基底の型パラメータが1個の場合に全セグメントを1引数として結合する（`Vector__Vector__int` → `[Vector__int]`）ことでネスト特殊化を正しく扱う。特殊化型のサイズ/アライメント計算（`typeinfo.cpp:321` / `:362`）もこの復元ヘルパーを使い、自然アライメントのCレイアウトで再帰計算する。マングル名からのプリミティブ種別復元は`primitive_kind_from_name`（`src/internal/mir/lowering/mono_internal.cpp:15`）に集約されている。

### 動作例

```cm
struct Pair<T, U> { T first; U second; }
struct Box<T> { T value; }

int main() {
    Box<Pair<int, string>> b;
    b.value.first = 42;
    b.value.second = "hi";
    println("{b.value.first} {b.value.second}");
    return 0;
}
```

このプログラムから単相化が生成する主なシンボルは次のとおりである。

| 入力 | 生成される特殊化 | 備考 |
|---|---|---|
| `Pair<int, string>` | 構造体 `Pair__int__string` | フラット名（ユーザー型と非衝突） |
| `Box<Pair<int, string>>` | 構造体 `Box__Pair__int__string` | フィールド型は`Pair__int__string`への参照に正規化 |
| 内部キー | `Box$1$20$Pair$2$3$int6$string` | typekey表現（衝突時の退避名・ユニットテストの検証対象） |

ユーザーが偶然`struct Box__int { ... }`を定義していた場合、`Box<int>`の特殊化キーだけが`Box$1$3$int`へ退避し、両者は別シンボルとして共存する。

## 実装箇所

| ファイル | 役割 |
|---|---|
| `src/internal/mir/lowering/mono/driver.cpp` | 単相化のエントリポイントと不動点反復 |
| `src/internal/mir/lowering/mono/scan.cpp` | Call終端命令の走査と型引数推論（`infer_type_args`） |
| `src/internal/mir/lowering/mono/specialize.cpp` | 特殊化関数の生成（MIRコピー + 型置換）とジェネリック原本の削除 |
| `src/internal/mir/lowering/mono/rewrite.cpp` | 呼び出し側FunctionRefの特殊化名への書き換え |
| `src/internal/mir/lowering/mono/typeinfo.cpp` | シンボルキー生成・フラット名復元・特殊化型のサイズ/アライメント計算 |
| `src/internal/mir/lowering/mono/typekey.{hpp,cpp}` | `$`区切りの可逆型キーエンコーディング |
| `src/internal/mir/lowering/mono_structs.cpp` | 構造体特殊化の収集・生成・型参照更新 |
| `src/internal/mir/lowering/mono_internal.cpp` | 型置換ヘルパー（`substitute_type_in_type`）とプリミティブ種別復元 |
| `src/internal/mir/lowering/monomorphization_utils.cpp` | 終端命令の複製と呼び出し名の型パラメータ書き換え |
| `src/internal/mir/lowering/monomorphization.hpp` | `Monomorphization`クラス定義 |

## 落とし穴とケア

- 防ぐバグのクラス: フラット名の縮退による誤コンパイル。`__`が有効な識別子文字である以上、ネスト型引数をフラット名から再パースすると`Box<Box<int>>`と`Box<Box,int>`とユーザー定義`Box__Box__int`を区別できず、別の型のレイアウトで特殊化が生成される。収集・生成・参照書き換えは`hir::Type`ツリーを直接引き回し、フラット名の再パースは`parse_flat_type_args`と`resolve_struct_field_types`の共通経路以外で行わないこと。
- 防ぐバグのクラス: 特殊化構造体のstride不一致によるメモリ破壊。フィールドサイズを独自の`switch`で概算すると、`T[N]`配列フィールドや小さいプリミティブ（`short`等）のstrideがcodegenの実レイアウトとずれ、memcpyや配列添字が隣接データを破壊する。サイズ/アライメントは`calculate_specialized_type_size`/`calculate_specialized_type_align`の再帰計算（`resolve_struct_field_types`経由）に一本化されており、二重管理を再導入しないこと。
- 維持すべき不変条件: `decode_type_key(encode_type_key(t))`は構造一致であること（`tests/unit/typekey_test.cpp`）。`struct_symbol_key`のフラット名維持は関数特殊化名（`Vector__int__push`等）との整合が前提であり、片側だけ規則を変えるとリンク失敗になる。
- 維持すべき不変条件: 未解決の型パラメータ（`tree_has_generic_param`が真）を含む型から特殊化を生成しないこと。生成すると`T`という名前の偽構造体がMIRに残り、バックエンド間で結果が分裂する。
- 回帰テスト: `tests/common/generics/nested_type_args.cm`（ネスト型引数のフィールド読み書き）、`tests/common/generics/box_array_field.cm`（`T[N]`フィールドのstride）、`tests/common/generics/user_type_name_collision.cm`（ユーザー定義`Box__int`と`Box<int>`の共存）、`tests/common/generics/nested_generics.cm`、`tests/unit/typekey_test.cpp`。

## 関連資料

- [型同一性の構造化（再帰的型キー）](../../archive/v0.17.0/type-system/type-identity-recursive-keys.md) — 本設計の導入経緯と縮退の実例
- [マングリング名の衝突検出](../../archive/v0.17.0/modules/mangling-collision-detection.md) — フラット名縮退の最終防波堤
- [mangling.md](mangling.md) — シンボルキー空間の全体設計
- [instantiation-diagnostics.md](instantiation-diagnostics.md) — 単相化の上流で不正なインスタンス化を弾く診断
