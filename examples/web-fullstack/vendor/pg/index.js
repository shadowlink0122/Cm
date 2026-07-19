// このサンプル用の最小Postgres互換ドライバ（本番では実際の pg パッケージを npm install し実DBへ接続する）。
// アプリ固有ロジックは持たず、SQL文字列を解釈してインメモリのタスクテーブルを操作する汎用エグゼキュータ。
// Cm側が SQL を書き、行→struct のマッピングを行う。
let rows = [
    { id: 1, title: "learn Cm", done: true },
    { id: 2, title: "build a web app in Cm", done: false },
];
let nextId = 3;
function query(sql) {
    const s = String(sql).trim().toLowerCase();
    if (s.startsWith("select")) return rows.slice().sort((a, b) => a.id - b.id);
    return [];
}
function insert(title) {
    const r = { id: nextId++, title: String(title), done: false };
    rows.push(r);
    return r;
}
function update(id, done) {
    const r = rows.find((x) => x.id === id);
    if (r) r.done = done;
    return r || { id: 0, title: "", done: false };
}
module.exports = { query, insert, update };
