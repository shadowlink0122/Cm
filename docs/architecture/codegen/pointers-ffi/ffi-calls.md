# FFI呼び出しのLLVM IR対訳

本書はCmのFFI構文（`use libc { ... }` 宣言・extern関数呼び出し・string引数のchar*渡し・malloc/freeによるヒープ構造体・可変長引数呼び出し）が、nativeバックエンドの`-O3`でどのLLVM IRに変換されるかを対訳形式で示す。
IR抜粋は `CM_DUMP_IR=2 ./cm compile -O3` で採取した最適化後の実出力であり、宣言からリンク解決までのパイプライン全体は[FFIとextern宣言のlowering](../../lowering/ffi-extern.md)が詳述する。

### `use libc { ... }` 宣言 → 本体なしのdeclare

```cm
use libc {
    void* malloc(long size);
    void free(void* p);
    long strlen(string s);
    int printf(string fmt, ...);
}
```

```llvm
declare noalias noundef ptr @malloc(i64 noundef) local_unnamed_addr #0
declare void @free(ptr allocptr nocapture noundef) local_unnamed_addr #1
declare i64 @strlen(ptr nocapture) local_unnamed_addr #2
declare noundef i32 @printf(ptr nocapture noundef readonly, ...) local_unnamed_addr #3

attributes #0 = { mustprogress nofree nounwind willreturn allockind("alloc,uninitialized") allocsize(0) memory(inaccessiblemem: readwrite) "alloc-family"="malloc" }
```

`use libc` ブロック内のC風シグネチャは本体なしの `declare` になり、extern関数はマングリング対象外なので `@malloc` のような素の名前でモジュールに載る（実体はリンク段でlibcに解決される）。
`void*`/`T*`/`string` はいずれもopaqueポインタ `ptr`、`long` は `i64` に対応し、`...` は関数型の可変長引数フラグになる。
さらにO3では、LLVMがlibcの既知関数に対して `allockind`/`allocsize` などの属性を推論し、未使用の `malloc` 呼び出しの除去といったアロケーション最適化を可能にする。
宣言のパースとマングリング除外の仕組みは[FFIとextern宣言のlowering](../../lowering/ffi-extern.md)を参照。

### 呼び出しはそのまま `call` になる

```cm
puts("hello from Cm");
```

```llvm
%2 = tail call i32 @puts(ptr nonnull dereferenceable(1) getelementptr inbounds (<{ i32, i32, i32, i32, [14 x i8] }>, ptr @strh.1, i64 0, i32 4, i64 0))
```

extern関数の呼び出しはCmの通常関数と同じ `call` 命令であり、シグネチャ変換・引数マーシャリングの追加コードは挿入されない。
Cm型からLLVM型への引数変換は宣言時に確定しているため、呼び出し側は宣言された型で値を渡すだけである。

### string引数のchar*渡し

```cm
long c_length(string s) {
    return strlen(s);
}
```

```llvm
@strh.1 = private unnamed_addr constant <{ i32, i32, i32, i32, [14 x i8] }> <{ i32 1129141041, i32 13, i32 1395741251, i32 0, [14 x i8] c"hello from Cm\00" }>, align 16

define i64 @c_length(ptr nocapture readonly %arg0) local_unnamed_addr #2 {
entry:
  %0 = tail call i64 @strlen(ptr noundef nonnull dereferenceable(1) %arg0)
  ret i64 %0
}
```

Cmの `string` はSDSヘッダ方式（ポインタの手前にメタデータを置き、ポインタ自体はNUL終端バイト列を指す）なので、変換なしでそのまま `char*` としてC関数へ渡せる。
文字列リテラルはヘッダ4フィールド+バイト列のパック構造体定数になり、引数にはバイト列先頭（`i32 4` フィールド）へのGEPが渡される。
なお `strlen("hello")` のようにリテラル長が既知の場合、O3は呼び出し自体を定数に畳み込む。
ヘッダレイアウトの詳細は[文字列のランタイム表現](../../strings/representation.md)を参照。

### malloc/freeによるヒープ構造体（連結リスト）

```cm
struct Node { int value; Node* next; }

// 先頭にノードを追加する
Node* push(Node* head, int v) {
    void* raw = malloc(__sizeof__(Node) as long);
    Node* n = raw as Node*;
    n->value = v;
    n->next = head;
    return n;
}

// リストを合計しながら全ノードを解放する
int drain(Node* head) {
    int total = 0;
    Node* cur = head;
    while (cur != null) {
        total = total + cur->value;
        Node* next = cur->next;
        free(cur as void*);
        cur = next;
    }
    return total;
}
```

```llvm
%Node = type { i32, ptr }

define noalias ptr @push(ptr %arg0, i32 %arg1) local_unnamed_addr #2 {
entry:
  %0 = tail call dereferenceable_or_null(16) ptr @malloc(i64 16)
  store i32 %arg1, ptr %0, align 4
  %field_ptr4 = getelementptr %Node, ptr %0, i64 0, i32 1
  store ptr %arg0, ptr %field_ptr4, align 8
  ret ptr %0
}

define i32 @drain(ptr %arg0) local_unnamed_addr #3 {
entry:
  %ptr_ne.not25 = icmp eq ptr %arg0, null
  br i1 %ptr_ne.not25, label %bb3, label %bb2

bb2:                                              ; preds = %entry, %bb2
  %local_2.027 = phi i32 [ %add, %bb2 ], [ 0, %entry ]
  %local_4.026 = phi ptr [ %field_load11.fca.1.extract, %bb2 ], [ %arg0, %entry ]
  %field_load = load %Node, ptr %local_4.026, align 8
  %field_load.fca.0.extract = extractvalue %Node %field_load, 0
  %add = add i32 %field_load.fca.0.extract, %local_2.027
  %field_load11.fca.1.extract = extractvalue %Node %field_load, 1
  tail call void @free(ptr nonnull %local_4.026)
  %ptr_ne.not = icmp eq ptr %field_load11.fca.1.extract, null
  br i1 %ptr_ne.not, label %bb3, label %bb2

bb3:                                              ; preds = %bb2, %entry
  %local_2.0.lcssa = phi i32 [ 0, %entry ], [ %add, %bb2 ]
  ret i32 %local_2.0.lcssa
}
```

`__sizeof__(Node)` はコンパイル時定数に畳まれて `malloc(i64 16)` の即値になり、`raw as Node*` のポインタキャストは命令を生成せず、フィールド初期化はGEP+storeの列になる。
走査ループは `phi` による合計値とカーソルの回転にまとまり、`cur->value` と `cur->next` の2回のloadは構造体全体の一括 `load %Node` + `extractvalue` に統合され、`free` は解放対象ポインタをそのまま渡す `call` になる。
`push` の返り値には `noalias` が付き、mallocで得た新規メモリであることが呼び出し側の最適化に伝わる。
Cm側アロケータとの関係は[アロケータ](../../memory/allocator.md)、ポインタ演算とGEPの基礎は[ポインタ構文のIR対訳](pointers.md)を参照。

### 可変長引数関数の呼び出し

```cm
void report(int count, double ratio) {
    printf("count=%d ratio=%f\n", count, ratio);
}
```

```llvm
define void @report(i32 %arg0, double %arg1) local_unnamed_addr #0 {
entry:
  %0 = tail call i32 (ptr, ...) @printf(ptr nonnull dereferenceable(1) getelementptr inbounds (<{ i32, i32, i32, i32, [19 x i8] }>, ptr @strh, i64 0, i32 4, i64 0), i32 %arg0, double %arg1)
  ret void
}
```

宣言の `...` により関数型が `i32 (ptr, ...)` の可変長引数型になり、呼び出しは固定引数の後ろへ実引数をそのまま並べたC可変長引数ABIの `call` になる。
Cm側でのva_list構築や引数昇格の追加処理はなく、可変部分の型はLLVM IR上の引数型（`i32`/`double` など）がそのまま使われる。
可変長引数宣言のパースと型検査範囲は[FFIとextern宣言のlowering](../../lowering/ffi-extern.md)を参照。

## 関連資料

- [FFIとextern宣言のlowering](../../lowering/ffi-extern.md) — 宣言のパースからHIR/MIR lowering・リンク解決までの全体像
- [文字列のランタイム表現](../../strings/representation.md) — stringがchar*互換である理由（SDSヘッダ方式）
- [ポインタ構文のIR対訳](pointers.md) — GEP・null比較・ポインタキャストの基礎対訳
- [アロケータ](../../memory/allocator.md) — Cm側のメモリ管理とmalloc/freeの関係
- [リンクとランタイム](../../codegen-native/linking-and-runtime.md) — externシンボルがリンク段で解決される仕組み
