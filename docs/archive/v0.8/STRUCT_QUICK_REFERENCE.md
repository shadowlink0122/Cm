# 構造体実装 クイックリファレンス

## 概要
Cm言語の構造体機能はAST～MIR層で完全に実装されており、LLVM バックエンドでのコード生成のみが必要です。

## 実装状況ダッシュボード

### ✅ 完全実装済み（9項目）
1. **AST層の構造体定義** - StructDecl, Field
2. **メンバアクセス式** - MemberExpr (x.field)
3. **型システム** - TypeKind::Struct
4. **パーサー** - parse_struct()完全実装
5. **HIR構造体ノード** - HirStruct, HirField
6. **HIR降格処理** - lower_struct(), lower_member()
7. **MIR構造体登録** - register_struct()
8. **フィールドプロジェクション** - PlaceProjection::field()
9. **MIRメンバ降格** - lower_member()で完全実装

### 🔧 部分実装（3項目）
1. **型システム** - サイズ計算 (info()メソッド)
2. **型チェッカー** - メンバアクセスの完全な型チェック
3. **LLVM codegen** - 基本的な構造体レイアウト計算

### ❌ 未実装（6項目）
1. **構造体インスタンス化** - Point{x: 1.0, y: 2.0}
2. **デフォルト初期化** - Point p;
3. **impl ブロック** - メソッド・コンストラクタ
4. **メモリレイアウト計算** - LLVM用フィールドオフセット
5. **ジェネリック構造体** - Vec<T>の特殊化
6. **構造体コピー** - 値のコピーセマンティクス

## キーファイル一覧

| カテゴリ | ファイルパス | 主な要素 |
|---------|-----------|--------|
| AST定義 | `src/frontend/ast/decl.hpp` | StructDecl, Field |
| 型システム | `src/frontend/ast/types.hpp` | TypeKind::Struct |
| メンバアクセス | `src/frontend/ast/expr.hpp` | MemberExpr |
| パーサー | `src/frontend/parser/parser.hpp` | parse_struct() |
| HIR定義 | `src/hir/hir_nodes.hpp` | HirStruct, HirMember |
| HIR降格 | `src/hir/hir_lowering.hpp` | lower_struct(), lower_member() |
| MIR情報 | `src/mir/mir_nodes.hpp` | PlaceProjection |
| MIR降格 | `src/mir/mir_lowering.hpp` | register_struct(), lower_member() |
| LLVM生成 | `src/codegen/llvm/*.hpp` | **要実装** |

## データフロー

```
.cm ソースコード
  ↓
Lexer/Parser → AST(StructDecl)
  ↓
HIR Lowering → HIR(HirStruct)
  ↓
MIR Lowering → MIR(StructInfo, PlaceProjection)
  ↓
LLVM Codegen → LLVM IR (**未実装**)
  ↓
Native Binary/WASM
```

## v0.2.0 実装チェックリスト

### Phase 1: 基本構造体（優先度：高）
```cm
struct Point { double x; double y; }
Point p;
p.x = 1.0;
println(p.x);
```

**必要作業**:
- [ ] LLVM構造体型定義生成
- [ ] フィールドオフセット計算
- [ ] メンバアクセス→LLVM GEP生成
- [ ] デフォルトローカル変数初期化
- [ ] テストケース作成

**推定工数**: 2-3日

### Phase 2: 構造体初期化（優先度：中）
```cm
Point p = {1.0, 2.0};
Point p2 = Point{x: 1.0, y: 2.0};
```

**必要作業**:
- [ ] 初期化式パース（{ } または Point{ }）
- [ ] フィールド初期化コード生成
- [ ] 名前付きフィールド初期化対応
- [ ] テストケース作成

**推定工数**: 1-2日

### Phase 3: impl ブロック（優先度：低, v0.3.0以降）
```cm
impl Point {
    self(double x, double y) { this.x = x; }
}
```

**必要作業**:
- [ ] impl ブロックパース
- [ ] メソッド登録・解決
- [ ] コンストラクタ呼び出し
- [ ] デストラクタ処理

**推定工数**: 3-4日

## コード例

### 完全に動作する例（現在）
```cm
struct Point {
    double x;
    double y;
}

int main() {
    Point p;  // ✅ AST→MIRで処理可能
    p.x = 1.5;  // ✅ メンバアクセス可能（MIRまで）
    p.y = 2.5;  // ✅ メンバアクセス可能（MIRまで）
    return 0;
}
```

**制限**: LLVMバックエンドでコード生成されないため実行不可

### まだ未実装の例
```cm
Point p = {1.0, 2.0};  // ❌ 初期化式未実装
Point p2 = Point{};     // ❌ 初期化式未実装
int x = p.x;            // ❌ 型推論・代入完全チェック未実装
```

## 重要な設計ポイント

### 1. メモリレイアウト
Cm言語の構造体はC互換のメモリレイアウトを使用:
- フィールドは定義順に配置
- 各フィールドはそのアライメント要件に従う
- 構造体全体のアライメントは最大フィールドアライメント

例:
```
struct Data {
    char c;      // offset 0, size 1, align 1
    // padding 3 bytes
    int i;       // offset 4, size 4, align 4
    double d;    // offset 8, size 8, align 8
}
// total size: 16, align: 8
```

### 2. 値セマンティクス
- 構造体変数はスタック割り当て
- コピーはメモリコピー（memcpy相当）
- ポインタは別途 `*Point` で表現

### 3. フィールドプロジェクション
MIR層ではPlaceProjectionで表現:
```cpp
MirPlace place(local_id, {PlaceProjection::field(0)});
// = local_id のフィールド0へのアクセス
```

## デバッグTips

### MIRダンプで確認
```bash
./build/bin/cm test.cm --dump-mir 2>&1 | grep -A20 "struct"
```

### 構造体情報確認
MIR降格時のログ:
```
[STRUCT] Registering struct: Point
[STRUCT FIELD] x: double (offset 0)
[STRUCT FIELD] y: double (offset 8)
```

### メンバアクセス確認
```
[MIR] Lowering member access: object.field
[MIR] Field index: 0
[MIR] Place projection: Field(0)
```

## 依存チェーン

実装順序の依存:
```
1. LLVM型定義 ← 必須
   ↓
2. メンバアクセス(GEP) ← 依存: 1
   ↓
3. 初期化コード生成 ← 依存: 1
   ↓
4. テスト・最適化 ← 依存: 2, 3
```

## 参考資料

| ドキュメント | 説明 |
|-----------|------|
| CANONICAL_SPEC.md | 言語仕様（構造体構文） |
| STRUCT_IMPLEMENTATION_STATUS.md | 詳細実装状況（この調査の詳細版） |
| 01_constructor_example.cm | 将来の impl ブロック例 |
| FEATURE_PRIORITY.md | 全機能の優先度 |

## よくある質問

**Q: なぜメンバアクセスがまだ実行できない？**
A: AST～MIRまでは実装されていますが、LLVMバックエンドでのコード生成がまだです。MIRまで到達すると「構造体のメンバアクセスは成功」ですが、実行ファイルには変換されません。

**Q: impl ブロックはいつ実装される？**
A: v0.3.0で計画されています。v0.2.0では基本的なメンバアクセスまでに限定。

**Q: ジェネリック構造体は？**
A: AST層にgeneric_params配列が予約されていますが、特殊化処理は v0.4.0以降の予定。

**Q: 構造体配列や構造体ポインタは？**
A: Array<T>と Pointer<T>として既に型システムに含まれているため、メンバアクセスが動作すれば自動的に対応。
