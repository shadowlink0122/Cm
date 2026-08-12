# メソッド呼び出しのLLVM IR対訳

implブロック内のメソッドは`Type__method`という名前の自由関数へ脱糖され、`obj.method()`はレシーバのアドレスを第0引数（self）に渡す直接callになる。
メソッドチェーンは各段の戻り値を一時ローカルへ実体化して次段のselfポインタとして繋ぎ、`return self`はselfの指す構造体の現在値を戻り値として返す。
本書はimplメソッド・チェーン・ビルダーパターンが-O3でどのようなIRに落ちるかの対訳を示す。

### implメソッドの呼び出し

```cm
struct Point {
    int x;
    int y;
}

impl Point {
    int sum() {
        return self.x + self.y;
    }

    void translate(int dx, int dy) {
        self.x = self.x + dx;
        self.y = self.y + dy;
    }
}

int main() {
    Point p = Point { x: 10, y: 20 };
    p.translate(5, 10);
    println("{p.sum()}");
    return 0;
}
```

```llvm
define i32 @Point__sum(ptr nocapture readonly %arg0) local_unnamed_addr #0 {
entry:
  %field_load = load i32, ptr %arg0, align 4
  %field_ptr1 = getelementptr %Point, ptr %arg0, i64 0, i32 1
  %field_load2 = load i32, ptr %field_ptr1, align 4
  %add = add i32 %field_load2, %field_load
  ret i32 %add
}

define void @Point__translate(ptr nocapture %arg0, i32 %arg1, i32 %arg2) local_unnamed_addr #1 {
entry:
  %field_load = load i32, ptr %arg0, align 4
  %add = add i32 %field_load, %arg1
  store i32 %add, ptr %arg0, align 4
  ...
}

; 最適化前IR（CM_DUMP_IR=1）の呼び出し部
call void @Point__translate(ptr %load3, i32 %load4, i32 %load5)
%2 = call i32 @Point__sum(ptr %load6)
```

HIR loweringが`p.translate(5, 10)`を`Point__translate(&p, 5, 10)`形式のcallへ脱糖し、selfはレシーバのalloca先頭アドレスをそのまま渡すポインタである。
構造体サイズに関係なくselfは常にポインタ渡しなので、`self.x = ...`の書き込みは呼び出し元の実体へ直接反映される。
-O3ではこの例の呼び出し列が全てインライン化され、mainには結果の定数`45`だけが残る（`Point__sum`等の定義は残る）。
脱糖の実装は[メソッドチェーン・式チェーンのlowering](../../lowering/method-chains.md)を参照。

### チェーン呼び出し

```cm
impl Point {
    Point moved(int dx, int dy) {
        return Point { x: self.x + dx, y: self.y + dy };
    }
}

int main() {
    Point p = Point { x: 1, y: 2 };
    const int x = p.moved(5, 5).moved(10, 10).x;
    ...
}
```

```llvm
; 最適化前IR: 各段の戻り値を一時ローカルへ実体化して次段のselfにする
%2 = call %Point @Point__moved(ptr %load3, i32 %load4, i32 %load5)
store %Point %2, ptr %local_9, align 4      ; 1段目の戻り値を一時%local_9へ
%3 = call %Point @Point__moved(ptr %load6, i32 %load7, i32 %load8)   ; %load6は%local_9のアドレス
store %Point %3, ptr %local_13, align 4
%field_ptr9 = getelementptr %Point, ptr %local_13, i32 0, i32 0
%field_load = load i32, ptr %field_ptr9, align 4

; -O3: チェーン全体が定数畳み込みされ、結果16だけが残る
%3 = tail call ptr @cm_format_replace_int(ptr %2, i32 16)
```

チェーン専用のIR表現はなく、前段の戻り値（16バイト以下なら第一級構造体値`%Point`）を一時ローカルへstoreし、そのアドレスを次段のselfポインタとして渡すことで左から右へ繋がる。
末尾のフィールドアクセス`.x`は最後の一時ローカルへの`getelementptr`+`load`になる。
-O3ではインライン化とSROAが一時ローカルを消し去り、この例では最終値`16`まで畳み込まれる。
呼び出し戻り値レシーバの実体化と場所解決の詳細は[メソッドチェーン・式チェーンのlowering](../../lowering/method-chains.md)にある。

### `return self`ビルダー

```cm
struct Builder {
    int count;
    int total;
}

impl Builder {
    self() {
        self.count = 0;
        self.total = 0;
    }

    Builder add(int n) {
        self.count = self.count + 1;
        self.total = self.total + n;
        return self;
    }
}

int main() {
    Builder b = Builder();
    const Builder done = b.add(10).add(20);
    ...
}
```

```llvm
define void @Builder__ctor(ptr nocapture writeonly %arg0) local_unnamed_addr #2 {
  ; フィールドをゼロ初期化
}

define %Builder @Builder__add(ptr nocapture %arg0, i32 %arg1) local_unnamed_addr #3 {
entry:
  %field_load = load i32, ptr %arg0, align 4
  %add = add i32 %field_load, 1
  store i32 %add, ptr %arg0, align 4
  ...
  %0 = insertvalue %Builder poison, i32 %add, 0
  %field_load1014 = insertvalue %Builder %0, i32 %add7, 1
  ret %Builder %field_load1014
}
```

コンストラクタ`self()`は`Builder__ctor(ptr self)`という戻り値voidの初期化関数になり、`Builder()`はallocaしたレシーバへの`Builder__ctor`呼び出しに落ちる。
`return self`はselfポインタのビットを返すのではなく、selfの指す構造体の現在値を構造体値として返す（-O3では更新後フィールドの`insertvalue`で直接構成される）。
そのため`b.add(10).add(20)`は各段が更新済みコピーを次段へ渡すビルダーとして機能し、戻り値が16バイト超の構造体になる場合は[sret変換](function-decl-call.md)と同じ規則で隠し出力ポインタ経由になる。

## 関連資料

- [メソッドチェーン・式チェーンのlowering](../../lowering/method-chains.md) — `Type__method(self, ...)`脱糖とチェーン中間結果実体化の実装詳細
- [関数宣言・呼び出しのIR対訳](function-decl-call.md) — 構造体の値渡し/sret変換の規則
- [集約コピーのlowering](../../memory/aggregate-copy.md) — チェーン中間一時のコピー戦略
- [ジェネリック関数のIR対訳](generic-functions.md) — ジェネリックimplメソッド（`Wrap__int__get`等）の単相化
