---
title: HTTP通信
---

# std::http - HTTP通信ライブラリ

Cm標準ライブラリのHTTP通信モジュール。クライアントとサーバの両方をサポートします。

> **対応バックエンド:** Native (LLVM) のみ。OpenSSLが必要。

**最終更新:** 2026-02-08

---

## 概要

`std::http` はHTTP/HTTPS通信のための高レベルAPIを提供します。

- **HttpClient** - RESTクライアント (GET/POST/PUT/DELETE)
- **HttpRequest** - ビルダーパターンによるカスタムリクエスト
- **HttpServer** - シンプルなHTTPサーバ
- **ParsedUrl** - URL解析

---

## HttpClient（クライアント）

### 基本的な使い方

```cm
import std::http::HttpClient;
import std::http::HttpResponse;
import std::io::println;

int main() {
    HttpClient client;
    client.init("localhost", 8080);

    // GETリクエスト
    HttpResponse resp = client.get("/api/data");
    if (resp.is_ok == 1) {
        println("Status: {resp.status}");
        println("Body: {resp.body}");
    } else {
        println("Error: {resp.err_msg}");
    }

    return 0;
}
```

### HTTPメソッド

| メソッド | 説明 |
|---------|------|
| `client.get(path)` | GETリクエスト |
| `client.post(path, body)` | POSTリクエスト (JSON等) |
| `client.put(path, body)` | PUTリクエスト |
| `client.delete_req(path)` | DELETEリクエスト |

### HttpResponse

| フィールド | 型 | 説明 |
|-----------|-----|------|
| `status` | `int` | HTTPステータスコード (200, 404等) |
| `is_ok` | `int` | 成功=1, 失敗=0 |
| `body` | `string` | レスポンスボディ |
| `err_msg` | `string` | エラーメッセージ (失敗時) |

---

## HttpRequest（カスタムリクエスト）

カスタムヘッダーや認証が必要な場合に使用します。

```cm
import std::http::HttpRequest;
import std::http::HttpResponse;
import std::http::METHOD_POST;
import std::io::println;

int main() {
    HttpRequest req;
    req.init();
    req.set_method(METHOD_POST());
    req.set_url("api.example.com", 443, "/v1/data");
    req.set_json();                          // Content-Type: application/json
    req.set_bearer_auth("my-token");         // Bearer認証
    req.set_body("{\"key\": \"value\"}");
    req.set_timeout(5000);                   // 5秒タイムアウト

    HttpResponse resp = req.execute();
    println("Status: {resp.status}");

    return 0;
}
```

### リクエスト設定メソッド

| メソッド | 説明 |
|---------|------|
| `set_method(method)` | HTTPメソッド設定 |
| `set_url(host, port, path)` | URL設定 |
| `set_header(key, value)` | カスタムヘッダー追加 |
| `set_body(body)` | リクエストボディ設定 |
| `set_json()` | Content-Type: application/json |
| `set_timeout(ms)` | タイムアウト設定(ミリ秒) |
| `set_basic_auth(user, pass)` | Basic認証 |
| `set_bearer_auth(token)` | Bearer認証 |
| `set_follow_redirects(flag)` | リダイレクト追跡 |
| `execute()` | リクエスト実行 → HttpResponse |

### HTTPメソッド定数

| 定数 | 値 |
|------|-----|
| `METHOD_GET()` | 0 |
| `METHOD_POST()` | 1 |
| `METHOD_PUT()` | 2 |
| `METHOD_DELETE()` | 3 |
| `METHOD_PATCH()` | 4 |

---

## HttpServer（サーバ）

シンプルなHTTPサーバを構築できます。

```cm
import std::http::HttpServer;
import std::http::HttpServerRequest;
import std::io::println;

int main() {
    HttpServer server;
    server.init(8080);
    server.listen();
    println("Server started on port 8080");

    // 10リクエストを処理
    for (int i = 0; i < 10; i++) {
        HttpServerRequest req = server.accept();
        string method = req.method();
        string path = req.path();
        println("{method} {path}");

        if (path == "/api/hello") {
            req.respond(200, "{\"message\": \"Hello!\"}");
        } else {
            req.respond(404, "{\"error\": \"Not Found\"}");
        }
    }

    server.close();
    return 0;
}
```

### HttpServerRequest

| メソッド | 説明 |
|---------|------|
| `method()` | HTTPメソッド取得 ("GET", "POST"等) |
| `path()` | リクエストパス取得 |
| `body()` | リクエストボディ取得 |
| `header(key)` | ヘッダー値取得 |
| `respond(status, body)` | レスポンス送信（接続もクローズ） |

---

## URL解析

```cm
import std::http::parse_url;
import std::http::ParsedUrl;

ParsedUrl url = parse_url("https://api.example.com:8080/v1/data");
// url.scheme = "https"
// url.host   = "api.example.com"
// url.port   = 8080
// url.path   = "/v1/data"
```

---

## 注意事項

- HTTPS通信にはOpenSSLが必要です（`brew install openssl@3`）
- `HttpRequest.execute()` を呼んだ後、リクエストオブジェクトは再利用できません
- `HttpServerRequest.respond()` は1回のみ呼べます

---

**最終更新:** 2026-02-08
