# Cm言語 モジュール相互運用設計

## 概要

Cm言語から外部のRustやTypeScriptモジュールを呼び出し、逆に外部からCmのモジュールを利用できる仕組みを設計する。

## 1. 外部モジュール呼び出し（Foreign Module Import）

### 1.1 Rustモジュールの呼び出し

```cm
// RustクレートをCmから使用
#[link(rust = "regex")]
extern mod regex {
    type Regex;

    fn new(pattern: string) -> Result<Regex, string>;
    fn is_match(&self, text: string) -> bool;
    fn find(&self, text: string) -> Option<Match>;
}

// 使用例
import regex;

int main() {
    let re = regex::new("[0-9]+").unwrap();
    if (re.is_match("abc123def")) {
        println("Found numbers!");
    }
    return 0;
}
```

### 1.2 TypeScript/JavaScriptモジュールの呼び出し

```cm
// npmパッケージをCmから使用
#[link(npm = "lodash")]
extern mod lodash {
    fn chunk<T>(array: Array<T>, size: int) -> Array<Array<T>>;
    fn debounce(func: fn(), wait: int) -> fn();
    fn merge<T>(object: T, ...sources: T[]) -> T;
}

// Node.js標準ライブラリ
#[link(node = "fs")]
extern mod fs {
    async fn readFile(path: string) -> string;
    async fn writeFile(path: string, data: string) -> void;
    fn existsSync(path: string) -> bool;
}

// 使用例
import lodash;
import fs;

async fn processFile() {
    if (fs::existsSync("data.txt")) {
        let content = await fs::readFile("data.txt");
        let lines = content.split("\n");
        let chunks = lodash::chunk(lines, 100);
        // 処理...
    }
}
```

## 2. 型マッピング

### 2.1 基本型マッピング

| Cm型 | Rust型 | TypeScript型 |
|------|--------|--------------|
| int | i32 | number |
| long | i64 | bigint |
| float | f32 | number |
| double | f64 | number |
| bool | bool | boolean |
| char | char | string |
| string | String | string |
| void | () | void |

### 2.2 複合型マッピング

```cm
// Cm側の型定義
struct Point {
    x: double;
    y: double;
}

// Rustへのマッピング
#[derive(Serialize, Deserialize)]
struct Point {
    x: f64,
    y: f64,
}

// TypeScriptへのマッピング
interface Point {
    x: number;
    y: number;
}
```

### 2.3 ジェネリクス型

```cm
// Cm側
struct Result<T, E> {
    value: Option<T>;
    error: Option<E>;
}

// Rust側
enum Result<T, E> {
    Ok(T),
    Err(E),
}

// TypeScript側
type Result<T, E> =
    | { ok: true; value: T }
    | { ok: false; error: E };
```

## 3. 実装アプローチ

### 3.1 コンパイル時バインディング生成

```yaml
# cm.toml - プロジェクト設定ファイル
[dependencies.rust]
regex = "1.5"
serde = { version = "1.0", features = ["derive"] }

[dependencies.npm]
lodash = "^4.17.21"
axios = "^0.27.2"

[build]
generate-bindings = true
binding-output = "src/bindings/"
```

### 3.2 実行時バインディング

#### Rust側（WebAssembly経由）

```rust
// Cm関数をRustから呼び出し
#[link(wasm = "cm_module.wasm")]
extern "C" {
    fn cm_calculate(x: f64, y: f64) -> f64;
}

// Rust関数をCmに公開
#[no_mangle]
pub extern "C" fn rust_process(data: *const c_char) -> *mut c_char {
    // 実装
}
```

#### TypeScript側（Node.js Addon API経由）

```typescript
// Cm関数をTypeScriptから呼び出し
import { CmModule } from './cm_bindings';

const result = CmModule.calculate(10, 20);

// TypeScript関数をCmに公開
export function tsProcess(data: string): string {
    // 実装
}
```

## 4. メモリ管理戦略

### 4.1 所有権の移動

```cm
// Rust側で所有権を持つ
#[move_ownership]
extern fn rust_take_ownership(data: string) -> void;

// Cm側で所有権を保持（借用）
#[borrow]
extern fn rust_borrow_data(data: &string) -> void;
```

### 4.2 ライフタイム管理

```cm
// ライフタイム注釈
fn process_with_lifetime<'a>(data: &'a string) -> &'a string {
    extern fn rust_transform<'a>(s: &'a string) -> &'a string;
    return rust_transform(data);
}
```

## 5. 非同期処理の統合

### 5.1 Promise/Future統合

```cm
// JavaScript Promiseとの統合
async fn fetch_data(url: string) -> Result<string, Error> {
    #[link(npm = "axios")]
    extern async fn get(url: string) -> Response;

    try {
        let response = await axios::get(url);
        return Ok(response.data);
    } catch (e: Error) {
        return Err(e);
    }
}

// Rust Futureとの統合
async fn process_async() -> int {
    #[link(rust = "tokio")]
    extern async fn sleep(ms: u64) -> void;

    await tokio::sleep(1000);
    return 42;
}
```

## 6. エラー処理

### 6.1 統一エラー型

```cm
// Cm側の統一エラー型
enum ForeignError {
    RustError(string),
    JsError(string),
    TypeError(string),
    NullPointer,
}

// 自動変換
impl From<rust::Error> for ForeignError {
    fn from(e: rust::Error) -> ForeignError {
        ForeignError::RustError(e.to_string())
    }
}
```

## 7. ビルドシステム統合

### 7.1 Cargoとの統合

```toml
# Cargo.toml
[dependencies]
cm-runtime = "0.1"
cm-macros = "0.1"

[build-dependencies]
cm-bindgen = "0.1"
```

### 7.2 package.jsonとの統合

```json
{
  "name": "cm-project",
  "dependencies": {
    "cm-runtime": "^0.1.0"
  },
  "scripts": {
    "build": "cm build --target=node",
    "test": "cm test"
  }
}
```

## 8. 実装フェーズ

### Phase 1: 基本FFI（現在）
- C ABI経由の基本的な関数呼び出し
- 基本型のマーシャリング

### Phase 2: 型安全なバインディング
- 自動バインディング生成
- 複合型のサポート
- エラー処理の統合

### Phase 3: 高度な統合
- ジェネリクス対応
- 非同期処理の完全サポート
- パッケージマネージャー統合

### Phase 4: 最適化
- ゼロコピー最適化
- インライン展開
- クロスランゲージLTO

## 9. 使用例：実用的なWebサーバー

```cm
// Rust製のWebフレームワークとTypeScript製のテンプレートエンジンを組み合わせ
#[link(rust = "actix-web")]
extern mod actix_web {
    type HttpServer;
    type App;
    async fn new() -> HttpServer;
    async fn bind(addr: string) -> Result<(), Error>;
}

#[link(npm = "handlebars")]
extern mod handlebars {
    fn compile(template: string) -> Template;
    fn render(template: Template, data: object) -> string;
}

async fn main() {
    let server = actix_web::HttpServer::new(|| {
        actix_web::App::new()
            .route("/", web::get(handle_index))
    });

    server.bind("127.0.0.1:8080")?.run().await?;
}

async fn handle_index(req: HttpRequest) -> HttpResponse {
    let template = handlebars::compile("<h1>Hello {{name}}!</h1>");
    let html = handlebars::render(template, { name: "Cm" });
    return HttpResponse::Ok().body(html);
}
```

## 10. セキュリティ考慮事項

- サンドボックス実行環境
- 型安全性の保証
- メモリ安全性の確保
- 信頼できないコードの隔離

## まとめ

この相互運用設計により、Cmは既存のRust/TypeScriptエコシステムを活用しながら、独自の言語機能を提供できる。段階的な実装により、基本的なFFIから始めて、最終的には完全に統合されたマルチ言語開発環境を実現する。