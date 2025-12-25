# Cm JS/Web 相互運用性と最適化の設計詳細

本文書は、`#[target]`、CSSインターフェース (`Css`)、および文字列補間の実装に関する詳細な仕様を定義します。

## 1. ビルドターゲットシステム (`#[target]`)

ソースコードレベルでターゲットプラットフォーム（Native, JS, Wasm等）に基づいてコードの包含/除外を制御します。

### 1.1. コンパイルパイプライン
ターゲットによるフィルタリングは、**AST構築後、意味解析（HIR生成）の前**に実施します。

1.  **Parser**: すべてのファイルをパースし、ASTを構築します。この段階ではターゲットに関係なく構文エラーのみをチェックします。
2.  **Attributes Check (AST Visitor)**: 新しいパス `TargetFilteringVisitor` を導入します。
    - 各 AST ノード（特に関数定義、構造体定義、インポート文などのトップレベル宣言）の属性を確認します。
    - `#[target(...)]` が存在する場合、現在のコンパイルターゲットと照合します。
    - マッチしない場合、その AST ノードを無効化（削除、または null化）します。
    - これにより、HIR生成時には無効なターゲットのコードが存在しないため、型エラーや未定義シンボルエラーが発生しません。
3.  **HIR Lowering / Type Check**: 有効なノードのみを対象に通常通り実行します。

### 1.2. 属性構文の詳細
`#[target(arg1, arg2, ...)]`
- 引数リストは `OR` 条件として扱われます（どれか1つでも一致すれば有効）。
- 引数は識別子: `active`, `js`, `web`, `wasm`, `intr` (interpreter)。
- 否定: `!native` は「Native以外」を意味します。

```cm
#[target(js)]
public void web_only_func() { ... }

#[target(!js)]
public void native_only_func() { ... }
```

### 1.3. モジュール/ファイルレベルの制御
ファイル全体の制御のために、ファイルの最初の宣言として属性を許可するか、モジュール宣言に対する属性として扱います。現状のパーサーに合わせて、トップレベルの `Attribute` ノードとして解析し、後続の全ての宣言に適用する、あるいはファイル単位のフラグとして処理します。
**決定**: ファイル単位の制御は実装が複雑になる（ファイルスコープという概念がASTに希薄）ため、まずは **宣言単位（関数、構造体、impl、import）** でのサポートを基本とします。

## 2. CSS統合 (`with Css`)

型安全なCSS定義を提供し、JSオブジェクトとしての生成を最適化します。

## 2.x 言語デザインと実装案まとめ

### 2.x.1 言語デザイン（構文と意味論）
- **構文**: `struct` に `with Css` を付与する（`#[css]` は不要）。
- **意味論**:
  - `with Css` 構造体のインスタンスは **JSのプレーンオブジェクト** として生成する。
  - **プロパティ名変換**: `snake_case` -> `kebab-case`。
  - **値の設定**: 構造体定義はフィールドのみを持ち、値はコンストラクタ等で設定する。
  - **値のマッピング**:
    - `string` -> JS string
    - `int/float` -> **非推奨**（単位が必要なため `string` で保持する）
    - `bool` -> `true` の場合は **キーだけ出力**、`false` の場合は **出力しない**
    - `Option<T>` -> `null` の場合は **キーを出力しない**
  - **disable規則**:
    - `bool disable` が `true` の場合、`disable: true` と同等の意味を持つ。
- **アクセス**:
    - 通常構造体: `obj.field`
    - CSS構造体: `obj["kebab-case"]`
  - **出力順**:
    - `css()` で生成する `key: value;` は **キー名でソート** して安定化する。

### 2.x.2 CSSインターフェース（設計案）
`with Css` 構造体が **CSSインターフェース** を自動実装し、文字列化と判別を統一します。

- **インターフェース定義（案）**:
  - `interface Css { string css(); bool isCss(); }`
- **自動実装**:
  - `with Css` 構造体は `Css` を自動実装する。
  - `css()` はフィールドを **ケバブケース + セミコロン連結** に展開し、キー名でソートする。
  - `isCss()` は常に `true` を返す。
- **用途**:
  - `Css` 型として関数引数に渡せる（CSS構造体の共通型）。
  - テンプレート文字列内で `style.css()` を埋め込み可能。

```cm
interface Css {
    string css();
    bool isCss();
}

struct ButtonStyle with Css {
    string background_color;
    int margin_top;
    bool disable;
}

void render(Css style) {
    string s = `button { ${style.css()} }`;
}
```

### 2.x.3 実装案（コンパイラ内部）
1. **AST/HIR/MIRにCSSフラグを保持**
   - AST: `StructDecl` の `with Css` を検出
   - HIR: `HirStruct.is_css` を追加
   - MIR: `MirStruct.is_css` を追加
2. **JSコード生成**
   - `emitStruct` / `emitAggregate` で CSS構造体の場合は `kebab-case` キーを出力
   - フィールド参照は `obj["kebab-case"]` 形式に切り替え
3. **型チェックと意味論の保証**
   - `Option<T>` の `null` をキー省略へ落とす（JSオブジェクト生成時）
   - `bool` は `true` の場合のみキーを出力（`false` は省略）
4. **テスト追加**
   - `tests/test_programs/js_specific/` に `css_struct_basic.cm`
   - JS出力の `.expect` を追加

### 2.x.4 デザイン上の注意点
- `with Css` を付与した構造体にのみ影響を与える（互換性重視）。
- 単位付与（`px` 等）は **コード生成側で行わず**、JS側に委ねる。
- ネストCSS（`&:hover`等）は将来拡張（`#[key("...")]`）として扱う。
- **optional/undefinedの扱い**:
  - **前提**: optional/undefined は言語共通の仕組みとする。
  - **採用**: `T?`（`Option<T>`）による明示的な任意指定を採用する。例: `bool? disable;`
  - **理由**: Cmは堅牢な型システムを優先し、未初期化や曖昧な状態を避ける。CSSの「未指定」は `T?` の `null` で表現し、生成時にキー省略として落とす。
  - **課題**: `T?` による optional は「一度宣言したら残る」ため、フィールドごとの可変性が限定される。CSS用途では **必要な箇所のみ optional を付与** する運用ルールを明記する。
  - **補足**: 追加ルールがない限りは考慮対象外とする。

### 2.1. 構文と意味論

新しいキーワードではなく、既存の `struct` に `with Css` を付与する方式を採用します（言語拡張を最小限にするため）。

```cm
struct MyStyle with Css {
    string color;
    int margin_top;
    string? background_color;  // Optional
    
    // ネストされたCSSは、ネスト構造体にも `with Css` が必要。
    // 例: hover は `with Css` を実装した構造体であることが求められる。
}

// 値の設定はユースケース次第（コンストラクタは既存実装）
// 引数が膨大になりやすいため、リテラルのコピー代入を推奨
// 例: MyStyle s = { color: "red", margin_top: 10, background_color: null };
```

### 2.2. 型と値のマッピング
CSSプロパティの値は柔軟ですが、型安全性とのバランスを取ります。

| Cm型 | CSS/JS値 | 処理 |
|---|---|---|
| `string` | 文字列 | そのまま出力 |
| `int`, `float` | 数値 +単位? | **非推奨**（単位が必要なため `string` で保持する） |
| `bool` | - | `true` の場合のみキーを出力、`false` は省略 |
| `Option<T>` | `T | undefined` | 値がない(`null`)場合、JSオブジェクトのキー自体を含めない（`undefined`）。 |

### 2.3. プロパティ名の変換
`with Css` 構造体に対してのみ、JSコード生成時にフィールド名の自動変換を行います。

- **ルール**: スネークケース (`foo_bar`) -> ケバブケース (`foo-bar`)
- **例外**: `_` で始まるフィールドなどはそのまま？ -> 基本全変換でOK。
- **実装**: `JSCodeGen::emitStruct` またはオブジェクトリテラル生成時に `#[css]` 属性を確認し、キー名を変換して出力。

### 2.4. インライン展開とヘルパー
CSS構造体はインスタンス化されると、JSのオブジェクトリテラルとして表現されます。
`MyStyle s = { ... };` -> `let s = { color: "red", "margin-top": "10px" };`

### 2.5. HTML上での利用例
`Css` は **スタイル文字列** として使う想定です。代表的な使い方は以下です。

```cm
struct ButtonStyle with Css {
    string color;
    string background_color;
    int padding_top;
    int padding_bottom;
    bool? disabled;
}

void render() {
    ButtonStyle s = {
        color: "white",
        background_color: "red",
        padding_top: 8,
        padding_bottom: 8,
        disabled: null
    };

    string css = s.css();
    // 例: `button { ${css} }` のようにHTML/テンプレートへ埋め込む
}
```

想定されるJS側の利用:
```js
const style = { color: "white", "background-color": "red", "padding-top": 8, "padding-bottom": 8 };
const css = "color: white; background-color: red; padding-top: 8; padding-bottom: 8;";
document.querySelector("button").style.cssText = css;
```

HTMLにそのまま埋め込む用途も優先する。
`style` 属性へ `style.css()` を文字列として埋め込めることを保証する。

## 3. 文字列補間と曖昧さ回避

### 3.1. 問題
JS/CSSコードをバッククォート文字列に埋め込む際、`{...}` がブロックとしても補間としても解釈されうる。
`string css = \`body { color: red; }\`;` -> パーサーは `color: red;` を式としてパースしようとして失敗する。

### 3.2. 解決策: raw文字列の `${...}` 補間
バッククォート文字列では `{` と `}` を **常にリテラル** として扱い、補間は `${...}` のみ許可します。
これにより、CSSのブロック `{ ... }` をそのまま書けます。

- **Lexer**: raw文字列で `${...}` を見つけた場合のみ補間として保持し、通常の `{` `}` はエスケープ済みとして扱う。
- **Parser**: 変更不要（Lexerが正しく文字列として返せばOK）。
- **Codegen**: `${...}` は Cm の補間として処理し、`{` 単体はリテラルとして残る。

通常の文字列リテラル (`"..."`) では従来通り `{...}` 補間を使い、`\\{` / `\\}` でリテラルにできます。

### 3.3. 例
```cm
string style = `
    .container {
        display: flex;
    }
    .cta {${button_css}}
`;
```

## 4. 実装フェーズ

### Phase 1: ターゲット制御 (`#[target]`)
1.  **Parser**: トップレベル宣言（`FunctionDecl`, `StructDecl`, `ImplDecl`, `UseDecl`）が `Attribute` を保持できるように確認/修正。
2.  **AST**: `ast::Attribute` に引数リストを持たせる解析ロジック追加。
3.  **Semantic Analysis**: `TargetFilteringVisitor` を実装し、HIR生成前に不要なノードを除外。

### Phase 2: 文字列エスケープ
1.  **Lexer**: `\{` および `\}` のサポート追加。

### Phase 3: CSS構造体
1.  **Codegen**: `emitStruct` および構造体インスタンス生成 (`emitAggregate`) において、`#[css]` 属性が付いている場合、フィールド名をケバブケースに変換するロジックを追加。
