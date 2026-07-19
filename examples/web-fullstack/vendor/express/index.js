// このサンプル用の最小Express互換実装（本番では実際の express パッケージを使う）。
// get登録・listen・text()レスポンスをサポートし、テスト用に __handle(method, path) で実行できる。
function express() {
    const routes = { GET: {}, POST: {} };
    const app = {
        get(path, handler) { routes.GET[path] = handler; return app; },
        post(path, handler) { routes.POST[path] = handler; return app; },
        listen(port, cb) { if (cb) cb(); return { close() {} }; },
        // テスト/デモ用: 実際のHTTPサーバを起動せずにルートを直接呼ぶ
        __handle(method, path) {
            const handler = (routes[method] || {})[path];
            if (!handler) return "404";
            let out = "";
            const res = {
                send(body) { out = String(body); return res; },
                type() { return res; },
                status() { return res; },
            };
            handler({ params: {}, body: {} }, res);
            return out;
        },
    };
    return app;
}
module.exports = express;
