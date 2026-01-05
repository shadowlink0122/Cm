# デバッグモード設計

## 概要

Cmコンパイラは `--debug` / `-d` オプションにより、コンパイル・実行過程の詳細なログを出力します。

## コマンドラインオプション

```bash
# デバッグモード有効（DEBUGレベル）
cm run example.cm --debug
cm run example.cm -d

# ログレベル指定
cm run example.cm -d=trace    # 最詳細
cm run example.cm -d=debug    # 開発用
cm run example.cm -d=info     # 一般情報

# コンパイル時
cm build example.cm --debug
```

## ログレベル

| レベル | 説明 | 出力内容 |
|--------|------|----------|
| **TRACE** | 最詳細 | 各トークン、各ASTノード、型推論過程 |
| **DEBUG** | 開発用 | フェーズ遷移、主要処理、サマリー |
| **INFO** | 一般 | コンパイル結果、ファイル数 |
| **WARN** | 警告 | 未使用変数、非推奨機能 |
| **ERROR** | エラー | コンパイルエラー、実行時エラー |

## 出力形式

### フェーズ別出力

```
[DEBUG] ════════════════════════════════════════════════════════
[DEBUG] Phase: LEXER
[DEBUG] ════════════════════════════════════════════════════════
[TRACE] Token[0]: KEYWORD 'fn' at 1:1
[TRACE] Token[1]: IDENT 'main' at 1:4
[TRACE] Token[2]: LPAREN '(' at 1:8
...
[DEBUG] Lexer completed: 42 tokens in 0.5ms

[DEBUG] ════════════════════════════════════════════════════════
[DEBUG] Phase: PARSER
[DEBUG] ════════════════════════════════════════════════════════
[TRACE] Parsing FunctionDef 'main'
[TRACE]   Parsing Block { }
[TRACE]     Parsing LetStmt 'x'
...
[DEBUG] Parser completed: AST with 15 nodes in 1.2ms

[DEBUG] ════════════════════════════════════════════════════════
[DEBUG] Phase: TYPE_CHECK
[DEBUG] ════════════════════════════════════════════════════════
[TRACE] Inferring type for 'x': Int
[TRACE] Checking call 'println': (String) -> ()
...
[DEBUG] TypeCheck completed: 0 errors, 1 warning in 0.8ms

[DEBUG] ════════════════════════════════════════════════════════
[DEBUG] Phase: HIR_LOWERING
[DEBUG] ════════════════════════════════════════════════════════
[TRACE] Lowering FunctionDef 'main' -> HirFunction
[TRACE] Desugaring ForStmt -> HirLoop + HirIf
...
[DEBUG] HIR lowering completed: 12 HIR nodes in 0.3ms

[DEBUG] ════════════════════════════════════════════════════════
[DEBUG] Phase: CODEGEN (Rust) / INTERPRET
[DEBUG] ════════════════════════════════════════════════════════
```

### カラー出力

ターミナルでは色分け表示:
- TRACE: グレー
- DEBUG: シアン
- INFO: 緑
- WARN: 黄
- ERROR: 赤

## 実装

### logger.hpp

```cpp
#pragma once
#include <iostream>
#include <string_view>

namespace cm {

enum class LogLevel { Trace, Debug, Info, Warn, Error };

class Logger {
public:
    static void set_level(LogLevel level);
    static LogLevel get_level();
    
    static void trace(std::string_view msg);
    static void debug(std::string_view msg);
    static void info(std::string_view msg);
    static void warn(std::string_view msg);
    static void error(std::string_view msg);
    
    // フェーズ管理
    static void phase_start(std::string_view name);
    static void phase_end(std::string_view name, size_t count, double ms);
    
private:
    static LogLevel current_level_;
    static bool use_color_;
};

// コンパイル時除去可能なマクロ
#ifdef CM_DEBUG_BUILD
  #define LOG_TRACE(msg) ::cm::Logger::trace(msg)
  #define LOG_DEBUG(msg) ::cm::Logger::debug(msg)
#else
  #define LOG_TRACE(msg) ((void)0)
  #define LOG_DEBUG(msg) ((void)0)
#endif

#define LOG_INFO(msg)  ::cm::Logger::info(msg)
#define LOG_WARN(msg)  ::cm::Logger::warn(msg)
#define LOG_ERROR(msg) ::cm::Logger::error(msg)

} // namespace cm
```

## 将来のJIT対応

```
[DEBUG] ════════════════════════════════════════════════════════
[DEBUG] Phase: JIT_COMPILE
[DEBUG] ════════════════════════════════════════════════════════
[DEBUG] Hot function detected: 'calculate' (1000+ calls)
[TRACE] Generating native code for 'calculate'
[TRACE] Optimization: Loop unrolling applied
[DEBUG] JIT compiled: 256 bytes of x86_64 code in 2.1ms
```
