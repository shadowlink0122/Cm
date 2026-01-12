[English](STRUCT_IMPLEMENTATION_STATUS.en.html)

# Cm言語 構造体実装調査報告書

## 実行日
2025-12-10

## 調査対象
v0.2.0での構造体機能実装の現状把握と必要な拡張の特定

---

## 1. AST層での構造体実装状況

### 1.1 構造体定義（struct StructDecl）
**ファイル**: `/Users/shadowlink/Documents/git/Cm/src/frontend/ast/decl.hpp`

#### 定義済みの要素
```cpp
struct StructDecl {
    std::string name;                        // 構造体名
    std::vector<Field> fields;               // フィールド定義
    Visibility visibility;                   // 可視性（Private/Export）
    std::vector<std::string> auto_impls;     // with キーワードで自動実装するinterface
    std::vector<std::string> generic_params; // ジェネリックパラメータ（将来用）
};

struct Field {
    std::string name;
    TypePtr type;
    Visibility visibility;
    TypeQualifiers qualifiers;
    ExprPtr default_value;  // オプション
};
```

#### 既実装機能
✅ 基本的な構造体定義
✅ フィールド定義（型、デフォルト値オプション）
✅ 可視性制御（private/export）
✅ with キーワードでの自動トレイト実装指定
✅ ジェネリック基盤（未実装だが構造用意されている）

#### ヘルパー関数
```cpp
inline DeclPtr make_struct(std::string name, std::vector<Field> fields, Span s = {})
```

### 1.2 型システムでの構造体型
**ファイル**: `/Users/shadowlink/Documents/git/Cm/src/frontend/ast/types.hpp`

#### TypeKind enum
```cpp
enum class TypeKind {
    Struct,      // ユーザー定義型
    Interface,   // インターフェース型
    // ...
};
```

#### Type構造体での構造体サポート
```cpp
struct Type {
    TypeKind kind;
    std::string name;           // 構造体名
    std::vector<TypePtr> type_args;  // ジェネリック型引数
    // ...
};
```

**注意**: コメント `// TODO: 構造体サイズ計算` あり → サイズ計算が未実装

### 1.3 式ノード（メンバアクセス）
**ファイル**: `/Users/shadowlink/Documents/git/Cm/src/frontend/ast/expr.hpp`

#### MemberExprの定義
```cpp
struct MemberExpr {
    ExprPtr object;      // オブジェクト
    std::string member;  // メンバ名
};
```

✅ AST層でメンバアクセス表現可能

#### NewExprの定義
```cpp
struct NewExpr {
    TypePtr type;
    std::vector<ExprPtr> args;  // コンストラクタ引数
};
```

**状態**: AST定義は存在するがparser/HIR層での処理は不明確

---

## 2. パーサー層での構造体実装

**ファイル**: `/Users/shadowlink/Documents/git/Cm/src/frontend/parser/parser.hpp`

### 2.1 構造体解析
```cpp
ast::DeclPtr parse_struct(bool is_export) {
    // 構造体名を取得
    std::string name = expect_ident();
    
    // with キーワード処理
    std::vector<std::string> auto_impls;
    if (consume_if(TokenKind::KwWith)) {
        do {
            auto_impls.push_back(expect_ident());
        } while (consume_if(TokenKind::Comma));
    }
    
    // フィールド処理
    std::vector<ast::Field> fields;
    while (!check(TokenKind::RBrace) && !is_at_end()) {
        ast::Field field;
        field.visibility = consume_if(TokenKind::KwPrivate) 
            ? ast::Visibility::Private 
            : ast::Visibility::Export;
        field.qualifiers.is_const = consume_if(TokenKind::KwConst);
        field.type = parse_type();
        field.name = expect_ident();
        expect(TokenKind::Semicolon);
        fields.push_back(std::move(field));
    }
    
    // ASTノード作成
    auto decl = std::make_unique<ast::StructDecl>(
        std::move(name), 
        std::move(fields)
    );
    decl->visibility = is_export ? ast::Visibility::Export : ast::Visibility::Private;
    decl->auto_impls = std::move(auto_impls);
}
```

✅ フル実装：構造体定義の完全なパース
✅ with キーワードサポート
✅ フィールド可視性制御
✅ const修飾子対応

### 2.2 メンバアクセス解析
パーサーレベルでは一般的な式解析で対応
- ドット演算子（.）の処理
- メンバ名の取得

---

## 3. HIR層での構造体実装

**ファイル**: `/Users/shadowlink/Documents/git/Cm/src/hir/hir_nodes.hpp`

### 3.1 HIR構造体定義
```cpp
struct HirField {
    std::string name;
    TypePtr type;
};

struct HirStruct {
    std::string name;
    std::vector<HirField> fields;
    bool is_export = false;
};
```

✅ AST→HIR変換で構造体情報を保持

### 3.2 メンバアクセスのHIR表現
```cpp
struct HirMember {
    HirExprPtr object;
    std::string member;
};
```

✅ メンバアクセス式を表現可能

### 3.3 HIRへの降格（AST→HIR）
**ファイル**: `/Users/shadowlink/Documents/git/Cm/src/hir/hir_lowering.hpp`

```cpp
HirDeclPtr lower_struct(ast::StructDecl& st) {
    debug::hir::log(debug::hir::Id::StructNode, "struct " + st.name, debug::Level::Debug);
    
    auto hir_st = std::make_unique<HirStruct>();
    hir_st->name = st.name;
    hir_st->is_export = st.visibility == ast::Visibility::Export;
    
    for (const auto& field : st.fields) {
        hir_st->fields.push_back({field.name, field.type});
        debug::hir::log(
            debug::hir::Id::StructField,
            field.name + " : " + (field.type ? type_to_string(*field.type) : "auto"),
            debug::Level::Trace);
    }
    
    return std::make_unique<HirDecl>(std::move(hir_st));
}

// メンバアクセス降格
HirExprPtr lower_member(ast::MemberExpr& mem, TypePtr type) {
    auto hir = std::make_unique<HirMember>();
    hir->object = lower_expr(*mem.object);
    hir->member = mem.member;
    return std::make_unique<HirExpr>(std::move(hir), type);
}
```

✅ フル実装：AST→HIR変換完了
✅ メンバアクセス降格実装完了

---

## 4. MIR層での構造体実装

**ファイル**: `/Users/shadowlink/Documents/git/Cm/src/mir/mir_lowering.hpp`

### 4.1 構造体情報登録
```cpp
struct StructInfo {
    std::string name;
    std::vector<Field> fields;
};

void register_struct(const hir::HirStruct& st) {
    StructInfo info;
    info.name = st.name;
    for (const auto& field : st.fields) {
        info.fields.push_back({field.name, field.type});
    }
    struct_defs[st.name] = std::move(info);
}
```

✅ 構造体定義をMIRコンテキストに登録

### 4.2 フィールドインデックス管理
```cpp
std::optional<FieldId> get_field_index(const std::string& struct_name,
                                       const std::string& field_name) {
    auto it = struct_defs.find(struct_name);
    if (it == struct_defs.end())
        return std::nullopt;
    for (size_t i = 0; i < it->second.fields.size(); ++i) {
        if (it->second.fields[i].name == field_name) {
            return static_cast<FieldId>(i);
        }
    }
    return std::nullopt;
}
```

✅ フィールドID管理システム実装

### 4.3 メンバアクセスの降格
```cpp
LocalId lower_member(FunctionContext& ctx, const hir::HirMember& member, hir::TypePtr type) {
    // オブジェクトを評価
    LocalId obj_local = lower_expr(ctx, *member.object);
    
    // オブジェクトの型から構造体名を取得
    std::string struct_name;
    if (member.object->type && member.object->type->kind == ast::TypeKind::Struct) {
        struct_name = member.object->type->name;
    }
    
    // フィールドインデックスを取得
    auto field_idx = get_field_index(struct_name, member.member);
    if (!field_idx) {
        return ctx.new_temp(type);  // エラー時はダミー返却
    }
    
    // MirPlaceにフィールドプロジェクション追加
    MirPlace place(obj_local, {PlaceProjection::field(*field_idx)});
    
    // 結果を一時変数にコピー
    LocalId result = ctx.new_temp(type);
    auto rvalue = MirRvalue::use(MirOperand::copy(place));
    ctx.push_statement(MirStatement::assign(MirPlace(result), std::move(rvalue)));
    
    return result;
}
```

✅ フル実装：メンバアクセス→MIR変換

### 4.4 Place投影システム
```cpp
enum class ProjectionKind {
    Field,   // 構造体フィールド
    Index,   // 配列/スライスのインデックス
    Deref,   // ポインタ/参照の間接参照
};

struct PlaceProjection {
    ProjectionKind kind;
    union {
        FieldId field_id;
        LocalId index_local;
    };
    
    static PlaceProjection field(FieldId id) { ... }
};
```

✅ フィールドアクセスのSSA表現完備

---

## 5. 型チェッカーでの構造体対応

**ファイル**: `/Users/shadowlink/Documents/git/Cm/src/frontend/types/type_checker.hpp`

### 5.1 構造体型登録
```cpp
void register_declaration(ast::Decl& decl) {
    if (auto* st = decl.as<ast::StructDecl>()) {
        // 構造体を型として登録
        scopes_.global().define(st->name, ast::make_named(st->name));
    }
}
```

✅ グローバルスコープに型として登録

### 5.2 制限事項
- メンバへのアクセス型チェックが完全でない
- フィールド型の検証が不完全
- 構造体インスタンス化の型チェック未実装

---

## 6. 型システムでのサポート

**ファイル**: `/Users/shadowlink/Documents/git/Cm/src/frontend/ast/types.hpp`

### 6.1 実装済み機能
✅ TypeKind::Struct定義
✅ 型情報の保持（kind, name, type_args）
✅ 名前付き型作成ヘルパー
```cpp
inline TypePtr make_named(const std::string& name) {
    auto t = std::make_shared<Type>(TypeKind::Struct);
    t->name = name;
    return t;
}
```

### 6.2 未実装機能
❌ 構造体サイズ計算（info()メソッド）
❌ 構造体メモリレイアウト計算
❌ ジェネリック構造体の特殊化

---

## 7. コードジェネレーション層での対応状況

### 7.1 LLVM バックエンド
**ファイル**: `/Users/shadowlink/Documents/git/Cm/src/codegen/llvm/*.hpp`

**状態**: 基本的な構造体コード生成が必要
- メンバアクセスのLLVM IRへの変換
- 構造体レイアウトの計算
- フィールドアドレス計算

### 7.2 旧バックエンド（参考）
- C++バックエンド：構造体コード生成実装済み
- Rustバックエンド：構造体コード生成実装済み
- TypeScriptバックエンド：構造体コード生成実装済み

---

## 8. スコープ管理での構造体対応

**ファイル**: `/Users/shadowlink/Documents/git/Cm/src/frontend/types/scope.hpp`

```cpp
class Scope {
    bool define(const std::string& name, ast::TypePtr type, bool is_const = false) {
        symbols_[name] = Symbol{name, std::move(type), is_const, false, {}, nullptr};
        return true;
    }
};
```

✅ シンボル定義が型情報を保持
✅ 構造体型の登録が可能

---

## 9. 実装状況サマリー

### 完全実装済み
| コンポーネント | 状態 | 詳細 |
|---|---|---|
| AST - 構造体定義 | ✅ | StructDecl, Field完全実装 |
| AST - メンバアクセス式 | ✅ | MemberExpr実装 |
| AST - 新規式 | ✅ | NewExpr定義済み（処理未実装） |
| パーサー | ✅ | parse_struct, parse_member完全実装 |
| HIR - 構造体ノード | ✅ | HirStruct, HirField実装 |
| HIR - 降格処理 | ✅ | lower_struct, lower_member実装 |
| MIR - 構造体登録 | ✅ | register_struct実装 |
| MIR - フィールドアクセス | ✅ | PlaceProjection::field実装 |
| MIR - メンバ降格 | ✅ | lower_member完全実装 |
| 型チェッカー - 登録 | ✅ | 構造体型登録実装 |

### 部分実装
| コンポーネント | 状態 | 詳細 |
|---|---|---|
| 型システム | 🔧 | サイズ計算がTODO |
| 型チェッカー | 🔧 | メンバ型チェック不完全 |
| LLVM codegen | 🔧 | 構造体レイアウト計算必要 |

### 未実装
| コンポーネント | 状態 | 詳細 |
|---|---|---|
| 構造体インスタンス化 | ❌ | new Point{x: 1.0, y: 2.0} 未実装 |
| コンストラクタ | ❌ | impl ブロック未実装 |
| メソッド呼び出し | ❌ | s.method() 未実装 |
| ジェネリック構造体 | ❌ | Vec<T> 特殊化未実装 |
| 構造体サイズ計算 | ❌ | info()メソッド未実装 |
| メモリレイアウト | ❌ | フィールドオフセット計算未実装 |

---

## 10. v0.2.0 実装計画

### Phase 1: 基本構造体操作
**目標**: 構造体の定義と基本的なメンバアクセス

```cm
struct Point {
    double x;
    double y;
}

void test() {
    Point p;  // 実装需要
    p.x = 1.0;  // メンバアクセス（既部分実装）
    p.y = 2.0;
}
```

**実装項目**:
1. ✅ AST→HIR→MIR 済み
2. 🔧 LLVM構造体レイアウト計算
3. 🔧 LLVM メンバアクセスコード生成
4. ❌ 構造体変数初期化（デフォルト初期化）
5. ❌ 構造体の値のコピー

### Phase 2: 構造体初期化
**目標**: コンストラクタなしの初期化

```cm
Point p = {1.0, 2.0};  // 未実装
Point p2 = Point{x: 1.0, y: 2.0};  // 未実装
```

**実装項目**:
1. ❌ 名前付きフィールド初期化パース
2. ❌ 位置指定初期化パース
3. ❌ init式コード生成

### Phase 3: impl ブロック（v0.3.0以降）
**目標**: メソッドとコンストラクタ

```cm
impl Point {
    self(double x, double y) {  // コンストラクタ
        this.x = x;
        this.y = y;
    }
    
    double distance() { ... }  // メソッド
}
```

---

## 11. 実装に必要な詳細作業

### 11.1 LLVM構造体レイアウト計算
```cpp
// 必要な実装
struct StructLayout {
    std::string name;
    uint32_t size;      // 構造体全体のサイズ
    uint32_t align;     // アライメント要件
    std::vector<uint32_t> field_offsets;  // 各フィールドのオフセット
};

StructLayout calculate_struct_layout(const MirStruct& st);
```

### 11.2 LLVM メンバアクセス生成
```cpp
// place.projections に Field が含まれている時の処理
llvm::Value* compute_field_address(
    llvm::Value* struct_ptr,
    const MirPlace& place,
    const StructLayout& layout
);
```

### 11.3 構造体初期化処理
```cpp
// 構造体値の初期化
// Point p; -> メモリ割り当てと初期化値設定
// Point p = {x: 1.0}; -> 部分初期化
```

### 11.4 構造体変数の管理
```cpp
// MIR でのローカル変数に対する構造体型の扱い
struct MirLocal {
    // ...
    TypePtr type;  // 構造体型の場合、その情報を保持
};
```

---

## 12. テスト計画

### 現在のテスト状況
- FEATURE_PRIORITY.md での参照:
  - 構造体定義: ✅ 状態
  - メンバアクセス（.）: 🔧 状態（stage2_types/104_struct_basic.cm）

### 必要なテストケース

**Phase 1 テストケース**:
```cm
// test_struct_basic.cm
struct Point {
    double x;
    double y;
}

int main() {
    Point p;
    p.x = 1.5;
    p.y = 2.5;
    println(p.x);  // 1.5
    println(p.y);  // 2.5
    return 0;
}
```

```cm
// test_struct_access.cm
struct Rect {
    Point topLeft;
    Point bottomRight;
}

int main() {
    Rect r;
    r.topLeft.x = 0.0;
    r.topLeft.y = 0.0;
    // ネストされたメンバアクセス
}
```

---

## 13. 依存関係図

```
AST層
  ├─ StructDecl ✅
  ├─ Field ✅
  ├─ MemberExpr ✅
  └─ NewExpr ✅ (AST定義のみ)
        ↓
型システム層
  └─ TypeKind::Struct ✅
        ↓
パーサー層
  └─ parse_struct() ✅
        ↓
型チェッカー層
  └─ register_declaration() ✅
        ↓
HIR層
  ├─ HirStruct ✅
  ├─ HirField ✅
  └─ HirMember ✅
        ↓
HIR降格層
  ├─ lower_struct() ✅
  └─ lower_member() ✅
        ↓
MIR層
  ├─ StructInfo 🔧
  ├─ PlaceProjection::Field ✅
  └─ lower_member() ✅
        ↓
MIR最適化層
  └─ (特に処理なし)
        ↓
コード生成層
  ├─ C++ codegen ✅ (廃止予定)
  ├─ Rust codegen ✅ (廃止予定)
  ├─ TS codegen ✅ (廃止予定)
  └─ LLVM codegen ❌ (実装必要)
        ↓
実行
```

---

## 14. 結論

### 既実装の強み
✅ AST～MIR層での構造体表現が完全に実装されている
✅ 構造体定義パースが完全実装
✅ メンバアクセスの全層対応（AST→MIR）
✅ MIR SSA形式でのフィールドプロジェクション完備
✅ スコープ管理システム実装済み

### v0.2.0で必要な実装
1. **優先度高**: LLVM バックエンドの構造体サポート
   - 構造体レイアウト計算
   - メンバアクセスのLLVM IR生成
   - 必要な時間: 2-3日

2. **優先度中**: 構造体初期化
   - デフォルト初期化
   - 名前付きフィールド初期化パース
   - 必要な時間: 1-2日

3. **優先度低**: ドキュメント整備
   - 構造体使用例
   - メモリレイアウト説明
   - 必要な時間: 0.5日

### クリティカルパス
LLVMバックエンドの完成が最も重要
→ これ以降のコンパイラ機能が全て依存

---

## 付録A: ファイル一覧

### AST関連
- `/Users/shadowlink/Documents/git/Cm/src/frontend/ast/decl.hpp` - StructDecl定義
- `/Users/shadowlink/Documents/git/Cm/src/frontend/ast/types.hpp` - TypeKind::Struct
- `/Users/shadowlink/Documents/git/Cm/src/frontend/ast/expr.hpp` - MemberExpr

### パーサー関連
- `/Users/shadowlink/Documents/git/Cm/src/frontend/parser/parser.hpp` - parse_struct

### HIR関連
- `/Users/shadowlink/Documents/git/Cm/src/hir/hir_nodes.hpp` - HirStruct
- `/Users/shadowlink/Documents/git/Cm/src/hir/hir_lowering.hpp` - lower_struct

### MIR関連
- `/Users/shadowlink/Documents/git/Cm/src/mir/mir_nodes.hpp` - PlaceProjection
- `/Users/shadowlink/Documents/git/Cm/src/mir/mir_lowering.hpp` - register_struct, lower_member

### 型チェック関連
- `/Users/shadowlink/Documents/git/Cm/src/frontend/types/type_checker.hpp` - 型登録
- `/Users/shadowlink/Documents/git/Cm/src/frontend/types/scope.hpp` - スコープ管理

### ドキュメント
- `/Users/shadowlink/Documents/git/Cm/docs/design/CANONICAL_SPEC.md` - 言語仕様
- `/Users/shadowlink/Documents/git/Cm/docs/FEATURE_PRIORITY.md` - 優先度リスト
- `/Users/shadowlink/Documents/git/Cm/examples/impl/01_constructor_example.cm` - 使用例
