// このサンプル用の最小Express互換（本番では実際の express を使う）。
// アプリのロジック（分岐・HTML/CSS生成・レスポンス組み立て）はCm側のroute()が行い、
// このFFIはHTTPソケット（listen・リクエスト受信）だけを担う薄い境界。
// このデモではroute()をmainから直接呼ぶため未使用だが、実サーバ起動の雛形として置く。
const http = require("http");
function createServer(onRequest) {
    return http.createServer((req, res) => {
        const result = onRequest(req.method, req.url, "");
        res.statusCode = result.status || 200;
        res.setHeader("content-type", result.content_type || "text/plain");
        res.end(result.body || "");
    });
}
module.exports = { createServer };
