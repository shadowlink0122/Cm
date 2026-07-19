// このサンプル用の最小Postgres互換クライアント（本番では実際の pg パッケージを npm install し、
// 実DBへ接続する）。ここではインメモリのタスクテーブルで query() を模倣する。
let rows = [
    { id: 1, title: "learn Cm", done: true },
    { id: 2, title: "build a web app", done: false },
];
let nextId = 3;
function Client(_config) {
    return {
        connect() { return Promise.resolve(); },
        end() { return Promise.resolve(); },
        // 実際のpgと同じ { rows } 形状を返す。$1等のプレースホルダを簡易サポート
        query(sql, params) {
            params = params || [];
            const s = sql.trim().toLowerCase();
            if (s.startsWith("select")) {
                return Promise.resolve({ rows: rows.slice() });
            }
            if (s.startsWith("insert")) {
                const row = { id: nextId++, title: params[0], done: false };
                rows.push(row);
                return Promise.resolve({ rows: [row] });
            }
            if (s.startsWith("update")) {
                const id = params[0];
                const r = rows.find((x) => x.id === id);
                if (r) r.done = !r.done;
                return Promise.resolve({ rows: r ? [r] : [] });
            }
            return Promise.resolve({ rows: [] });
        },
    };
}
// このサンプル用の同期ヘルパー（実際のpgは async client.query(sql) を使う。README参照）。
// SELECT id, title, done FROM tasks 相当
function allTasks() { return rows.slice(); }
// INSERT INTO tasks(title) VALUES($1) RETURNING * 相当
function addTask(title) { const r = { id: nextId++, title, done: false }; rows.push(r); return r; }
// UPDATE tasks SET done = NOT done WHERE id = $1 相当
function toggleTask(id) { const r = rows.find((x) => x.id === id); if (r) r.done = !r.done; return r || { id: 0, title: "", done: false }; }
module.exports = { Client, allTasks, addTask, toggleTask };
