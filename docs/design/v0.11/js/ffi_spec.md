# Cm FFI & モジュールシステム設計

## 1. 概要
本文書は、C言語（ネイティブ）とJavaScript（NPM）の両ターゲットをサポートすることに焦点を当てた、Cmにおける外部関数インターフェース（FFI）およびモジュールシステムの設計概要を記述します。

## 2. 構文案

### 2.1. ライブラリ/パッケージインポート (`use`)
`use` キーワードは、外部ライブラリやパッケージのシンボルをリンクおよび宣言するために使用します。

**現在の構文:**
```cm
use libc {
    int printf(char* format, ...);
}
```

**拡張案:**
`use` で文字列リテラルを許可し、特殊文字を含むパッケージ名（例: NPMパッケージ）をサポートします。

```cm
// Cライブラリ（識別子でOK）
use libc { ... }

// JSパッケージ（NPM互換性のために文字列リテラルを使用）
use "axios" as axios {
    ...
}

// スコープ付きパッケージ
use "@google/generative-ai" as genai {
    ...
}
```

### 2.2. グローバル/組み込み宣言 (`extern`)
`extern` キーワードは、特定の「ライブラリ」リンクを意味せず、グローバルスコープに存在するか、ランタイム環境/言語によって提供されるシンボルを宣言するために使用します。

```cm
// Cグローバル
extern "C" {
    int errno;
}

// JSグローバル（ブラウザ/Node環境）
extern "js" {
    // グローバルスコープに 'console' が存在することを宣言
    void console.log(string msg); // 構文は要検討、関数のみにするかなど
    
    // より良いアプローチ:
    class Console {
        void log(string msg);
    }
    Console console;
}
```

## 3. JavaScript FFI 詳細

### 3.1. 実現可能性
JSとの直接的なジェネリックFFIは実現可能です。Cmコンパイラ（JSバックエンド）は標準的なJavaScriptを生成するため、他のJSコードとの相互作用は容易です。

### 3.2. インポート戦略
- **NPMパッケージ**: `use "package-name" { ... }` でマッピング。
    - **コード生成**: `const alias = require("package-name");` または `import ...` を生成。
- **グローバルオブジェクト**: `extern "js" { ... }` でマッピング。

### 3.3. 型マッピング

| Cmの型 | JSの型 | 備考 |
|---------|---------|-------|
| `int`, `i32` | `number` | JSの整数は浮動小数点数（2^53まで安全） |
| `i64` | `BigInt` | 明示的なBigInt処理が必要 |
| `string` | `string` | UTF-8/16の差異はランタイムで吸収 |
| `bool` | `boolean` | 直接マッピング |
| `struct` | `object` | `{ field: val }` |
| `T[]` (slice) | `Array<T>` | JS配列にマッピング |
| `Option<T>` | `T \| null` | または `undefined` |
| `func` | `function` | コールバック対応 |

### 3.4. Async/Await
JSはPromiseに大きく依存しています。Cmにはこれに対する戦略が必要です。
- **フェーズ1**: `Promise<T>` を不透明なハンドルまたはジェネリック構造体として扱う。
- **フェーズ2**: `async`/`await` 構文をCmに追加（将来の課題）。

### 3.5. コード例

```cm
// Node.jsの 'fs' モジュールをインポート
use "fs" as fs {
    void writeFileSync(string path, string data);
    string readFileSync(string path, string encoding);
}

// グローバル JSON オブジェクト
extern "js" {
    class JSON {
        static string stringify(any obj);
        static any parse(string json);
    }
}

int main() {
    fs::writeFileSync("test.txt", "Hello FFI");
    return 0;
}
```

## 4. 実装ステップ

1.  **パーサー更新**:
    - `Parser::parse_use` を修正し、モジュールパスとして `TokenKind::StringLiteral` を受け入れるようにする。
2.  **AST更新**:
    - `ast::UseDecl` を更新し、文字列パスを保持できるようにする（または単に文字列に正規化）。
3.  **コード生成 (JS)**:
    - `emitUseDecl` を実装。
    - ファイル先頭に `require()` 文を生成。
    - FFI関数呼び出しを `alias.function()` にマッピング。

## 5. 推奨事項
- パッケージインポートには `use "string_literal" { ... }` を採用する。
- 環境グローバルには `extern "js" { ... }` を使用する。
- アドホックなスクリプトよりも「パッケージインストール」ワークフロー（NPM）を優先し、清潔さを保つ。Cmコンパイラは `node_modules` を管理する必要はなく、`require` 呼び出しを生成するだけでよい。`npm install` はユーザーの責任とする。
