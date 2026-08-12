# 条件付きコンパイル

Cmの条件付きコンパイルは、字句解析より前に走る行ベースのプリプロセッサ（`ConditionalPreprocessor`）が `#ifdef` / `#ifndef` / `#else` / `#end` ディレクティブを解釈し、定義シンボル集合に基づいてソース行を取捨選択する仕組みである。シンボルはホスト環境から自動検出される組み込み定義、CLIの `-D` によるユーザー定義、ターゲット種別やテストモードに応じたCLI注入定義の3系統から構成され、パース後に宣言単位で働く `#[target]` 属性フィルタとは役割・タイミングともに別系統として棲み分けられている。

## 概要

条件付きコンパイルはコンパイルパイプラインの最前段近くに位置する。`cm build` / `cm run`（`src/cmd/cm/build.cpp`）では、ソース読み込み後にまず `ImportPreprocessor` がimport文をモジュール本文へ展開し（[import-resolution.md](../modules/import-resolution.md)）、その展開済みテキスト全体に対して `ConditionalPreprocessor::process()` が適用され、その結果がLexer・Parserへ渡る。つまり処理順は「import展開 → 条件付きフィルタ → 字句解析 → 構文解析 → `TargetFilteringVisitor`」であり、条件付きコンパイルはトークン化前の純テキスト変換である。`cm check` / lint（`src/cmd/cm/check.cpp`）もファイルごとに同じ2段プリプロセスを通す。

この順序から2つの帰結がある。第一に、importされたモジュール本文もインライン展開後にフィルタを通るため、標準ライブラリやユーザーモジュール内の `#ifdef` はエントリファイルと同じ定義集合で評価される。第二に、`#ifdef` でimport文を囲んでも、import解決・展開自体はフィルタより先に行われるため抑止できない（展開後の本文が行単位で落とされるだけであり、モジュールが存在しなければ解決エラーになる）。

フィルタを通過しなかった行はトークンにならないため、その内容は構文的に正しい必要すらない。この性質を利用して、アーキテクチャ固有のインラインアセンブリを `#ifdef __x86_64__` / `#ifdef __arm64__` で分岐させるのが最大の実用例である（[inline-asm.md](../lowering/inline-asm.md)）。

## データ構造とアルゴリズム

### 定義シンボル集合

`ConditionalPreprocessor` の状態は `std::unordered_set<std::string> definitions_` の1つだけで、コンストラクタの `init_builtin_definitions()` がホスト環境検出に基づく組み込みシンボルを登録する。検出はCmコンパイラ自身をビルドしたC++コンパイラの `#if defined(...)` で行われる点が設計上の要である。

```cpp
// src/internal/preprocessor/conditional.cpp（抜粋）
#if defined(__x86_64__) || defined(_M_X64)
    definitions_.insert("__x86_64__");
    definitions_.insert("__x86__");
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
    definitions_.insert("__arm64__");
    definitions_.insert("__aarch64__");
#endif
```

組み込みシンボルの全一覧は以下の通り（該当するホスト条件が成立したときのみ定義される）。

| 分類 | シンボル | ホスト側の検出条件 |
|---|---|---|
| アーキテクチャ | `__x86_64__`, `__x86__` | `__x86_64__` / `_M_X64` |
| アーキテクチャ | `__arm64__`, `__aarch64__` | `__aarch64__` / `_M_ARM64` |
| アーキテクチャ | `__i386__`, `__x86__` | `__i386__` / `_M_IX86` |
| アーキテクチャ | `__riscv__` | `__riscv` |
| OS | `__macos__`, `__apple__`, `__unix__` | `__APPLE__ && __MACH__` |
| OS | `__linux__`, `__unix__` | `__linux__` |
| OS | `__windows__` | `_WIN32` / `_WIN64` |
| OS | `__freebsd__`, `__unix__` | `__FreeBSD__` |
| コンパイラ | `__CM__` | 常に定義 |
| ポインタ幅 | `__64BIT__` / `__32BIT__` | `__LP64__` / `_WIN64` の有無で排他 |
| デバッグ | `__DEBUG__` | `NDEBUG` 未定義（＝cmバイナリ自体がデバッグビルド）のとき |

これに加えて、CLI側（`build.cpp`）が状況に応じてシンボルを注入する。`-D NAME` / `-DNAME` / `--define=NAME`（`options.cpp` でパースされ `opts.defines` に蓄積）は `conditional.define(def)` でそのまま登録される。`cm test` / `--test` のテストモードでは `TEST` が自動定義され、`#[test]` 属性と連動するテスト補助コードを `#ifdef TEST` で書ける。ターゲットがベアメタル系（`baremetal-arm` / `bm` / `baremetal-x86` / `bm-x86`）なら `__NO_STD__` と `__BAREMETAL__`、UEFI（`uefi`）ならさらに `__UEFI__` / `__EFI__` が定義される。つまり「ベアメタル/UEFIか否か」だけは `--target` に追従するが、アーキテクチャ・OS・ビット幅のシンボルはホスト固定である（後述の落とし穴を参照）。

### 走査アルゴリズムとネスト

`process()` はソースを `std::getline` で1行ずつ読み、各行を `parse_directive()` で判定する。判定は行頭の空白を許し、`#` に続く英字列が `ifdef` / `ifndef` / `else` / `end` のいずれかならディレクティブ、それ以外（`#[test]` のような属性行や未知の `#foo` を含む）は通常行として扱う。`#ifdef` / `#ifndef` の後続からは `[A-Za-z0-9_]` のシンボル名を読み取る。

ネストは `{active, parent_active, had_true}` の3フィールドを持つ状態スタックで管理する。行を出力するのは「スタックが空（トップレベル）」または「最上段の `active && parent_active` が真」のときで、`#ifdef` は親の出力可否を `parent_active` として積むため、外側が偽のブロック内では内側の条件が真でも出力されない。`had_true` は既に真の分岐を通ったかを記録し、`#else` は `had_true` が偽のときだけ有効化される。

```cpp
// src/internal/preprocessor/conditional.cpp（抜粋）
case Directive::Ifdef: {
    bool parent = should_output();
    bool defined = is_defined(symbol);
    stack.push_back({defined, parent, defined});
    // ディレクティブ行自体は出力しない（空行で置換して行番号を保持）
    result += '\n';
    break;
}
```

行の落とし方は「削除」ではなく「空行置換」である。ディレクティブ行も、条件が偽で落とされた通常行も、出力には改行だけが残るため、フィルタ後のソースは元ソースと行番号が完全に一致し、以降のLexer・Parser・診断の行番号がそのまま元ファイルを指す。末尾については、元ソースが改行で終わっていない場合のみ最後に付いた余分な改行を1つ除去して原文との整合を保つ。

### Cmコード例

```cm
int main() {
    #ifdef __x86_64__
        __asm__("addl $$1, ${+r:x}");   // x86_64ホストでのみトークン化される
    #else
        #ifdef __arm64__
            __asm__("add ${w:+r:x}, ${w:r:x}, #1");
        #end
    #end

    #ifndef __UNDEFINED_SYMBOL__
        println("ifndef: 未定義シンボルなのでこの行は残る");
    #end
    return 0;
}
```

### `#[target]` 属性との棲み分け

条件分岐にはもう1系統、パース直後にASTへ適用される `TargetFilteringVisitor`（`src/internal/syntax/ast/target_filtering_visitor.hpp`）がある。これは `#[target(js, wasm)]` / `#[target(!js)]` 属性を宣言（関数・struct・interface・impl・enum・typedef・グローバル変数・use・import・externブロック、およびimpl内メソッドやexternブロック内宣言）単位で評価し、`--target` から解決した実際のコンパイルターゲットに一致しない宣言をASTから除去する。同Visitorはテストモード以外での `#[test]` 宣言の除去も担う。属性の構文と評価規則の詳細は [attributes.md](attributes.md) を参照。

使い分けの指針は次の通り。`#[target]` は宣言単位のバックエンド分岐（JS版とネイティブ版で実装を分けたいAPIなど）に使い、`--target` に正しく追従する。`#ifdef` は行単位の分岐（関数本体内の文、インラインアセンブリ、import文以外の任意テキスト）に使え、除外側の分岐がパース不能なコードでもよい。一方 `#[target]` はパース後に働くため、全分岐が構文的に正しい必要があり、逆に `#ifdef` のアーキ・OSシンボルはホスト由来なのでクロスコンパイルに追従しない。

## 実装箇所

| ファイル | 役割 |
|---|---|
| `src/internal/preprocessor/conditional.hpp` | `ConditionalPreprocessor` の宣言（`process` / `define` / `undefine` / `is_defined` / `definitions`） |
| `src/internal/preprocessor/conditional.cpp` | 組み込みシンボル初期化・ディレクティブ判定・行走査本体 |
| `src/cmd/cm/options.cpp` | `-D NAME` / `-DNAME` / `--define=NAME` のパース（`opts.defines`） |
| `src/cmd/cm/build.cpp` | パイプラインへの組み込み（import展開後・Lexer前）、`TEST` / `__NO_STD__` / `__BAREMETAL__` / `__UEFI__` / `__EFI__` の注入、`--debug` 時の定義一覧とフィルタ後ソースの表示 |
| `src/cmd/cm/check.cpp` | `cm check` / lint での同一フローの適用 |
| `src/internal/syntax/ast/target_filtering_visitor.hpp` | パース直後の `#[target]` / `#[test]` 宣言フィルタ（別系統） |
| `tests/common/preprocessor/` | 統合テスト（`ifdef_basic.cm`＝基本・ネスト・else、`builtin_constants.cm`＝組み込みシンボル、`test_attr_excluded.cm`＝`#[test]` 除外との連動。各 `.expect` と突き合わせて全バックエンドスイートで実行） |
| `tests/regression/cases/formatter/ifdef/` | フォーマッタがディレクティブ行を保持することの回帰ケース（ネスト・else整列・不均衡分岐など） |

## 落とし穴とケア

- **ホスト由来シンボルとクロスコンパイル**: アーキテクチャ・OS・ビット幅のシンボルはcmバイナリをビルドしたホスト環境の `#if defined` で決まるため、`--target arm64` のようなネイティブクロスコンパイルでもホストのシンボル（x86_64ホストなら `__x86_64__` / `__macos__` 等）が定義されたままになる。インラインアセンブリのアーキ分岐（[inline-asm.md](../lowering/inline-asm.md)）はこのシンボルに依存しているので、クロスコンパイル時にはホスト側の分岐が静かに選ばれ、生成先アーキと不一致のasmが混入するバグクラスがある。ターゲットに追従させたい分岐は `#[target]` 属性を使うか、`-D` で明示的にシンボルを上書き注入してケアする。`--target` に追従する組み込み定義はベアメタル/UEFI系（`__NO_STD__` / `__BAREMETAL__` / `__UEFI__` / `__EFI__`）のみである。
- **`__DEBUG__` はユーザープログラムのビルドモードではない**: `__DEBUG__` はcmコンパイラ自身が `NDEBUG` なしでビルドされたかを反映するので、リリース配布されたcmでは `-O0` でコンパイルしても定義されない。
- **不整合なディレクティブは診断されない**: 対応する `#ifdef` のない `#end` / `#else` はスタック空チェックにより黙って空行化され、閉じ忘れた `#ifdef` もエラーにならずファイル末尾まで条件が効き続ける。ブロック全体が意図せず消えても行番号は保たれたまま後段の「シンボル未定義」等の間接的なエラーとして現れるため、原因調査には `--debug` のフィルタ後ソース表示（および `.tmp/preprocessed.cm` のimport展開ダンプ）が有効である。
- **未知の `#` ディレクティブは素通しされる**: `#ifdef` のタイポ（例: `#ifdfe`）は通常行としてLexerに渡り、プリプロセッサエラーではなく構文エラーとして報告される。属性 `#[...]` はこの素通し規則により意図的にプリプロセッサを透過する。
- **`#ifdef` でimportを抑止できない**: import展開が先行するため、条件が偽でも参照先モジュールの解決・読み込みは行われる。存在しないモジュールを条件付きで参照する構成は成立しない。

## 関連資料

- [attributes.md](attributes.md) — `#[target]` / `#[test]` 属性の構文と `TargetFilteringVisitor` の評価規則
- [../lowering/inline-asm.md](../lowering/inline-asm.md) — `#ifdef` のアーキシンボルに依存するインラインアセンブリ分岐
- [../modules/import-resolution.md](../modules/import-resolution.md) — 条件付きフィルタに先行するimport展開
- [overview.md](overview.md) — パイプライン全体の中での位置づけ
