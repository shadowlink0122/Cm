[English](grammar.en.html)

# Cm 文法定義

**ステータス**: 🚧 開発中（機能実装に応じて追記）

## 概要

Cm言語は **手書き再帰下降パーサ** で実装します。

## 表記法

```
構文規則:
  非終端記号 ::= 定義

  |     選択
  [ ]   省略可能（0回または1回）
  { }   繰り返し（0回以上）
  ( )   グループ化
  ' '   終端記号（リテラル）
```

## 文法規則

### プログラム構造

```bnf
program         ::= { top_level_item }

top_level_item  ::= function_def
                  | struct_def
                  | interface_def
                  | impl_def
                  | import_decl
```

### 修飾子

```bnf
(* 可視性 *)
(* export: 他ファイルからimport可能 *)
(* private: 構造体メンバ、interface/impl用 *)
(* 省略時: 暗黙的にprivate（ファイル内スコープ） *)
visibility      ::= 'export' | 'private'

(* ストレージクラス *)
storage_class   ::= 'static' | 'extern'

(* 型修飾子 *)
type_qualifier  ::= 'const' | 'volatile' | 'mutable'

(* 関数修飾子 *)
func_modifier   ::= 'inline' | 'async'
```

#### 可視性の使い分け

| 修飾子 | 用途 | 例 |
|--------|------|-----|
| `export` | 他ファイルから使用可能 | `export struct Point { ... }` |
| `private` | 構造体メンバの非公開 | `private int internal;` |
| (省略) | ファイル内のみ | `int helper() { ... }` |

```cpp
// lib.cm
export struct Point {      // 他ファイルからimport可能
    int x;                  // デフォルトで公開メンバ
    int y;
    private int internal;   // 非公開メンバ
};

export int add(int a, int b) {  // export関数
    return a + b;
}

int helper() {              // ファイル内のみ（暗黙的private）
    return 0;
}

// main.cm
import lib;
int main() {
    Point p = Point(1, 2);  // OK
    lib::add(1, 2);         // OK
    // lib::helper();       // エラー: helperはexportされていない
}
```

### 関数定義

```bnf
function_def    ::= { modifier } type IDENT 
                    [ generic_params ] '(' [ params ] ')' block

modifier        ::= visibility | storage_class | type_qualifier | func_modifier
generic_params  ::= '<' IDENT { ',' IDENT } '>'
params          ::= param { ',' param }
param           ::= [ type_qualifier ] type IDENT
```

### 構造体定義

```bnf
struct_def      ::= { modifier } 'struct' IDENT [ generic_params ] 
                    '{' { field_def } '}'

field_def       ::= [ visibility ] [ type_qualifier ] type IDENT ';'
```

### インターフェース定義

```bnf
interface_def   ::= [ visibility ] 'interface' IDENT [ generic_params ]
                    '{' { method_sig } '}'

method_sig      ::= type IDENT '(' [ params ] ')' ';'

impl_def        ::= 'impl' IDENT 'for' type '{' { function_def } '}'
```

### 型

```bnf
type            ::= primitive_type
                  | IDENT [ '<' type { ',' type } '>' ]
                  | '*' type              (* ポインタ *)
                  | '&' type              (* 参照 *)
                  | '[' type ']'          (* 配列 *)
                  | '[' type ';' INT ']'  (* 固定長配列 *)

(* 符号付き整数: tiny(8), short(16), int(32), long(64) *)
(* 符号なし整数: utiny, ushort, uint, ulong *)
(* 浮動小数点: float(32), double(64) *)
(* その他: char(8), bool(8), void, string *)

primitive_type  ::= integer_type | float_type | other_type
integer_type    ::= 'tiny' | 'short' | 'int' | 'long'
                  | 'utiny' | 'ushort' | 'uint' | 'ulong'
float_type      ::= 'float' | 'double'
other_type      ::= 'char' | 'bool' | 'void' | 'string'
```

#### プリミティブ型サイズ

| 型 | サイズ | 範囲/備考 |
|----|--------|----------|
| `tiny` | 8bit | -128 ~ 127 |
| `short` | 16bit | -32768 ~ 32767 |
| `int` | 32bit | -2^31 ~ 2^31-1 |
| `long` | 64bit | -2^63 ~ 2^63-1 |
| `utiny` | 8bit | 0 ~ 255 |
| `ushort` | 16bit | 0 ~ 65535 |
| `uint` | 32bit | 0 ~ 2^32-1 |
| `ulong` | 64bit | 0 ~ 2^64-1 |
| `float` | 32bit | IEEE 754 単精度 |
| `double` | 64bit | IEEE 754 倍精度 |
| `char` | 8bit | ASCII/UTF-8 code unit |
| `bool` | 8bit | 0 = false, 非0 = true |
| `void` | - | 空型（汎用ポインタで8bit） |
| `string` | - | 文字列（互換用、内部は char* + length）|

#### 派生型

```cpp
// ポインタ
int* ptr;
void* generic_ptr;

// 参照
int& ref;

// 配列
int[] dynamic_arr;     // 動的配列
int[10] fixed_arr;     // 固定長配列
```

### 文

```bnf
block           ::= '{' { statement } '}'

statement       ::= let_stmt
                  | expr_stmt
                  | return_stmt
                  | if_stmt
                  | for_stmt
                  | while_stmt
                  | match_stmt
                  | block

let_stmt        ::= [ 'const' ] type IDENT [ '=' expr ] ';'
                  | 'auto' IDENT '=' expr ';'

return_stmt     ::= 'return' [ expr ] ';'

if_stmt         ::= 'if' '(' expr ')' block [ 'else' ( if_stmt | block ) ]

for_stmt        ::= 'for' '(' [ let_stmt | expr_stmt ] [ expr ] ';' [ expr ] ')' block

while_stmt      ::= 'while' '(' expr ')' block

match_stmt      ::= 'match' '(' expr ')' '{' { match_arm } '}'
match_arm       ::= pattern '=>' ( expr ';' | block )
```

### 式

```bnf
expr            ::= assignment_expr

assignment_expr ::= ternary_expr [ ( '=' | '+=' | '-=' | '*=' | '/=' | '%=' 
                                   | '&=' | '|=' | '^=' | '<<=' | '>>=' ) assignment_expr ]

ternary_expr    ::= logical_or_expr [ '?' expr ':' ternary_expr ]

logical_or_expr ::= logical_and_expr { '||' logical_and_expr }
logical_and_expr::= bitwise_or_expr { '&&' bitwise_or_expr }
bitwise_or_expr ::= bitwise_xor_expr { '|' bitwise_xor_expr }
bitwise_xor_expr::= bitwise_and_expr { '^' bitwise_and_expr }
bitwise_and_expr::= equality_expr { '&' equality_expr }
equality_expr   ::= relational_expr { ( '==' | '!=' ) relational_expr }
relational_expr ::= shift_expr { ( '<' | '>' | '<=' | '>=' ) shift_expr }
shift_expr      ::= additive_expr { ( '<<' | '>>' ) additive_expr }
additive_expr   ::= multiplicative_expr { ( '+' | '-' ) multiplicative_expr }
multiplicative_expr ::= unary_expr { ( '*' | '/' | '%' ) unary_expr }

unary_expr      ::= ( '-' | '!' | '~' | '&' | '*' ) unary_expr
                  | postfix_expr

postfix_expr    ::= primary_expr { postfix_op }
postfix_op      ::= '(' [ args ] ')'      (* 関数呼び出し *)
                  | '[' expr ']'          (* 添字アクセス *)
                  | '.' IDENT             (* メンバアクセス *)
                  | '.' IDENT '(' [ args ] ')' (* メソッド呼び出し *)
                  | '?'                    (* エラー伝播 *)
                  | '++' | '--'

primary_expr    ::= INT_LITERAL
                  | FLOAT_LITERAL
                  | STRING_LITERAL
                  | CHAR_LITERAL
                  | 'true' | 'false'
                  | 'null'
                  | IDENT
                  | '(' expr ')'
                  | 'new' type [ '(' [ args ] ')' ]
                  | 'await' expr
                  | lambda_expr

lambda_expr     ::= '(' [ params ] ')' '=>' ( expr | block )

args            ::= expr { ',' expr }
```

### パターン

```bnf
pattern         ::= IDENT                         (* 変数束縛 *)
                  | IDENT '(' [ pattern ] ')'     (* コンストラクタ *)
                  | literal
                  | '_'                           (* ワイルドカード *)
```

### インポート

```bnf
import_decl     ::= 'import' module_path [ 'as' IDENT ] ';'
module_path     ::= IDENT { '::' IDENT }
```

## 演算子優先順位

| 優先度 | 演算子 | 結合性 |
|--------|--------|--------|
| 1 (低) | `=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` | 右 |
| 2 | `?:` (三項) | 右 |
| 3 | `\|\|` | 左 |
| 4 | `&&` | 左 |
| 5 | `\|` (ビットOR) | 左 |
| 6 | `^` (ビットXOR) | 左 |
| 7 | `&` (ビットAND) | 左 |
| 8 | `==` `!=` | 左 |
| 9 | `<` `>` `<=` `>=` | 左 |
| 10 | `<<` `>>` (シフト) | 左 |
| 11 | `+` `-` | 左 |
| 12 | `*` `/` `%` | 左 |
| 13 | `-` `!` `~` `&` `*` (単項) | 右 |
| 14 (高) | `()` `[]` `.` `?` `++` `--` | 左 |

## 予約語

```
async, await, break, const, continue, delete, else, enum, export, extern,
false, for, if, impl, import, inline, interface, match, mutable, new,
null, private, return, static, struct, this, true, void, volatile, while, with
```

## TODO

- [ ] ジェネリクス制約 (`where T: Interface`)
- [x] 列挙型 (`enum`)
- [ ] マクロ
- [x] 自動実装 (`with` キーワード)