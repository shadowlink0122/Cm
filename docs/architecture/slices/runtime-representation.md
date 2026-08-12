# スライスのランタイム表現と要素型ディスパッチ

Cmの動的配列（スライス、`T[]`）は、native/jitバックエンドではCランタイムのヘッダ構造体`CmSlice`への不透明ポインタとして表現され、push/pop/get等の全操作はランタイム関数呼び出しとして実行される。要素型ごとに確保幅（elem_size）とランタイム関数の幅サフィックスを選ぶ対応表は`src/internal/mir/lowering/slice_dispatch.hpp`の`slice_scalar_info`へ一元化されており、新しいスカラ型の追加は1箇所の変更で済み、確保幅とアクセス幅の不一致によるヒープ破壊を構造的に防ぐ。

## 概要

スライスの実体はヒープ上の4フィールドヘッダ（データポインタ・長さ・容量・要素サイズ）であり、Cm側の値としてはこのヘッダへのポインタ（`CmSlice*`）が受け渡される。native/jitは同一のCランタイム実装を共有する。`src/internal/codegen/llvm/native/runtime/core.c:25`が`runtime/slice.c`を単一翻訳単位に取り込み、`CMakeLists.txt:621`で`cm_runtime.o`としてビルドされ、AOTでは実行ファイルへリンク、JITでは`cm`バイナリ自身に埋め込まれたシンボルを`DynamicLibrarySearchGenerator`がホストプロセスから解決する（`src/internal/codegen/llvm/jit/jit_engine.cpp:88-95`）。

MIR loweringはスライス操作を`cm_slice_push_i32`のような幅サフィックス付きランタイム関数への`Call`ターミネータへ変換する。このとき「要素型 → elem_size・幅サフィックス」の対応を各loweringサイトが個別に持つと、確保時と使用時で幅が食い違う余地が生まれるため、対応表は`slice_dispatch.hpp`の`slice_scalar_info`だけが持つ。

## データ構造とアルゴリズム

### ヘッダ構造

`src/internal/codegen/llvm/native/runtime/slice.c:22-27`:

```c
typedef struct {
    void* data;         // データポインタ
    int64_t len;        // 現在の要素数
    int64_t cap;        // 容量
    int64_t elem_size;  // 要素サイズ
} CmSlice;
```

生成は`cm_slice_new(elem_size, initial_cap)`（`runtime/slice.c:30-41`）で、容量の既定は4、ヘッダとデータバッファを別々に`cm_alloc`で確保する。容量拡張は`cm_slice_grow`（`runtime/slice.c:117-127`）で2倍成長（最低4）し、`cm_realloc`でデータバッファのみ再確保する。ヘッダ自体のアドレスは成長で変わらないため、`CmSlice*`を保持する側は再確保の影響を受けない（データバッファ内を指すポインタは無効化される。後述）。

### 操作関数群

各操作はスカラ幅ごとの関数バリアントとして定義され、`elem_size`と関数の型幅が一致していることを前提にデータバッファを直接キャストしてアクセスする。

| 操作 | 関数 | 場所 | 備考 |
|---|---|---|---|
| 生成/解放 | `cm_slice_new` / `cm_slice_free` | runtime/slice.c:30-41 / :44-52 | ヘッダ+バッファの2確保 |
| 長さ/容量 | `cm_slice_len` / `cm_slice_cap` | runtime/slice.c:101-114 | NULLは0 |
| push（スカラ） | `cm_slice_push_i8/i16/i32/i64/f32/f64` | runtime/slice.c:130-217 | 満杯なら`cm_slice_grow` |
| push（ポインタ/文字列） | `cm_slice_push_ptr` | runtime/slice.c:220-232 | `void*`幅で格納 |
| push（内側スライス） | `cm_slice_push_slice` | runtime/slice.c:235-249 | `CmSlice`ヘッダ値をインラインコピー |
| push（構造体/ユニオン） | `cm_slice_push_blob` | runtime/slice.c:253-266 | `elem_size`バイトを`cm_memcpy` |
| pop | `cm_slice_pop_i8`〜`_f64` / `_ptr` | runtime/slice.c:269-364 | 空なら0/NULL |
| get（スカラ） | `cm_slice_get_i8`〜`_f64` / `_ptr` | runtime/slice.c:367-455 | 範囲外は0/NULLセンチネル |
| 要素アドレス | `cm_slice_get_element_ptr` | runtime/slice.c:774-783 | `elem_size`でオフセット計算、書き込み・blob読みの土台 |
| 内側スライス取得 | `cm_slice_get_subslice` / `cm_slice_get_subslice_ref` | runtime/slice.c:802-827 / :792-800 | 前者はヘッダのコピー、後者は格納中ヘッダへの参照 |
| delete | `cm_slice_delete` | runtime/slice.c:458-472 | `memmove`で前詰め |
| clear | `cm_slice_clear` | runtime/slice.c:475-480 | `len = 0`のみ（バッファ保持） |
| first/last | `cm_slice_first_i32/_i64/_ptr`等 | runtime/slice.c:728-767, :830-848 | |
| reverse/sort | `cm_slice_reverse` / `cm_slice_sort_*` | runtime/slice.c:863-897 / :899-991 | 新しいスライスを返す（非破壊） |

書き込み（`a[i] = v`）はスカラでも専用のset関数を持たず、`cm_slice_get_element_ptr`が返す要素アドレスへのデリファレンス格納として実現される。MIR loweringはスライス基点の`Index`プロジェクションを検出すると、要素ポインタ取得+`Deref`格納へ正規化する（`src/internal/mir/lowering/expr/binary.cpp:316-366`）。

### 要素型ディスパッチの一元化

スカラ要素型の「elem_size・幅サフィックス」対応は`slice_scalar_info`だけが定義する（`src/internal/mir/lowering/slice_dispatch.hpp:23-56`）:

```cpp
struct SliceScalarInfo {
    int64_t elem_size;  // 要素のバイト幅
    const char* width;  // ランタイム関数の幅サフィックス: "i8"/"i16"/"i32"/"i64"/"f32"/"f64"
};

inline std::optional<SliceScalarInfo> slice_scalar_info(hir::TypeKind kind) {
    switch (kind) {
        case hir::TypeKind::Bool:
        case hir::TypeKind::Char:
        case hir::TypeKind::Tiny:
        case hir::TypeKind::UTiny:
            return SliceScalarInfo{1, "i8"};
        case hir::TypeKind::Short:
        case hir::TypeKind::UShort:
            return SliceScalarInfo{2, "i16"};
        // ... Int/UInt=4/"i32", Long系=8/"i64", Float=4/"f32", Double=8/"f64"
        default:
            return std::nullopt;  // 集約型は各サイト個別
    }
}
```

利用側は「スカラなら`slice_scalar_info`、集約型のみ個別分岐」というパターンに統一されている。push関数選択の例（`src/internal/mir/lowering/expr_slice.cpp:88-101`）:

```cpp
if (auto info = slice_scalar_info(elem_kind)) {
    // スカラ型: 幅サフィックスをslice_dispatchから取得（elem_sizeと整合。C4）
    push_func = std::string("cm_slice_push_") + info->width;
} else if (elem_kind == hir::TypeKind::Array) {
    push_func = "cm_slice_push_slice";       // 多次元: 内側ヘッダをインラインコピー
} else if (elem_kind == hir::TypeKind::Union || elem_kind == hir::TypeKind::Struct) {
    push_func = "cm_slice_push_blob";        // 集約: elem_sizeバイトのインラインコピー
} else if (elem_kind == hir::TypeKind::Pointer || elem_kind == hir::TypeKind::String) {
    push_func = "cm_slice_push_ptr";
}
```

集約型を一元化しないのは意図的である（`slice_dispatch.hpp:11-12`）。ポインタ/文字列はターゲット依存幅（`cm::target_pointer_size()`、wasm32では4）、構造体/ユニオンはレイアウト計算（`ctx.layout_size`）、内側スライスは`CmSlice`ヘッダのインライン格納と、格納表現がサイトごとに異なるためである（例: `src/internal/mir/lowering/stmt/let.cpp:230-240`、`src/internal/mir/lowering/stmt/control.cpp:59-67`）。

## 実装箇所

| ファイル | 役割 |
|---|---|
| src/internal/mir/lowering/slice_dispatch.hpp | `slice_scalar_info`（elem_size・幅サフィックスの唯一の情報源）と`slice_scalar_sort_suffix` |
| src/internal/codegen/llvm/native/runtime/slice.c | native/jit共用のスライスランタイム実装（`CmSlice`定義と全操作） |
| src/internal/mir/lowering/expr_slice.cpp:88, :184 | push/popの関数選択 |
| src/internal/mir/lowering/expr/access.cpp:673 | 添字読み（get）の関数選択 |
| src/internal/mir/lowering/expr/binary.cpp:316-366 | 添字書き込みの`cm_slice_get_element_ptr`+Deref格納への正規化 |
| src/internal/mir/lowering/stmt/let.cpp:230, :348, :403, :433, :574 | スライス初期化時のelem_size決定 |
| src/internal/mir/lowering/expr/construct.cpp:132, :157 | 配列リテラル構築時のpush関数・elem_size決定 |
| src/internal/mir/lowering/stmt/control.cpp:59 | 固定長配列のスライス化返却（`cm_array_to_slice`）のelem_size決定 |
| src/internal/mir/lowering/impl.cpp:303 | グローバルスライス初期化子のelem_size決定 |
| src/internal/mir/lowering/expr_println.cpp:603, :1476 | スライス出力時のget関数選択 |
| src/internal/hir/lowering/expr_member.cpp:589-651, :718-766 | メソッド構文の`__builtin_slice_*`・`cm_slice_sort_*`への脱糖 |
| src/internal/codegen/llvm/core/runtime/builtins.cpp:341-352 | ランタイム関数のLLVM宣言登録 |

## 落とし穴とケア

- 防ぐバグのクラス（要素サイズ不一致によるヒープ破壊）: `cm_slice_new`へ渡すelem_sizeと、アクセスに選ぶランタイム関数の幅が食い違うと、確保ストライドを超えた読み書きが起きヒープを破壊する（例: `short[]`をelem_size=2で確保しながら`cm_slice_push_i32`で4バイト書き込む）。かつてこの対応表がexpr_slice.cpp・access.cpp・construct.cpp・let.cpp・expr_println.cppへ個別複製されており、Short/UShortの取りこぼしで実際にこの破壊が起きた（`slice_dispatch.hpp:7-10`のコメントに経緯の記録がある）。
- 維持すべき不変条件: 新しいスカラ型を追加するときは`slice_scalar_info`（および符号区別が必要なら`slice_scalar_sort_suffix`）だけを変更し、loweringサイトへ幅の分岐を書き足さないこと。逆に新しいloweringサイトを書くときは、スカラ判定を必ず`slice_scalar_info`経由にすること。
- sort関数の選択はHIR脱糖段（`expr_member.cpp:718-766`）が`ast::TypeKind`ベースの独自テーブルで行っており、MIR側の`slice_scalar_sort_suffix`（`slice_dispatch.hpp:59-90`）と情報が二重化している。ソート対象型を追加・変更するときは両者とランタイムの`cm_slice_sort_*`定義（`runtime/slice.c:969-987`）の整合を保つこと。
- `cm_slice_push_slice`は内側`CmSlice`ヘッダを値でコピーするが、`data`ポインタは共有される（`runtime/slice.c:245-248`）。外側スライスのelem_sizeは`sizeof(CmSlice)`でなければならず、これは`let.cpp`の多次元初期化（32バイト）と整合している。
- `cm_slice_get_element_ptr`・`cm_slice_get_subslice_ref`が返すポインタは外側スライスのデータバッファ内を指すため、その後のpush等による`cm_realloc`で無効化される（`runtime/slice.c:790-791`のコメント）。取得直後の単一操作にのみ使い、保持しないこと。
- 回帰テスト: `tests/common/dynamic_array/`（`slice_elem_types.cm`が要素型幅ごとの一致検証、`slice_basic.cm`・`slice_comprehensive.cm`・`slice_index_write.cm`・`slice_member_ops.cm`）、多次元は`tests/common/array/`配下。

## 関連資料

- [境界検査ポリシー](bounds-checking.md) — get/element_ptr経由アクセスへの検査挿入
- [チェーンレシーバの解決](chain-receiver.md) — `cm_slice_get_subslice_ref`によるレシーバ場所化
- [境界チェック統一ポリシー設計文書（archive）](../../archive/v0.17.0/arrays-slices/bounds-checking-policy.md)
