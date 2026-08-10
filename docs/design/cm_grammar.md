---
title: Cm Language Grammar (EBNF)
---

# Cm Language Grammar (EBNF)

**バージョン:** v0.13.1  
**最終更新:** 2026-02-08

## 記法

| 記号 | 意味 |
|------|------|
| `::=` | 定義 |
| `\|` | 選択肢 |
| `*` | 0回以上の繰り返し |
| `+` | 1回以上の繰り返し |
| `?` | 省略可能 |
| `( )` | グルーピング |
| `'x'` | リテラル終端記号 |
| `[ ]` | 文字クラス |

---

## プログラム構造

```bnf
program ::= top_level_decl*

top_level_decl ::= import_statement
                 | function_decl
                 | struct_decl
                 | trait_decl
                 | impl_decl
                 | typedef_decl
                 | macro_decl
                 | enum_decl
                 | operator_decl

import_statement ::= 'import' string_literal ';'
```

---

## 宣言

### 関数宣言

```bnf
function_decl ::= generic_params? type identifier '(' param_list? ')' where_clause? block
                | generic_params? type identifier '(' param_list? ')' where_clause? ';'  # プロトタイプ
```

### 構造体宣言

```bnf
struct_decl ::= 'struct' identifier generic_params? where_clause? '{' struct_member* '}'

struct_member ::= type identifier ';'
               | function_decl
```

### トレイト宣言

```bnf
trait_decl ::= 'trait' identifier generic_params? '{' trait_member* '}'

trait_member ::= type identifier '(' param_list? ')' ';'
              | type identifier '(' param_list? ')' block  # デフォルト実装
```

### impl宣言

```bnf
impl_decl ::= 'impl' generic_params? trait_name 'for' type where_clause? '{' impl_member* '}'
            | 'impl' generic_params? type where_clause? '{' impl_member* '}'  # inherent impl

impl_member ::= method_modifier* function_decl
              | 'type' identifier '=' type ';'  # 関連型

# メソッド修飾子。exportは既定可視性（公開）の明示（exportとprivateの併用はエラー）
method_modifier ::= 'export' | 'private' | 'static'
```

### typedef・マクロ・Enum・演算子

```bnf
typedef_decl ::= 'typedef' identifier generic_params? '=' type ';'

# マクロ宣言（v0.13.0更新）
macro_decl ::= 'macro' type identifier '=' literal ';'                          # 定数マクロ
             | 'macro' type '*(' type_list? ')' identifier '=' lambda_expr ';'  # 関数マクロ

enum_decl ::= 'enum' identifier generic_params? '{' enum_variant (',' enum_variant)* ','? '}'

enum_variant ::= identifier
               | identifier '(' type_list ')'
               | identifier '{' struct_member* '}'

operator_decl ::= 'operator' operator_symbol '(' param_list ')' type block
                | 'operator' operator_symbol '(' param_list ')' type ';'  # プロトタイプ

operator_symbol ::= '+' | '-' | '*' | '/' | '%'
                  | '==' | '!=' | '<' | '>' | '<=' | '>='
                  | '&' | '|' | '^' | '~' | '!'
                  | '<<' | '>>'
```

添字演算子`[]`・呼び出し演算子`()`のオーバーロードは未対応（専用診断で拒否される）。


---

## ジェネリクス

```bnf
generic_params ::= '<' generic_param_list '>'

generic_param_list ::= generic_param (',' generic_param)*

generic_param ::= type_param
                | const_param

type_param ::= identifier (':' type_constraint)?

const_param ::= identifier ':' 'const' type  # 未対応（宣言時に専用診断で拒否される）

type_constraint ::= union_type       # int | double | float
                  | type_name         # Number (typedef or trait)
```

---

## Where句

```bnf
where_clause ::= 'where' where_bound (',' where_bound)*

where_bound ::= identifier ':' interface_bound

interface_bound ::= trait_name ('+' trait_name)*
```

---

## 型

```bnf
type ::= primitive_type
       | type_name                          # ユーザー定義型
       | type_name '<' type_list '>'        # ジェネリクスインスタンス化
       | type '[' const_expr ']'            # 配列
       | type '*'                           # ポインタ
       | union_type                         # int | double
       | 'typeof' '(' expression ')'        # 式の型

primitive_type ::= 'int' | 'uint'
                 | 'tiny' | 'utiny'
                 | 'short' | 'ushort'
                 | 'long' | 'ulong'
                 | 'double' | 'float'
                 | 'bool' | 'char' | 'void'
                 | 'string' | 'size_t'

union_type ::= type ('|' type)+

type_list ::= type (',' type)*

type_name ::= identifier ('::' identifier)*
```

参照型`T&`とタプル型`(T1, T2)`は未対応（参照はポインタ`T*`で代替する。`T&`は専用診断で拒否される）。

```bnf

trait_name ::= identifier ('::' identifier)*
```

---

## 文

```bnf
statement ::= declaration_statement
            | expression_statement
            | if_statement
            | switch_statement
            | while_statement
            | for_statement
            | return_statement
            | break_statement
            | continue_statement
            | defer_statement
            | must_statement
            | block

must_statement ::= 'must' block  # デッドコード削除防止（volatile扱い）

declaration_statement ::= type identifier ('=' expression)? ';'

expression_statement ::= expression ';'

if_statement ::= 'if' '(' expression ')' statement ('else' statement)?

switch_statement ::= sv_case_attribute? 'switch' '(' expression ')' '{' case_clause* '}'

# SVのcase修飾属性（v0.17.0）: switch/match文の直前にのみ前置できる（priority=順序優先のpriority case・unique0=該当なし許容のunique0 case。非SVターゲットでは無視される）
sv_case_attribute ::= '#[' ('sv::priority' | 'sv::unique0') ']'

case_clause ::= 'case' const_expr ':' statement*
              | 'default' ':' statement*

while_statement ::= 'while' '(' expression ')' statement

for_statement ::= 'for' '(' for_init? ';' expression? ';' expression? ')' statement
                | 'for' '(' type identifier ':' expression ')' statement  # range-based

for_init ::= declaration_statement
           | expression

return_statement ::= 'return' expression? ';'

break_statement ::= 'break' ';'

continue_statement ::= 'continue' ';'

defer_statement ::= 'defer' statement

block ::= '{' statement* '}'
```

---

## 式

### 演算子の優先順位（低→高）

```bnf
expression ::= assignment_expr

assignment_expr ::= conditional_expr
                  | unary_expr assign_op assignment_expr

assign_op ::= '=' | '+=' | '-=' | '*=' | '/=' | '%='
            | '&=' | '|=' | '^=' | '<<=' | '>>='

conditional_expr ::= logical_or_expr
                   | logical_or_expr '?' expression ':' conditional_expr

logical_or_expr ::= logical_and_expr
                   | logical_or_expr '||' logical_and_expr

logical_and_expr ::= inclusive_or_expr
                    | logical_and_expr '&&' inclusive_or_expr

inclusive_or_expr ::= exclusive_or_expr
                     | inclusive_or_expr '|' exclusive_or_expr

exclusive_or_expr ::= and_expr
                     | exclusive_or_expr '^' and_expr

and_expr ::= equality_expr
           | and_expr '&' equality_expr

equality_expr ::= relational_expr
                 | equality_expr ('==' | '!=') relational_expr

relational_expr ::= shift_expr
                   | relational_expr ('<' | '>' | '<=' | '>=') shift_expr

shift_expr ::= additive_expr
              | shift_expr ('<<' | '>>') additive_expr

(* シフト量の意味論: 左オペランドの型幅でmodを取る（int << 32 == int << 0、long << 65 == long << 1）。C言語のような未定義動作にはならず、全バックエンド・全最適化レベルで同一結果を保証する *)

additive_expr ::= multiplicative_expr
                 | additive_expr ('+' | '-') multiplicative_expr

multiplicative_expr ::= cast_expr
                       | multiplicative_expr ('*' | '/' | '%') cast_expr

cast_expr ::= unary_expr
            | '(' type ')' cast_expr

unary_expr ::= postfix_expr
             | '++' unary_expr
             | '--' unary_expr
             | unary_operator cast_expr
             | 'sizeof' '(' type ')'
             | 'sizeof' unary_expr
             | 'typeof' '(' expression ')'

unary_operator ::= '&' | '*' | '+' | '-' | '~' | '!'
```

### 後置式・一次式

```bnf
postfix_expr ::= primary_expr
               | postfix_expr '[' expression ']'         # 配列添字
               | postfix_expr '[' expression ':' expression ']'    # スライス/ビットスライス（x[hi:lo]）
               | postfix_expr '[' expression '+:' expression ']'   # インデックスドパートセレクト（上昇方向）
               | postfix_expr '[' expression '-:' expression ']'   # インデックスドパートセレクト（下降方向。v0.17.0）
               | postfix_expr '(' argument_list? ')'     # 関数呼び出し
               | postfix_expr '.' identifier             # メンバアクセス
               | postfix_expr '->' identifier            # ポインタメンバアクセス
               | postfix_expr '++'                       # 後置インクリメント
               | postfix_expr '--'                       # 後置デクリメント
               | postfix_expr '<' type_list '>'          # ジェネリクス特殊化

primary_expr ::= identifier
               | literal
               | '(' expression ')'
               | lambda_expr
               | format_string

lambda_expr ::= '[' capture_list? ']' '(' param_list? ')' type? block

capture_list ::= capture_item (',' capture_item)*

capture_item ::= '&'? identifier

format_string ::= 'f"' format_string_content '"'

format_string_content ::= (format_text | format_expr)*

format_text ::= [^{}]+

format_expr ::= '{' expression '}'
              | '{' expression ':' format_spec '}'
```

---

## パラメータと引数

```bnf
param_list ::= parameter (',' parameter)*

parameter ::= type identifier ('=' const_expr)?

# 可変長引数 `type '...'` はFFIのextern宣言専用（ユーザー定義関数では専用診断で拒否される）

argument_list ::= expression (',' expression)*
```

---

## 定数とリテラル

```bnf
const_expr ::= conditional_expr  # コンパイル時評価可能

literal ::= integer_literal
          | float_literal
          | char_literal
          | string_literal
          | bool_literal
          | null_literal

integer_literal ::= decimal_literal
                  | hex_literal
                  | binary_literal
                  | octal_literal

decimal_literal ::= [0-9] [0-9_]*

hex_literal ::= '0x' [0-9a-fA-F] [0-9a-fA-F_]*

binary_literal ::= '0b' [01] [01_]*

octal_literal ::= '0o' [0-7] [0-7_]*

float_literal ::= [0-9]+ '.' [0-9]+ ([eE] [+-]? [0-9]+)?
                | [0-9]+ [eE] [+-]? [0-9]+

# 数値サフィックス（42u・3.14f等）は未対応。桁区切りアンダースコア（1_000・0xFF_FF等）は全基数・小数部で使用できる

char_literal ::= "'" (escape_sequence | [^'\\\n]) "'"

string_literal ::= '"' (escape_sequence | [^"\\\n])* '"'
                 | raw_string_literal

# raw文字列はバッククォート区切り。エスケープは解釈されずバックスラッシュはリテラル
# （唯一の例外はデリミタのエスケープ \`）。補間は ${expr} のみ有効
raw_string_literal ::= '`' ( '\\`' | [^`] )* '`'

escape_sequence ::= '\\' ['"\\nrtbfav0$}{]
                  | '\\x' hex_digit hex_digit    # 1バイト
                  | '\\u' hex_digit{4}           # Unicodeコードポイント（UTF-8へエンコード）
                  | '\\U' hex_digit{8}           # Unicodeコードポイント（UTF-8へエンコード）

bool_literal ::= 'true' | 'false'

null_literal ::= 'null' | 'nullptr'
```

---

## 字句要素

```bnf
identifier ::= [a-zA-Z_] [a-zA-Z0-9_]*

keyword ::= 'if' | 'else' | 'while' | 'for' | 'switch' | 'case' | 'default'
          | 'break' | 'continue' | 'return' | 'defer'
          | 'struct' | 'enum' | 'trait' | 'impl' | 'typedef' | 'macro'
          | 'import' | 'export' | 'module'
          | 'operator' | 'private'
          | 'const' | 'static'
          | 'sizeof' | 'typeof'
          | 'true' | 'false' | 'null' | 'nullptr'
          | 'void' | 'int' | 'uint' | 'tiny' | 'utiny' | 'short' | 'ushort'
          | 'long' | 'ulong' | 'float' | 'double' | 'bool' | 'char' | 'string'
          | 'auto' | 'with' | 'where' | 'as'

delimiter ::= '(' | ')' | '[' | ']' | '{' | '}'
            | ';' | ',' | ':'

comment ::= '//' [^\n]* '\n'
          | '/*' ([^*] | '*' [^/])* '*/'

whitespace ::= [ \t\n\r]+
```

---

## プリプロセッサ

```bnf
preprocessor ::= '#' preprocessor_directive

preprocessor_directive ::= 'ifdef' identifier
                         | 'ifndef' identifier
                         | 'else'
                         | 'end'
                         | 'endif'
```

> **注意:** `#end` と `#endif` は同義（別名）です。`#define` は未実装です——シンボル定義はCLIオプション `-D<名前>` と組み込みシンボル（`__macos__` 等）のみで、`#define` の使用は専用診断になります。`#include`、`#pragma`、`#if`/`#elif` も現在未実装です。閉じ忘れ・対応ブロックのない `#end`/`#endif`/`#else` はコンパイルエラーになります。
