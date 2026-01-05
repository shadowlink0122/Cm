# FFI (Foreign Function Interface) 設計

## 概要

CmからC/Rust/システムライブラリを呼び出すためのFFI設計です。
**`use *`構文**によるシンプルで直感的なFFI記法を採用しています。

## アーキテクチャ

```
┌─────────────────────────────────────────────────────────────┐
│                     Cm ユーザーコード                        │
├─────────────────────────────────────────────────────────────┤
│                    std:: ライブラリ                          │
│   ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │
│   │ std::io  │  │std::core │  │std::math │  │std::net  │   │
│   │ println  │  │  panic   │  │   sin    │  │  http    │   │
│   └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘   │
├────────┼─────────────┼─────────────┼─────────────┼──────────┤
│        │      std::sys (プラットフォーム抽象化)     │          │
│   ┌────┴─────────────┴─────────────┴─────────────┴────┐     │
│   │              プラットフォーム層                     │     │
│   │  ┌──────────┐  ┌──────────┐  ┌──────────┐        │     │
│   │  │  Linux   │  │  macOS   │  │ Windows  │        │     │
│   │  │ syscall  │  │ syscall  │  │  WinAPI  │        │     │
│   │  └────┬─────┘  └────┬─────┘  └────┬─────┘        │     │
│   └───────┼─────────────┼─────────────┼──────────────┘     │
├───────────┼─────────────┼─────────────┼─────────────────────┤
│       use libc / use curl       │    use js (WASM)        │
└─────────────────────────────────┴──────────────────────────┘
```

---

## 1. 基本構文: `use *` によるFFI

### シンプルなuse構文

```cm
// libc関数をインポート
use libc {
    int printf(cstring format, ...);
    *void malloc(usize size);
    void free(*void ptr);
}

int main() {
    printf("Hello from Cm!\n");
    return 0;
}
```

### 複数ライブラリの使用

```cm
// 標準Cライブラリ
use libc {
    int puts(cstring s);
    int open(cstring path, int flags);
    int close(int fd);
    isize read(int fd, *void buf, usize count);
    isize write(int fd, *void buf, usize count);
}

// libcurlをリンク
use curl {
    *void curl_easy_init();
    int curl_easy_perform(*void curl);
    void curl_easy_cleanup(*void curl);
}

// libsslをリンク  
use ssl {
    void SSL_library_init();
}
```

### 名前空間付きインポート

```cm
// 名前空間を指定してインポート
use libc as c {
    int printf(cstring format, ...);
}

int main() {
    c::printf("Hello!\n");  // 名前空間付きで呼び出し
    return 0;
}
```

---

## 2. プラットフォーム別FFI

### 条件付きuse

```cm
// プラットフォーム別のuse
#[os(linux)]
use libc {
    int epoll_create(int size);
    int epoll_wait(int epfd, *void events, int maxevents, int timeout);
}

#[os(macos)]
use libc {
    int kqueue();
    int kevent(int kq, *void changelist, int nchanges, *void eventlist, int nevents, *void timeout);
}

#[os(windows)]
use kernel32 {
    *void CreateIoCompletionPort(*void FileHandle, *void ExistingCompletionPort, usize CompletionKey, uint NumberOfConcurrentThreads);
}
```

### WASM向けFFI

```cm
// JavaScript関数をインポート
#[target(wasm)]
use js {
    void console_log(string msg);
    void alert(string msg);
}

// DOM操作
#[target(wasm)]
use js::dom {
    *void document_getElementById(string id);
    void element_setInnerHTML(*void elem, string html);
    void addEventListener(*void elem, string event, fn() callback);
}

// WASI
#[target(wasi)]
use wasi {
    int fd_write(int fd, *iovec iovs, int iovs_len, *int nwritten);
    int fd_read(int fd, *iovec iovs, int iovs_len, *int nread);
}
```

---

## 3. 型マッピング

### プリミティブ型

| Cm         | C              | サイズ   | 備考                |
|------------|----------------|----------|---------------------|
| `tiny`     | `int8_t`       | 1 byte   | 符号付き8bit       |
| `utiny`    | `uint8_t`      | 1 byte   | 符号なし8bit       |
| `short`    | `int16_t`      | 2 bytes  | 符号付き16bit      |
| `ushort`   | `uint16_t`     | 2 bytes  | 符号なし16bit      |
| `int`      | `int32_t`      | 4 bytes  | 符号付き32bit      |
| `uint`     | `uint32_t`     | 4 bytes  | 符号なし32bit      |
| `long`     | `int64_t`      | 8 bytes  | 符号付き64bit      |
| `ulong`    | `uint64_t`     | 8 bytes  | 符号なし64bit      |
| `float`    | `float`        | 4 bytes  | IEEE 754単精度     |
| `double`   | `double`       | 8 bytes  | IEEE 754倍精度     |
| `bool`     | `_Bool`        | 1 byte   | 0/1                |
| `char`     | `char`         | 1 byte   | UTF-8コードユニット|

### ポインタ型

| Cm           | C              | 説明                    |
|--------------|----------------|-------------------------|
| `*T`         | `T*`           | 可変ポインタ            |
| `*const T`   | `const T*`     | 不変ポインタ            |
| `*void`      | `void*`        | 汎用ポインタ            |

### 特殊型

| Cm           | C              | 説明                    |
|--------------|----------------|-------------------------|
| `usize`      | `size_t`       | ポインタサイズ符号なし  |
| `isize`      | `ssize_t`      | ポインタサイズ符号付き  |
| `cstring`    | `const char*`  | NULL終端文字列          |

---

## 4. リンク設定

### use構文でのリンク（自動検出）

```cm
// ライブラリ名から自動でリンク設定を推論
use curl {
    *void curl_easy_init();
}

// libc は特別扱い（常にリンク済み）
use libc {
    int printf(cstring format, ...);
}
```

### パス指定（importと同じ形式）

```cm
// ローカルライブラリをパスでインポート（importと同じ形式）
use ./libs/mylib {
    int my_function();
}

// 絶対パス
use /usr/local/lib/custom {
    int custom_func();
}

// 相対パス（プロジェクトからの相対）
use libs::graphics::opengl {
    void glClear(uint mask);
    void glDrawArrays(int mode, int first, int count);
}

// フレームワーク（macOS）- 属性で指定
#[os(macos)]
#[framework]
use CoreFoundation {
    void CFRelease(*void cf);
}

// 静的リンク - 属性で指定
#[static]
use mylib {
    int my_function();
}
```

### Cm.toml でのリンク設定

```toml
[package]
name = "my-app"
version = "0.1.0"

[ffi]
# 共通リンク設定
libraries = ["curl", "ssl", "crypto"]
search_paths = ["/usr/local/lib"]

[ffi.linux]
libraries = ["pthread", "dl"]

[ffi.macos]
frameworks = ["Security", "CoreFoundation"]

[ffi.windows]
libraries = ["ws2_32", "advapi32"]
```

---

## 5. プラットフォーム抽象化 (std::sys)

### ディレクトリ構造

```
std/
├── sys/
│   ├── mod.cm              # プラットフォーム共通インターフェース
│   ├── linux/
│   │   ├── mod.cm          # Linux実装
│   │   ├── io.cm
│   │   ├── fs.cm
│   │   └── net.cm
│   ├── macos/
│   │   ├── mod.cm          # macOS実装
│   │   ├── io.cm
│   │   └── ...
│   └── windows/
│       ├── mod.cm          # Windows実装
│       └── ...
├── io/
│   └── mod.cm              # std::sys を使う高レベルAPI
└── ...
```

### プラットフォーム検出

```cm
// std/sys/mod.cm
module std::sys;

// コンパイル時にプラットフォームを選択
#[os(linux)]
export use std::sys::linux;

#[os(macos)]
export use std::sys::macos;

#[os(windows)]
export use std::sys::windows;
```

### Linux実装例

```cm
// std/sys/linux/io.cm
module std::sys::linux::io;

// システムコール番号（x86_64）をenumで管理
enum Syscall : int {
    READ  = 0,
    WRITE = 1,
    OPEN  = 2,
    CLOSE = 3,
    STAT  = 4,
    FSTAT = 5,
    LSTAT = 6,
    POLL  = 7,
    LSEEK = 8,
    MMAP  = 9,
    MPROTECT = 10,
    MUNMAP = 11,
    BRK = 12,
    IOCTL = 16,
    PREAD64 = 17,
    PWRITE64 = 18,
    READV = 19,
    WRITEV = 20,
    ACCESS = 21,
    PIPE = 22,
    SELECT = 23,
    SCHED_YIELD = 24,
    DUP = 32,
    DUP2 = 33,
    PAUSE = 34,
    NANOSLEEP = 35,
    GETPID = 39,
    SOCKET = 41,
    CONNECT = 42,
    ACCEPT = 43,
    SENDTO = 44,
    RECVFROM = 45,
    BIND = 49,
    LISTEN = 50,
    FORK = 57,
    VFORK = 58,
    EXECVE = 59,
    EXIT = 60,
    WAIT4 = 61,
    KILL = 62,
    UNAME = 63,
    FCNTL = 72,
    FLOCK = 73,
    FSYNC = 74,
    TRUNCATE = 76,
    FTRUNCATE = 77,
    GETCWD = 79,
    CHDIR = 80,
    RENAME = 82,
    MKDIR = 83,
    RMDIR = 84,
    CREAT = 85,
    LINK = 86,
    UNLINK = 87,
    SYMLINK = 88,
    READLINK = 89,
    CHMOD = 90,
    CHOWN = 92,
    GETUID = 102,
    GETGID = 104,
    GETEUID = 107,
    GETEGID = 108,
    EPOLL_CREATE = 213,
    EPOLL_CTL = 233,
    EPOLL_WAIT = 232,
}

// ファイルディスクリプタ
enum FileDescriptor : int {
    STDIN  = 0,
    STDOUT = 1,
    STDERR = 2,
}

// ファイルオープンフラグ
enum OpenFlags : int {
    RDONLY    = 0x0000,
    WRONLY    = 0x0001,
    RDWR      = 0x0002,
    CREAT     = 0x0040,
    EXCL      = 0x0080,
    TRUNC     = 0x0200,
    APPEND    = 0x0400,
    NONBLOCK  = 0x0800,
    SYNC      = 0x101000,
}

// ファイルパーミッション
enum FileMode : uint {
    S_IRWXU = 0o700,   // 所有者: rwx
    S_IRUSR = 0o400,   // 所有者: r
    S_IWUSR = 0o200,   // 所有者: w
    S_IXUSR = 0o100,   // 所有者: x
    S_IRWXG = 0o070,   // グループ: rwx
    S_IRGRP = 0o040,   // グループ: r
    S_IWGRP = 0o020,   // グループ: w
    S_IXGRP = 0o010,   // グループ: x
    S_IRWXO = 0o007,   // その他: rwx
    S_IROTH = 0o004,   // その他: r
    S_IWOTH = 0o002,   // その他: w
    S_IXOTH = 0o001,   // その他: x
}

// libc経由のFFI
use libc {
    isize write(int fd, *void buf, usize count);
    isize read(int fd, *void buf, usize count);
}

// 高レベルAPI
export isize sys_write(int fd, *utiny buf, usize len) {
    return write(fd, buf as *void, len);
}

export isize sys_read(int fd, *utiny buf, usize len) {
    return read(fd, buf as *void, len);
}
```

### macOS実装例

```cm
// std/sys/macos/io.cm
module std::sys::macos::io;

// macOSシステムコール番号（BSD系、x86_64/arm64）
// macOSはBSD系なのでシステムコール番号がLinuxと異なる
enum Syscall : int {
    EXIT = 1,
    FORK = 2,
    READ = 3,
    WRITE = 4,
    OPEN = 5,
    CLOSE = 6,
    WAIT4 = 7,
    LINK = 9,
    UNLINK = 10,
    CHDIR = 12,
    CHMOD = 15,
    GETPID = 20,
    GETUID = 24,
    GETEUID = 25,
    GETGID = 47,
    GETEGID = 43,
    KILL = 37,
    MKDIR = 136,
    RMDIR = 137,
    DUP = 41,
    PIPE = 42,
    SOCKET = 97,
    CONNECT = 98,
    ACCEPT = 30,
    BIND = 104,
    LISTEN = 106,
    SELECT = 93,
    FSYNC = 95,
    RENAME = 128,
    FTRUNCATE = 201,
    STAT = 188,
    LSTAT = 190,
    FSTAT = 189,
    MMAP = 197,
    MUNMAP = 73,
    MPROTECT = 74,
    FCNTL = 92,
    NANOSLEEP = 240,
    KQUEUE = 362,
    KEVENT = 363,
}

// ファイルディスクリプタ
enum FileDescriptor : int {
    STDIN  = 0,
    STDOUT = 1,
    STDERR = 2,
}

// ファイルオープンフラグ（macOS）
enum OpenFlags : int {
    RDONLY    = 0x0000,
    WRONLY    = 0x0001,
    RDWR      = 0x0002,
    CREAT     = 0x0200,
    EXCL      = 0x0800,
    TRUNC     = 0x0400,
    APPEND    = 0x0008,
    NONBLOCK  = 0x0004,
    SYNC      = 0x0080,
}

use libc {
    isize write(int fd, *void buf, usize count);
    isize read(int fd, *void buf, usize count);
}

export isize sys_write(int fd, *utiny buf, usize len) {
    return write(fd, buf as *void, len);
}

export isize sys_read(int fd, *utiny buf, usize len) {
    return read(fd, buf as *void, len);
}
```

### Windows実装例

```cm
// std/sys/windows/io.cm
module std::sys::windows::io;

// Windows APIには「システムコール」という概念がないが、
// NTDLLの内部関数としてNtXxx関数が存在する
// 通常はkernel32.dll経由でWinAPIを使用

// ファイルアクセス権限
enum FileAccess : uint {
    GENERIC_READ    = 0x80000000,
    GENERIC_WRITE   = 0x40000000,
    GENERIC_EXECUTE = 0x20000000,
    GENERIC_ALL     = 0x10000000,
}

// ファイル共有モード
enum FileShare : uint {
    NONE   = 0x00,
    READ   = 0x01,
    WRITE  = 0x02,
    DELETE = 0x04,
}

// ファイル作成モード
enum CreationDisposition : uint {
    CREATE_NEW        = 1,
    CREATE_ALWAYS     = 2,
    OPEN_EXISTING     = 3,
    OPEN_ALWAYS       = 4,
    TRUNCATE_EXISTING = 5,
}

// ファイル属性
enum FileAttributes : uint {
    READONLY            = 0x00000001,
    HIDDEN              = 0x00000002,
    SYSTEM              = 0x00000004,
    DIRECTORY           = 0x00000010,
    ARCHIVE             = 0x00000020,
    NORMAL              = 0x00000080,
    TEMPORARY           = 0x00000100,
    SPARSE_FILE         = 0x00000200,
    REPARSE_POINT       = 0x00000400,
    COMPRESSED          = 0x00000800,
    ENCRYPTED           = 0x00004000,
}

// 標準ハンドル
enum StdHandle : uint {
    INPUT  = 0xFFFFFFF6,  // -10
    OUTPUT = 0xFFFFFFF5,  // -11
    ERROR  = 0xFFFFFFF4,  // -12
}

// Windows API エラーコード
enum WinError : uint {
    SUCCESS              = 0,
    FILE_NOT_FOUND       = 2,
    PATH_NOT_FOUND       = 3,
    ACCESS_DENIED        = 5,
    INVALID_HANDLE       = 6,
    NOT_ENOUGH_MEMORY    = 8,
    INVALID_DATA         = 13,
    NO_MORE_FILES        = 18,
    SHARING_VIOLATION    = 32,
    FILE_EXISTS          = 80,
    INVALID_PARAMETER    = 87,
    BROKEN_PIPE          = 109,
    ALREADY_EXISTS       = 183,
    IO_PENDING           = 997,
}

// WinSock エラーコード
enum WsaError : int {
    WSABASEERR           = 10000,
    WSAEINTR             = 10004,
    WSAEACCES            = 10013,
    WSAEFAULT            = 10014,
    WSAEINVAL            = 10022,
    WSAEMFILE            = 10024,
    WSAEWOULDBLOCK       = 10035,
    WSAEINPROGRESS       = 10036,
    WSAEALREADY          = 10037,
    WSAENOTSOCK          = 10038,
    WSAECONNREFUSED      = 10061,
    WSAETIMEDOUT         = 10060,
}

// kernel32.dll FFI
use kernel32 {
    *void GetStdHandle(uint nStdHandle);
    bool WriteFile(*void hFile, *void lpBuffer, uint nNumberOfBytesToWrite, *uint lpNumberOfBytesWritten, *void lpOverlapped);
    bool ReadFile(*void hFile, *void lpBuffer, uint nNumberOfBytesToRead, *uint lpNumberOfBytesRead, *void lpOverlapped);
    *void CreateFileA(cstring lpFileName, uint dwDesiredAccess, uint dwShareMode, *void lpSecurityAttributes, uint dwCreationDisposition, uint dwFlagsAndAttributes, *void hTemplateFile);
    bool CloseHandle(*void hObject);
    uint GetLastError();
}

// 高レベルAPI
export isize sys_write(int fd, *utiny buf, usize len) {
    *void handle = GetStdHandle(StdHandle::OUTPUT as uint);
    uint written = 0;
    if (WriteFile(handle, buf as *void, len as uint, &written, null)) {
        return written as isize;
    }
    return -1;
}

export isize sys_read(int fd, *utiny buf, usize len) {
    *void handle = GetStdHandle(StdHandle::INPUT as uint);
    uint read_bytes = 0;
    if (ReadFile(handle, buf as *void, len as uint, &read_bytes, null)) {
        return read_bytes as isize;
    }
    return -1;
}
```

---

## 6. HTTPクライアント設計

### 方針: libcurl FFI

HTTPはlibcurlを使用（OSに依存しない、安定したAPI）

```cm
// std/net/http.cm
module std::net::http;

import std::io;

// libcurl FFI
use curl {
    *void curl_easy_init();
    int curl_easy_setopt(*void curl, int option, ...);
    int curl_easy_perform(*void curl);
    void curl_easy_cleanup(*void curl);
    cstring curl_easy_strerror(int code);
}

// CURLOPTオプション定数
enum CurlOption : int {
    // 動作オプション
    VERBOSE           = 41,
    HEADER            = 42,
    NOPROGRESS        = 43,
    NOSIGNAL          = 99,
    
    // ネットワークオプション
    URL               = 10002,
    PORT              = 3,
    TIMEOUT           = 13,
    TIMEOUT_MS        = 155,
    CONNECTTIMEOUT    = 78,
    CONNECTTIMEOUT_MS = 156,
    
    // HTTPオプション
    HTTPHEADER        = 10023,
    CUSTOMREQUEST     = 10036,
    POSTFIELDS        = 10015,
    POSTFIELDSIZE     = 60,
    
    // 認証オプション
    USERPWD           = 10005,
    HTTPAUTH          = 107,
    
    // SSL/TLSオプション
    SSL_VERIFYPEER    = 64,
    SSL_VERIFYHOST    = 81,
    CAINFO            = 10065,
    
    // コールバックオプション
    WRITEFUNCTION     = 20011,
    WRITEDATA         = 10001,
    READFUNCTION      = 20012,
    READDATA          = 10009,
    HEADERFUNCTION    = 20079,
    HEADERDATA        = 10029,
    
    // プロキシオプション
    PROXY             = 10004,
    PROXYPORT         = 59,
    PROXYTYPE         = 101,
}

// CURLコード（戻り値）
enum CurlCode : int {
    OK                    = 0,
    UNSUPPORTED_PROTOCOL  = 1,
    FAILED_INIT           = 2,
    URL_MALFORMAT         = 3,
    COULDNT_RESOLVE_PROXY = 5,
    COULDNT_RESOLVE_HOST  = 6,
    COULDNT_CONNECT       = 7,
    REMOTE_ACCESS_DENIED  = 9,
    OPERATION_TIMEDOUT    = 28,
    SSL_CONNECT_ERROR     = 35,
    SSL_CERTPROBLEM       = 58,
    SSL_CACERT            = 60,
    SEND_ERROR            = 55,
    RECV_ERROR            = 56,
}

// HTTPメソッド
enum HttpMethod : int {
    GET     = 0,
    POST    = 1,
    PUT     = 2,
    DELETE  = 3,
    PATCH   = 4,
    HEAD    = 5,
    OPTIONS = 6,
}

// HTTPステータスコード
enum HttpStatus : int {
    // 2xx Success
    OK                    = 200,
    CREATED               = 201,
    ACCEPTED              = 202,
    NO_CONTENT            = 204,
    
    // 3xx Redirection
    MOVED_PERMANENTLY     = 301,
    FOUND                 = 302,
    NOT_MODIFIED          = 304,
    TEMPORARY_REDIRECT    = 307,
    PERMANENT_REDIRECT    = 308,
    
    // 4xx Client Errors
    BAD_REQUEST           = 400,
    UNAUTHORIZED          = 401,
    FORBIDDEN             = 403,
    NOT_FOUND             = 404,
    METHOD_NOT_ALLOWED    = 405,
    CONFLICT              = 409,
    GONE                  = 410,
    UNPROCESSABLE_ENTITY  = 422,
    TOO_MANY_REQUESTS     = 429,
    
    // 5xx Server Errors
    INTERNAL_SERVER_ERROR = 500,
    NOT_IMPLEMENTED       = 501,
    BAD_GATEWAY           = 502,
    SERVICE_UNAVAILABLE   = 503,
    GATEWAY_TIMEOUT       = 504,
}

// HTTPレスポンス
export struct HttpResponse {
    int status_code;
    string body;
}

// HTTPクライアント
export struct HttpClient {
    *void handle;
    
    static HttpClient new() {
        HttpClient client;
        client.handle = curl_easy_init();
        return client;
    }
    
    HttpResponse get(string url) {
        curl_easy_setopt(this.handle, CurlOption::URL as int, url.c_str());
        int result = curl_easy_perform(this.handle);
        // ... レスポンス処理
        return response;
    }
    
    HttpResponse post(string url, string body) {
        curl_easy_setopt(this.handle, CurlOption::URL as int, url.c_str());
        curl_easy_setopt(this.handle, CurlOption::POSTFIELDS as int, body.c_str());
        return response;
    }
    
    void drop() {
        if (this.handle != null) {
            curl_easy_cleanup(this.handle);
        }
    }
}

// 簡易API
export HttpResponse http_get(string url) {
    HttpClient client = HttpClient::new();
    HttpResponse response = client.get(url);
    client.drop();
    return response;
}
```

### 使用例

```cm
import std::net::http;
import std::io;

int main() {
    HttpResponse res = http_get("https://api.example.com/data");
    
    if (res.status_code == HttpStatus::OK as int) {
        println(res.body);
    } else if (res.status_code == HttpStatus::NOT_FOUND as int) {
        println("Resource not found");
    } else {
        println("Error: " + to_string(res.status_code));
    }
    
    return 0;
}
```

---

## 7. 構造体レイアウト

### C互換レイアウト

```cm
#[repr(C)]
struct Point {
    int x;
    int y;
}

#[repr(C, packed)]
struct PackedData {
    utiny a;
    int b;      // パディングなし
}

#[repr(C, align(16))]
struct Aligned {
    double x;
    double y;
}
```

### 構造体のFFI使用

```cm
#[repr(C)]
struct TimeSpec {
    long tv_sec;
    long tv_nsec;
}

use libc {
    int clock_gettime(int clk_id, *TimeSpec tp);
}

int main() {
    TimeSpec ts;
    clock_gettime(0, &ts);  // CLOCK_REALTIME
    println("Seconds: " + to_string(ts.tv_sec));
    return 0;
}
```

---

## 8. unsafeブロック

### 明示的unsafe

```cm
// ポインタ操作はunsafeブロック内
void example() {
    unsafe {
        *int ptr = malloc(sizeof(int)) as *int;
        *ptr = 42;
        free(ptr as *void);
    }
}
```

### FFI関数の自動unsafe

```cm
// use で宣言した関数は暗黙的にunsafe
use libc {
    *void malloc(usize size);
}

void example() {
    // 直接呼び出し可能（将来的にはunsafe必須に）
    *void ptr = malloc(100);
    
    // 推奨: 安全なラッパーを使用
    // Buffer buf = Buffer::new(100);
}
```

---

## 9. use構文の詳細

### 基本形式

```cm
// システムライブラリ
use <library_name> {
    <return_type> <function_name>(<parameters>);
    ...
}

// 名前空間付き
use <library_name> as <namespace> {
    ...
}

// パス形式（importと統一）
use ./path/to/lib {
    ...
}

use libs::graphics::opengl {
    ...
}

// 属性付き
#[static]
#[os(linux)]
use <library_name> {
    ...
}
```

### サポートされる属性

| 属性 | 説明 | 例 |
|-----------|------|-----|
| `#[static]` | 静的ライブラリとしてリンク | `#[static] use mylib {...}` |
| `#[framework]` | macOSフレームワーク | `#[framework] use CoreFoundation {...}` |
| `#[os(...)]` | プラットフォーム条件 | `#[os(linux)] use epoll {...}` |
| `#[target(...)]` | ターゲット条件 | `#[target(wasm)] use js {...}` |

### パス形式

| 形式 | 説明 | 例 |
|------|------|-----|
| `./path` | 相対パス（カレントから） | `use ./libs/mylib {...}` |
| `/path` | 絶対パス | `use /usr/local/lib/custom {...}` |
| `path::to::lib` | モジュールパス | `use libs::graphics::opengl {...}` |
| `name` | システムライブラリ | `use curl {...}` |

### 予約ライブラリ名

| ライブラリ名 | 説明 | 自動リンク |
|-------------|------|-----------|
| `libc` | 標準Cライブラリ | 常にリンク済み |
| `libm` | 数学ライブラリ | 自動リンク |
| `pthread` | POSIXスレッド | Linux/macOS |
| `js` | JavaScript (WASM) | WASMターゲット |
| `wasi` | WASI | WASIターゲット |

---

## 10. 実装ステータス

| 機能                    | 状態  | 備考                           |
|-------------------------|-------|--------------------------------|
| use libc 基本構文       | 📋    | 設計完了、実装予定             |
| 関数呼び出し            | ✅    | LLVM経由（extern "C"）         |
| 基本型マッピング        | ✅    | int, float, pointer           |
| use [options]           | 📋    | 将来実装                       |
| #[repr(C)]              | 📋    | 将来実装                       |
| varargs                 | ⚠️    | printf限定                     |
| コールバック            | 📋    | 将来実装                       |
| std::sys 抽象化         | 📋    | 将来実装                       |
| use js (WASM)           | 📋    | 将来実装                       |
| プラットフォーム条件    | 📋    | #[os(...)] 実装予定            |

---

## 11. 将来の拡張

### Rustクレート連携

```cm
// Rust crateをリンク
use rust::my_crate {
    int rust_function(int x);
}
```

### C++連携（限定的）

```cm
// extern "C"でエクスポートされたC++関数のみ
use cpp::mylib {
    void cpp_function_with_c_linkage();
}
```

---

## 注意事項

> [!WARNING]
> FFIは本質的にunsafeです。以下に注意してください：
> - ポインタのライフタイム管理
> - メモリリーク防止
> - NULL チェック
> - スレッドセーフティ
> - エンディアン（ネットワーク通信時）

---

## 旧構文との比較

### 旧構文（extern "C"）

```cm
// 旧: extern "C" 構文
extern "C" {
    int printf(const char* format, ...);
}

#[link(name = "curl")]
extern "C" {
    void* curl_easy_init();
}
```

### 新構文（use）

```cm
// 新: use 構文（推奨）
use libc {
    int printf(cstring format, ...);
}

use curl {
    *void curl_easy_init();
}
```

### importとuseの統一パス形式

```cm
// Cmモジュールのインポート
import std::io;
import ./libs/mymodule;
import libs::utils::helper;

// FFIライブラリのインポート（同じパス形式）
use std::sys::linux {
    isize write(int fd, *void buf, usize count);
}

use ./libs/native/graphics {
    void draw_pixel(int x, int y, uint color);
}

use libs::ffi::opengl {
    void glClear(uint mask);
}
```

新構文のメリット：
- importと同じパス形式で直感的
- ライブラリ名が明示的
- 名前空間サポート
- 属性によるプラットフォーム条件との統合が容易
- `[option]`のような特殊構文が不要
