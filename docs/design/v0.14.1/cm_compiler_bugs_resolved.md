# Cm コンパイラ v0.14.1 — 修正済みバグ

> CosmOS開発中に発見され、v0.14.1で修正されたバグの記録。

---

## Bug #2: `*ptr as ulong` でデリファレンスエラー

**状態**: ✅ 解決済み（仕様通りの動作）

`*ptr as ulong` は演算子優先順位により `*(ptr as ulong)` と解釈される。
`(*ptr) as ulong` と括弧を付ければ正常動作。

---

## Bug #5: `__asm__` 出力変数の while 条件不具合

**状態**: ✅ 修正済み

SCCP（疎条件定数伝播）最適化がASM出力変数を定数として扱い、
while条件を定数trueに置換していた。

**修正**: `sccp.cpp` でASM文処理時に出力変数を定数テーブルから除外。  
**テスト**: `common/loops/while_sccp_regression`

---

## Bug #6: `stoll: out of range` — 大きな16進リテラル

**状態**: ✅ 修正済み

コンパイラ内部で`stoll`を使用していたため、`0x8000000000000000`以上の値でオーバーフロー。

**修正**: `stoull` への変更と符号なし型の適切な処理。  
**テスト**: `common/types/ulong_large_hex`

---

## Bug #8: const式でのI/Oポート計算

**状態**: ✅ 修正済み

const変数同士の加算式が関数引数で正しく評価されなかった。

**修正**: コンパイラのconst式評価パイプラインの修正。  
**テスト**: `common/const/const_expr_arithmetic`

---

## Bug #9: ローカル配列のスタックオフセット重複

**状態**: ✅ 修正済み

`&arr as void*` で配列アドレスを取得すると、パーサー優先順位により
`&(arr as void*)` と解析され、配列全体がコピーされてバッファオーバーフロー。

**根本原因（3重問題）**:
1. パーサー: `as` が `&` より高優先度 → `&(b as void*)`
2. MIR lowering: 配列全体コピー → ポインタキャスト
3. LLVM codegen: `Pointer<Array>`型のalloca skipで型不一致

**修正**:
- `mir_to_llvm.cpp`: `Pointer<Array>`型のalloca skipルール削除
- `expr_basic.cpp`: `lower_cast`にarray-to-pointer decay（暗黙的Ref）実装

**テスト**: `common/memory/array_ptr_cast`

---

## Bug #10: `impl`メソッドのネスト呼び出しで`self`変更が消失

**状態**: ✅ JITモードで修正済み（UEFIポインタ経由は未修正）

`impl`メソッドが`self.method()`で別メソッドを呼ぶと、
呼び出し先での`self`フィールド変更が反映されなかった。

**修正**:
- `expr_call.cpp`: impl self書き戻しロジックの修正
- `monomorphization_impl.cpp`: ネストメソッド呼び出し時のself伝播

**テスト**: `common/impl/impl_nested_self`, `impl_nested_self_deep`, `impl_ptr_self`

> [!NOTE]
> JITおよびLLVMネイティブでは修正済み。UEFIターゲットのポインタ経由
> （`ptr->method()`）では未修正。Bug #10bとして別途追跡。
