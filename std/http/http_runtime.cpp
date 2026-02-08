// http_runtime.cpp - Cm HTTP クライアント stdバッキング実装
// TCP (std::net) ベースの HTTP/1.1 クライアント
// リクエスト構築・レスポンス解析をC++側で実装

#include <arpa/inet.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// HTTPS (TLS) 対応 — OpenSSLが利用可能な場合のみ
#ifdef CM_HAS_OPENSSL
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

// ============================================================
// HTTPメソッド定数
// ============================================================
static const int HTTP_GET = 0;
static const int HTTP_POST = 1;
static const int HTTP_PUT = 2;
static const int HTTP_DELETE = 3;
static const int HTTP_PATCH = 4;

// ============================================================
// 内部構造体
// ============================================================

struct CmHttpRequest {
    int method;  // HTTP_GET, HTTP_POST, etc.
    std::string host;
    int port;
    std::string path;
    std::map<std::string, std::string> headers;
    std::string body;
    int timeout_ms;         // タイムアウト（ミリ秒、0=無制限）
    bool follow_redirects;  // リダイレクト自動追跡
    int max_redirects;      // 最大リダイレクト回数
};

struct CmHttpResponse {
    int status_code;
    std::string status_text;
    std::map<std::string, std::string> headers;
    std::string body;
    std::string error_message;
    bool is_error;
};

// ============================================================
// ヘルパー関数
// ============================================================

// メソッド文字列を取得
static const char* method_string(int method) {
    switch (method) {
        case HTTP_GET:
            return "GET";
        case HTTP_POST:
            return "POST";
        case HTTP_PUT:
            return "PUT";
        case HTTP_DELETE:
            return "DELETE";
        case HTTP_PATCH:
            return "PATCH";
        default:
            return "GET";
    }
}

// HTTPリクエスト文字列を構築
static std::string build_request(const CmHttpRequest* req) {
    std::string request;
    request += method_string(req->method);
    request += " ";
    request += req->path;
    request += " HTTP/1.1\r\n";
    request += "Host: ";
    request += req->host;
    if (req->port != 80 && req->port != 443) {
        request += ":";
        request += std::to_string(req->port);
    }
    request += "\r\n";

    // ユーザーヘッダー
    for (const auto& h : req->headers) {
        request += h.first + ": " + h.second + "\r\n";
    }

    // Content-Lengthヘッダー（ボディがある場合）
    if (!req->body.empty()) {
        request += "Content-Length: " + std::to_string(req->body.size()) + "\r\n";
    }

    // Content-Typeヘッダー（未設定でボディがある場合）
    if (!req->body.empty() && req->headers.find("Content-Type") == req->headers.end()) {
        request += "Content-Type: application/json\r\n";
    }

    // 接続管理
    request += "Connection: close\r\n";
    request += "\r\n";

    // ボディ
    if (!req->body.empty()) {
        request += req->body;
    }

    return request;
}

// HTTPレスポンスをパース
static CmHttpResponse* parse_response(const std::string& raw) {
    auto* resp = new CmHttpResponse();
    resp->is_error = false;

    // ステータスライン解析: "HTTP/1.1 200 OK\r\n"
    size_t first_line_end = raw.find("\r\n");
    if (first_line_end == std::string::npos) {
        resp->is_error = true;
        resp->error_message = "Invalid HTTP response: no status line";
        resp->status_code = -1;
        return resp;
    }

    std::string status_line = raw.substr(0, first_line_end);

    // "HTTP/x.x " をスキップ
    size_t space1 = status_line.find(' ');
    if (space1 == std::string::npos) {
        resp->is_error = true;
        resp->error_message = "Invalid status line";
        resp->status_code = -1;
        return resp;
    }

    size_t space2 = status_line.find(' ', space1 + 1);
    std::string code_str = (space2 != std::string::npos)
                               ? status_line.substr(space1 + 1, space2 - space1 - 1)
                               : status_line.substr(space1 + 1);
    resp->status_code = atoi(code_str.c_str());

    if (space2 != std::string::npos) {
        resp->status_text = status_line.substr(space2 + 1);
    }

    // ヘッダー解析
    size_t headers_start = first_line_end + 2;
    size_t headers_end = raw.find("\r\n\r\n", headers_start);
    if (headers_end == std::string::npos) {
        // ヘッダーのみ、ボディなし
        headers_end = raw.size();
    }

    std::string headers_section = raw.substr(headers_start, headers_end - headers_start);
    size_t pos = 0;
    while (pos < headers_section.size()) {
        size_t line_end = headers_section.find("\r\n", pos);
        if (line_end == std::string::npos) {
            line_end = headers_section.size();
        }
        std::string line = headers_section.substr(pos, line_end - pos);
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string value = line.substr(colon + 1);
            // 先頭空白を除去
            size_t val_start = value.find_first_not_of(' ');
            if (val_start != std::string::npos) {
                value = value.substr(val_start);
            }
            resp->headers[key] = value;
        }
        pos = line_end + 2;
    }

    // ボディ
    if (headers_end + 4 <= raw.size()) {
        resp->body = raw.substr(headers_end + 4);
    }

    return resp;
}

// TCP接続してデータ送受信
static int tcp_connect_and_communicate(const std::string& host, int port,
                                       const std::string& request, std::string& response) {
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result) != 0) {
        return -1;
    }

    int fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(result);
        return -2;
    }

    if (connect(fd, result->ai_addr, result->ai_addrlen) < 0) {
        close(fd);
        freeaddrinfo(result);
        return -3;
    }
    freeaddrinfo(result);

    // TCP_NODELAY
    int opt = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    // リクエスト送信
    const char* data = request.c_str();
    size_t total = request.size();
    size_t sent = 0;
    while (sent < total) {
        ssize_t n = write(fd, data + sent, total - sent);
        if (n <= 0) {
            close(fd);
            return -4;
        }
        sent += static_cast<size_t>(n);
    }

    // レスポンス受信（最大64KB）
    char buf[4096];
    response.clear();
    while (true) {
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        if (n <= 0)
            break;
        buf[n] = '\0';
        response += buf;
        // レスポンス完了判定（Connection: close前提）
        if (response.size() > 65536)
            break;
    }

    close(fd);
    return 0;
}

// ============================================================
// TLS (HTTPS) 通信
// ============================================================
#ifdef CM_HAS_OPENSSL

// OpenSSL初期化（プロセスで1度だけ）
static SSL_CTX* get_ssl_context() {
    static SSL_CTX* ctx = nullptr;
    if (!ctx) {
        const SSL_METHOD* method = TLS_client_method();
        ctx = SSL_CTX_new(method);
        if (ctx) {
            // システムのCA証明書を読み込み
            SSL_CTX_set_default_verify_paths(ctx);
            // TLS 1.2以上を要求
            SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
        }
    }
    return ctx;
}

// TLS接続してデータ送受信
static int tls_connect_and_communicate(const std::string& host, int port,
                                       const std::string& request, std::string& response) {
    // TCP接続
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result) != 0) {
        return -1;  // DNS解決失敗
    }

    int fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(result);
        return -2;  // ソケット作成失敗
    }

    if (connect(fd, result->ai_addr, result->ai_addrlen) < 0) {
        close(fd);
        freeaddrinfo(result);
        return -3;  // 接続拒否
    }
    freeaddrinfo(result);

    // SSL コンテキスト取得
    SSL_CTX* ctx = get_ssl_context();
    if (!ctx) {
        close(fd);
        return -5;  // SSL初期化失敗
    }

    // SSL接続作成
    SSL* ssl = SSL_new(ctx);
    if (!ssl) {
        close(fd);
        return -5;
    }

    // SNI (Server Name Indication) 設定
    SSL_set_tlsext_host_name(ssl, host.c_str());
    SSL_set_fd(ssl, fd);

    // TLSハンドシェイク
    if (SSL_connect(ssl) <= 0) {
        SSL_free(ssl);
        close(fd);
        return -6;  // TLSハンドシェイク失敗
    }

    // リクエスト送信
    const char* data = request.c_str();
    int total = static_cast<int>(request.size());
    int sent = 0;
    while (sent < total) {
        int n = SSL_write(ssl, data + sent, total - sent);
        if (n <= 0) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
            close(fd);
            return -4;  // 送信失敗
        }
        sent += n;
    }

    // レスポンス受信
    char buf[4096];
    response.clear();
    while (true) {
        int n = SSL_read(ssl, buf, sizeof(buf) - 1);
        if (n <= 0)
            break;
        buf[n] = '\0';
        response.append(buf, n);
        if (response.size() > 65536)
            break;
    }

    // クリーンアップ
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(fd);
    return 0;
}

#endif  // CM_HAS_OPENSSL

extern "C" {

// ============================================================
// HTTPリクエスト API
// ============================================================

// リクエストハンドル作成
int64_t cm_http_request_create() {
    auto* req = new CmHttpRequest();
    req->method = HTTP_GET;
    req->port = 80;
    req->path = "/";
    req->timeout_ms = 0;
    req->follow_redirects = true;
    req->max_redirects = 5;
    return reinterpret_cast<int64_t>(req);
}

// メソッド設定 (0=GET, 1=POST, 2=PUT, 3=DELETE)
void cm_http_request_set_method(int64_t handle, int32_t method) {
    auto* req = reinterpret_cast<CmHttpRequest*>(handle);
    if (req)
        req->method = method;
}

// URL設定（ホスト、ポート、パス）
void cm_http_request_set_url(int64_t handle, const char* host, int32_t port, const char* path) {
    auto* req = reinterpret_cast<CmHttpRequest*>(handle);
    if (req) {
        req->host = host ? host : "";
        req->port = port;
        req->path = path ? path : "/";
    }
}

// ヘッダー追加
void cm_http_request_set_header(int64_t handle, const char* key, const char* value) {
    auto* req = reinterpret_cast<CmHttpRequest*>(handle);
    if (req && key && value) {
        req->headers[key] = value;
    }
}

// ボディ設定
void cm_http_request_set_body(int64_t handle, const char* body) {
    auto* req = reinterpret_cast<CmHttpRequest*>(handle);
    if (req && body) {
        req->body = body;
    }
}

// リクエスト破棄
void cm_http_request_destroy(int64_t handle) {
    auto* req = reinterpret_cast<CmHttpRequest*>(handle);
    delete req;
}

// ============================================================
// HTTPリクエスト実行
// ============================================================

// リクエストを実行し、レスポンスハンドルを返す
// 戻り値: レスポンスハンドル（成功/エラーどちらもレスポンスとして返す）
int64_t cm_http_execute(int64_t req_handle) {
    auto* req = reinterpret_cast<CmHttpRequest*>(req_handle);
    if (!req) {
        auto* resp = new CmHttpResponse();
        resp->is_error = true;
        resp->error_message = "Invalid request handle";
        resp->status_code = -1;
        return reinterpret_cast<int64_t>(resp);
    }

    // リクエスト文字列を構築
    std::string request_str = build_request(req);

    // 通信（ポート443の場合はTLS、それ以外はTCP）
    std::string raw_response;
    int err;
#ifdef CM_HAS_OPENSSL
    if (req->port == 443) {
        err = tls_connect_and_communicate(req->host, req->port, request_str, raw_response);
    } else {
        err = tcp_connect_and_communicate(req->host, req->port, request_str, raw_response);
    }
#else
    err = tcp_connect_and_communicate(req->host, req->port, request_str, raw_response);
#endif
    if (err != 0) {
        auto* resp = new CmHttpResponse();
        resp->is_error = true;
        resp->status_code = -1;
        switch (err) {
            case -1:
                resp->error_message = "DNS resolution failed for host: " + req->host;
                break;
            case -2:
                resp->error_message = "Socket creation failed";
                break;
            case -3:
                resp->error_message =
                    "Connection refused: " + req->host + ":" + std::to_string(req->port);
                break;
            case -4:
                resp->error_message = "Failed to send request";
                break;
            case -5:
                resp->error_message = "TLS initialization failed";
                break;
            case -6:
                resp->error_message =
                    "TLS handshake failed: " + req->host + ":" + std::to_string(req->port);
                break;
            default:
                resp->error_message = "Unknown network error";
                break;
        }
        return reinterpret_cast<int64_t>(resp);
    }

    if (raw_response.empty()) {
        auto* resp = new CmHttpResponse();
        resp->is_error = true;
        resp->error_message = "Empty response from server";
        resp->status_code = -1;
        return reinterpret_cast<int64_t>(resp);
    }

    // レスポンスパース
    CmHttpResponse* resp = parse_response(raw_response);
    return reinterpret_cast<int64_t>(resp);
}

// ============================================================
// HTTPレスポンス API
// ============================================================

// ステータスコード取得（エラー時は-1）
int32_t cm_http_response_status(int64_t handle) {
    auto* resp = reinterpret_cast<CmHttpResponse*>(handle);
    if (!resp)
        return -1;
    return resp->status_code;
}

// レスポンスボディ取得（strdup でCm側への所有権移譲）
const char* cm_http_response_body(int64_t handle) {
    auto* resp = reinterpret_cast<CmHttpResponse*>(handle);
    if (!resp)
        return strdup("");
    return strdup(resp->body.c_str());
}

// レスポンスヘッダー取得（strdup でCm側への所有権移譲）
const char* cm_http_response_header(int64_t handle, const char* key) {
    auto* resp = reinterpret_cast<CmHttpResponse*>(handle);
    if (!resp || !key)
        return strdup("");
    auto it = resp->headers.find(key);
    if (it == resp->headers.end())
        return strdup("");
    return strdup(it->second.c_str());
}

// エラーかどうか
int32_t cm_http_response_is_error(int64_t handle) {
    auto* resp = reinterpret_cast<CmHttpResponse*>(handle);
    if (!resp)
        return 1;
    return resp->is_error ? 1 : 0;
}

// エラーメッセージ取得（strdup でCm側への所有権移譲）
const char* cm_http_error_message(int64_t handle) {
    auto* resp = reinterpret_cast<CmHttpResponse*>(handle);
    if (!resp)
        return strdup("Invalid response handle");
    return strdup(resp->error_message.c_str());
}

// レスポンス破棄
void cm_http_response_destroy(int64_t handle) {
    auto* resp = reinterpret_cast<CmHttpResponse*>(handle);
    delete resp;
}

// ============================================================
// 簡易リクエストAPI（リクエスト構築→実行→リクエスト破棄を一度に）
// ============================================================

// GET リクエスト
int64_t cm_http_get(const char* host, int32_t port, const char* path) {
    int64_t req = cm_http_request_create();
    cm_http_request_set_method(req, HTTP_GET);
    cm_http_request_set_url(req, host, port, path);
    int64_t resp = cm_http_execute(req);
    cm_http_request_destroy(req);
    return resp;
}

// POST リクエスト
int64_t cm_http_post(const char* host, int32_t port, const char* path, const char* body) {
    int64_t req = cm_http_request_create();
    cm_http_request_set_method(req, HTTP_POST);
    cm_http_request_set_url(req, host, port, path);
    if (body)
        cm_http_request_set_body(req, body);
    int64_t resp = cm_http_execute(req);
    cm_http_request_destroy(req);
    return resp;
}

// PUT リクエスト
int64_t cm_http_put(const char* host, int32_t port, const char* path, const char* body) {
    int64_t req = cm_http_request_create();
    cm_http_request_set_method(req, HTTP_PUT);
    cm_http_request_set_url(req, host, port, path);
    if (body)
        cm_http_request_set_body(req, body);
    int64_t resp = cm_http_execute(req);
    cm_http_request_destroy(req);
    return resp;
}

// DELETE リクエスト
int64_t cm_http_delete(const char* host, int32_t port, const char* path) {
    int64_t req = cm_http_request_create();
    cm_http_request_set_method(req, HTTP_DELETE);
    cm_http_request_set_url(req, host, port, path);
    int64_t resp = cm_http_execute(req);
    cm_http_request_destroy(req);
    return resp;
}

// ============================================================
// HTTPサーバ API（Cm側でルーティング・レスポンスを制御）
// ============================================================

// サーバ側リクエスト情報を保持する構造体
struct CmHttpServerRequest {
    int client_fd;
    std::string method;
    std::string path;
    std::string body;
    std::map<std::string, std::string> headers;
};

// サーバソケットを作成し、bind+listen（SO_REUSEADDR設定済み）
int64_t cm_http_server_create(int32_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -2;
    }

    if (listen(fd, 16) < 0) {
        close(fd);
        return -3;
    }

    return static_cast<int64_t>(fd);
}

// サーバソケットをクローズ
void cm_http_server_close(int64_t server_fd) {
    if (server_fd > 0) {
        close(static_cast<int>(server_fd));
    }
}

// クライアント接続を受け付け、HTTPリクエストをパースして返す（ブロッキング）
int64_t cm_http_server_accept(int64_t server_fd) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int client_fd = accept(static_cast<int>(server_fd), (struct sockaddr*)&client_addr, &addr_len);
    if (client_fd < 0)
        return 0;

    // TCP_NODELAY設定
    int nodelay = 1;
    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    // HTTPリクエストを受信・パース
    char buf[4096];
    std::string raw;

    while (true) {
        ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
        if (n <= 0)
            break;
        buf[n] = '\0';
        raw += buf;
        if (raw.find("\r\n\r\n") != std::string::npos)
            break;
    }

    auto* req = new CmHttpServerRequest();
    req->client_fd = client_fd;

    if (raw.empty()) {
        req->method = "";
        req->path = "";
        return reinterpret_cast<int64_t>(req);
    }

    // リクエストライン解析: "GET /path HTTP/1.1\r\n"
    size_t first_line_end = raw.find("\r\n");
    if (first_line_end == std::string::npos) {
        return reinterpret_cast<int64_t>(req);
    }

    std::string request_line = raw.substr(0, first_line_end);
    size_t sp1 = request_line.find(' ');
    size_t sp2 = request_line.find(' ', sp1 + 1);
    if (sp1 != std::string::npos) {
        req->method = request_line.substr(0, sp1);
    }
    if (sp1 != std::string::npos && sp2 != std::string::npos) {
        req->path = request_line.substr(sp1 + 1, sp2 - sp1 - 1);
    }

    // ヘッダー解析
    size_t headers_start = first_line_end + 2;
    size_t headers_end = raw.find("\r\n\r\n", headers_start);
    if (headers_end != std::string::npos) {
        std::string headers_section = raw.substr(headers_start, headers_end - headers_start);
        size_t pos = 0;
        while (pos < headers_section.size()) {
            size_t line_end = headers_section.find("\r\n", pos);
            if (line_end == std::string::npos)
                line_end = headers_section.size();
            std::string line = headers_section.substr(pos, line_end - pos);
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = line.substr(0, colon);
                std::string value = line.substr(colon + 1);
                size_t vs = value.find_first_not_of(' ');
                if (vs != std::string::npos)
                    value = value.substr(vs);
                req->headers[key] = value;
            }
            pos = line_end + 2;
        }
    }

    // ボディ読み取り（Content-Lengthベース）
    if (headers_end != std::string::npos) {
        std::string partial_body = raw.substr(headers_end + 4);
        auto cl_it = req->headers.find("Content-Length");
        if (cl_it != req->headers.end()) {
            int content_length = atoi(cl_it->second.c_str());
            req->body = partial_body;
            while (static_cast<int>(req->body.size()) < content_length) {
                ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
                if (n <= 0)
                    break;
                buf[n] = '\0';
                req->body += buf;
            }
        }
    }

    return reinterpret_cast<int64_t>(req);
}

// リクエストのHTTPメソッド取得（strdup）
const char* cm_http_server_req_method(int64_t handle) {
    auto* req = reinterpret_cast<CmHttpServerRequest*>(handle);
    if (!req)
        return strdup("");
    return strdup(req->method.c_str());
}

// リクエストのパス取得（strdup）
const char* cm_http_server_req_path(int64_t handle) {
    auto* req = reinterpret_cast<CmHttpServerRequest*>(handle);
    if (!req)
        return strdup("");
    return strdup(req->path.c_str());
}

// リクエストのボディ取得（strdup）
const char* cm_http_server_req_body(int64_t handle) {
    auto* req = reinterpret_cast<CmHttpServerRequest*>(handle);
    if (!req)
        return strdup("");
    return strdup(req->body.c_str());
}

// リクエストのヘッダー取得（strdup）
const char* cm_http_server_req_header(int64_t handle, const char* key) {
    auto* req = reinterpret_cast<CmHttpServerRequest*>(handle);
    if (!req || !key)
        return strdup("");
    auto it = req->headers.find(key);
    if (it == req->headers.end())
        return strdup("");
    return strdup(it->second.c_str());
}

// レスポンス送信（HTTPレスポンスを構築して送信し、接続をクローズ）
void cm_http_server_respond(int64_t handle, int32_t status, const char* body) {
    auto* req = reinterpret_cast<CmHttpServerRequest*>(handle);
    if (!req)
        return;

    // ステータステキスト
    const char* status_text = "OK";
    if (status == 201)
        status_text = "Created";
    else if (status == 400)
        status_text = "Bad Request";
    else if (status == 404)
        status_text = "Not Found";
    else if (status == 500)
        status_text = "Internal Server Error";

    std::string response;
    response += "HTTP/1.1 " + std::to_string(status) + " " + status_text + "\r\n";
    response += "Content-Type: application/json\r\n";
    std::string body_str = body ? body : "";
    response += "Content-Length: " + std::to_string(body_str.size()) + "\r\n";
    response += "Connection: close\r\n";
    response += "\r\n";
    response += body_str;

    const char* data = response.c_str();
    size_t total = response.size();
    size_t sent = 0;
    while (sent < total) {
        ssize_t n = write(req->client_fd, data + sent, total - sent);
        if (n <= 0)
            break;
        sent += static_cast<size_t>(n);
    }

    close(req->client_fd);
    delete req;
}

// リクエスト破棄（respondを呼ばなかった場合の後始末）
void cm_http_server_req_destroy(int64_t handle) {
    auto* req = reinterpret_cast<CmHttpServerRequest*>(handle);
    if (req) {
        close(req->client_fd);
        delete req;
    }
}

// ============================================================
// テスト用ミニHTTPサーバ（レガシー: 後方互換性維持）
// ============================================================

// 簡易HTTPレスポンスを構築して送信
static void send_http_response(int client_fd, int status, const char* status_text,
                               const char* content_type, const std::string& body) {
    std::string response;
    response += "HTTP/1.1 " + std::to_string(status) + " " + status_text + "\r\n";
    response += "Content-Type: ";
    response += content_type;
    response += "\r\n";
    response += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    response += "Connection: close\r\n";
    response += "\r\n";
    response += body;

    const char* data = response.c_str();
    size_t total = response.size();
    size_t sent = 0;
    while (sent < total) {
        ssize_t n = write(client_fd, data + sent, total - sent);
        if (n <= 0)
            break;
        sent += static_cast<size_t>(n);
    }
}

// HTTPリクエストを受信・パース
struct MiniServerRequest {
    std::string method;
    std::string path;
    std::string body;
    std::map<std::string, std::string> headers;
};

static MiniServerRequest parse_client_request(int client_fd) {
    MiniServerRequest req;
    char buf[4096];
    std::string raw;

    // ヘッダーを受信
    while (true) {
        ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
        if (n <= 0)
            break;
        buf[n] = '\0';
        raw += buf;
        if (raw.find("\r\n\r\n") != std::string::npos)
            break;
    }

    if (raw.empty())
        return req;

    // リクエストライン解析: "GET /path HTTP/1.1\r\n"
    size_t first_line_end = raw.find("\r\n");
    if (first_line_end == std::string::npos)
        return req;

    std::string request_line = raw.substr(0, first_line_end);
    size_t sp1 = request_line.find(' ');
    size_t sp2 = request_line.find(' ', sp1 + 1);
    if (sp1 != std::string::npos) {
        req.method = request_line.substr(0, sp1);
    }
    if (sp1 != std::string::npos && sp2 != std::string::npos) {
        req.path = request_line.substr(sp1 + 1, sp2 - sp1 - 1);
    }

    // ヘッダー解析
    size_t headers_start = first_line_end + 2;
    size_t headers_end = raw.find("\r\n\r\n", headers_start);
    if (headers_end != std::string::npos) {
        std::string headers_section = raw.substr(headers_start, headers_end - headers_start);
        size_t pos = 0;
        while (pos < headers_section.size()) {
            size_t line_end = headers_section.find("\r\n", pos);
            if (line_end == std::string::npos)
                line_end = headers_section.size();
            std::string line = headers_section.substr(pos, line_end - pos);
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = line.substr(0, colon);
                std::string value = line.substr(colon + 1);
                size_t vs = value.find_first_not_of(' ');
                if (vs != std::string::npos)
                    value = value.substr(vs);
                req.headers[key] = value;
            }
            pos = line_end + 2;
        }
    }

    // ボディ読み取り（Content-Lengthベース）
    if (headers_end != std::string::npos) {
        std::string partial_body = raw.substr(headers_end + 4);
        auto cl_it = req.headers.find("Content-Length");
        if (cl_it != req.headers.end()) {
            int content_length = atoi(cl_it->second.c_str());
            req.body = partial_body;
            // 残りのボディを読み取り
            while (static_cast<int>(req.body.size()) < content_length) {
                ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
                if (n <= 0)
                    break;
                buf[n] = '\0';
                req.body += buf;
            }
        }
    }

    return req;
}

// ミニHTTPサーバ起動（テスト用）
// max_requests回のリクエストを処理してシャットダウン
// ルーティング:
//   GET  /api/hello     → {"message": "Hello, World!"}
//   POST /api/echo      → リクエストボディをそのまま返す
//   PUT  /api/update    → {"updated": true}
//   DELETE /api/remove  → {"deleted": true}
//   その他              → 404
int64_t cm_http_test_server_start(int32_t port, int32_t max_requests) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -2;
    }

    if (listen(fd, 16) < 0) {
        close(fd);
        return -3;
    }

    // max_requests分のリクエストを処理
    for (int i = 0; i < max_requests; i++) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(fd, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0)
            continue;

        // TCP_NODELAY
        int nodelay = 1;
        setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

        MiniServerRequest req = parse_client_request(client_fd);

        // ルーティング
        if (req.method == "GET" && req.path == "/api/hello") {
            send_http_response(client_fd, 200, "OK", "application/json",
                               "{\"message\": \"Hello, World!\"}");
        } else if (req.method == "POST" && req.path == "/api/echo") {
            send_http_response(client_fd, 200, "OK", "application/json", req.body);
        } else if (req.method == "PUT" && req.path == "/api/update") {
            send_http_response(client_fd, 200, "OK", "application/json", "{\"updated\": true}");
        } else if (req.method == "DELETE" && req.path == "/api/remove") {
            send_http_response(client_fd, 200, "OK", "application/json", "{\"deleted\": true}");
        } else {
            send_http_response(client_fd, 404, "Not Found", "application/json",
                               "{\"reason\": \"Not Found\"}");
        }

        close(client_fd);
    }

    close(fd);
    return 0;
}

// ============================================================
// URLパース
// ============================================================

// URL文字列を解析してscheme/host/port/pathに分離
// url: "http://example.com:8080/api/v1" or "https://example.com/path"
// 戻り値: パースされたURL情報ハンドル（失敗時0）

struct CmParsedUrl {
    std::string scheme;
    std::string host;
    int port;
    std::string path;
};

int64_t cm_http_parse_url(const char* url) {
    if (!url)
        return 0;

    auto* parsed = new CmParsedUrl();
    std::string url_str(url);

    // scheme
    size_t scheme_end = url_str.find("://");
    if (scheme_end == std::string::npos) {
        parsed->scheme = "http";
        scheme_end = 0;
    } else {
        parsed->scheme = url_str.substr(0, scheme_end);
        scheme_end += 3;  // "://" をスキップ
    }

    // host[:port]/path
    std::string rest = url_str.substr(scheme_end);
    size_t path_start = rest.find('/');
    std::string host_port;
    if (path_start == std::string::npos) {
        host_port = rest;
        parsed->path = "/";
    } else {
        host_port = rest.substr(0, path_start);
        parsed->path = rest.substr(path_start);
    }

    // port
    size_t port_sep = host_port.find(':');
    if (port_sep == std::string::npos) {
        parsed->host = host_port;
        parsed->port = (parsed->scheme == "https") ? 443 : 80;
    } else {
        parsed->host = host_port.substr(0, port_sep);
        parsed->port = std::stoi(host_port.substr(port_sep + 1));
    }

    return reinterpret_cast<int64_t>(parsed);
}

// パース結果取得
const char* cm_http_parsed_scheme(int64_t handle) {
    auto* p = reinterpret_cast<CmParsedUrl*>(handle);
    return p ? strdup(p->scheme.c_str()) : nullptr;
}

const char* cm_http_parsed_host(int64_t handle) {
    auto* p = reinterpret_cast<CmParsedUrl*>(handle);
    return p ? strdup(p->host.c_str()) : nullptr;
}

int32_t cm_http_parsed_port(int64_t handle) {
    auto* p = reinterpret_cast<CmParsedUrl*>(handle);
    return p ? static_cast<int32_t>(p->port) : 0;
}

const char* cm_http_parsed_path(int64_t handle) {
    auto* p = reinterpret_cast<CmParsedUrl*>(handle);
    return p ? strdup(p->path.c_str()) : nullptr;
}

void cm_http_parsed_url_destroy(int64_t handle) {
    auto* p = reinterpret_cast<CmParsedUrl*>(handle);
    delete p;
}

// ============================================================
// タイムアウト設定
// ============================================================

void cm_http_request_set_timeout(int64_t handle, int32_t timeout_ms) {
    auto* req = reinterpret_cast<CmHttpRequest*>(handle);
    if (req)
        req->timeout_ms = timeout_ms;
}

// ============================================================
// リダイレクト設定
// ============================================================

void cm_http_request_set_follow_redirects(int64_t handle, int32_t follow) {
    auto* req = reinterpret_cast<CmHttpRequest*>(handle);
    if (req)
        req->follow_redirects = (follow != 0);
}

void cm_http_request_set_max_redirects(int64_t handle, int32_t max_redirects) {
    auto* req = reinterpret_cast<CmHttpRequest*>(handle);
    if (req)
        req->max_redirects = max_redirects;
}

// ============================================================
// 認証
// ============================================================

// Basic認証ヘッダーを設定
void cm_http_request_set_basic_auth(int64_t handle, const char* user, const char* pass) {
    auto* req = reinterpret_cast<CmHttpRequest*>(handle);
    if (!req || !user || !pass)
        return;

    // Base64エンコード（簡易実装）
    std::string creds = std::string(user) + ":" + std::string(pass);
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    int val = 0, valb = -6;
    for (unsigned char c : creds) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            encoded.push_back(b64[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6)
        encoded.push_back(b64[((val << 8) >> (valb + 8)) & 0x3F]);
    while (encoded.size() % 4)
        encoded.push_back('=');

    req->headers["Authorization"] = "Basic " + encoded;
}

// Bearerトークン認証ヘッダーを設定
void cm_http_request_set_bearer_auth(int64_t handle, const char* token) {
    auto* req = reinterpret_cast<CmHttpRequest*>(handle);
    if (!req || !token)
        return;
    req->headers["Authorization"] = std::string("Bearer ") + token;
}

// ============================================================
// Content-Type簡易設定
// ============================================================

void cm_http_request_set_content_type(int64_t handle, const char* content_type) {
    auto* req = reinterpret_cast<CmHttpRequest*>(handle);
    if (!req || !content_type)
        return;
    req->headers["Content-Type"] = content_type;
}

// JSON Content-Type設定
void cm_http_request_set_json(int64_t handle) {
    auto* req = reinterpret_cast<CmHttpRequest*>(handle);
    if (!req)
        return;
    req->headers["Content-Type"] = "application/json";
}

// ============================================================
// レスポンス追加情報
// ============================================================

// Content-Typeヘッダーを取得
const char* cm_http_response_content_type(int64_t handle) {
    auto* resp = reinterpret_cast<CmHttpResponse*>(handle);
    if (!resp)
        return nullptr;
    auto it = resp->headers.find("content-type");
    if (it == resp->headers.end()) {
        it = resp->headers.find("Content-Type");
    }
    if (it != resp->headers.end()) {
        return strdup(it->second.c_str());
    }
    return nullptr;
}

// Locationヘッダーを取得（リダイレクト先URL）
const char* cm_http_response_location(int64_t handle) {
    auto* resp = reinterpret_cast<CmHttpResponse*>(handle);
    if (!resp)
        return nullptr;
    auto it = resp->headers.find("location");
    if (it == resp->headers.end()) {
        it = resp->headers.find("Location");
    }
    if (it != resp->headers.end()) {
        return strdup(it->second.c_str());
    }
    return nullptr;
}

// ステータスコードがリダイレクトかどうか
int32_t cm_http_response_is_redirect(int64_t handle) {
    auto* resp = reinterpret_cast<CmHttpResponse*>(handle);
    if (!resp)
        return 0;
    return (resp->status_code == 301 || resp->status_code == 302 || resp->status_code == 303 ||
            resp->status_code == 307 || resp->status_code == 308)
               ? 1
               : 0;
}

}  // extern "C"
