// DOM操作 & 状態管理ランタイムブリッジ
// Cmから呼び出し可能なグローバル関数として定義
// CmのJSバックエンドの use js { ... } FFI経由で利用
//
// 注意: Cmの mapExternJsName は '_' を '.' に変換するため、
// 関数名にアンダースコアを使わないこと（camelCase）

// === タスクデータ（Cmからアクセス可能なStore） ===
const _store = {
    tasks: [],    // { id, text, done }
    nextId: 1
};

// === 状態操作API ===

// タスクを追加し、新しいIDを返す
function storeAddTask(text) {
    const id = _store.nextId++;
    _store.tasks.push({ id, text, done: false });
    return id;
}

// タスクの完了状態をトグル
function storeToggleTask(id) {
    const task = _store.tasks.find(t => t.id === id);
    if (task) task.done = !task.done;
}

// タスクを削除
function storeDeleteTask(id) {
    _store.tasks = _store.tasks.filter(t => t.id !== id);
}

// タスク数を返す
function storeGetCount() {
    return _store.tasks.length;
}

// 指定インデックスのタスクIDを返す
function storeGetId(index) {
    return _store.tasks[index] ? _store.tasks[index].id : -1;
}

// 指定インデックスのタスクテキストを返す
function storeGetText(index) {
    return _store.tasks[index] ? _store.tasks[index].text : "";
}

// 指定インデックスのタスク完了状態を返す
function storeIsDone(index) {
    return _store.tasks[index] ? _store.tasks[index].done : false;
}

// === DOM操作API ===

// 要素のinnerHTMLを設定
function domSetHTML(id, html) {
    const el = document.getElementById(id);
    if (el) el.innerHTML = html;
}

// input要素の値を取得
function domGetValue(id) {
    const el = document.getElementById(id);
    return el ? el.value.trim() : "";
}

// input要素の値をクリア
function domClearValue(id) {
    const el = document.getElementById(id);
    if (el) el.value = "";
}

// 要素の表示/非表示を切り替え
function domSetDisplay(id, display) {
    const el = document.getElementById(id);
    if (el) el.style.display = display;
}

// === イベント登録API ===

// Enterキーでグローバル関数を呼び出し
function domOnEnter(id, callbackName) {
    const el = document.getElementById(id);
    if (el) {
        el.addEventListener('keypress', (e) => {
            if (e.key === 'Enter' && typeof window[callbackName] === 'function') {
                window[callbackName]();
            }
        });
    }
}

// クリックでグローバル関数を呼び出し
function domOnClick(id, callbackName) {
    const el = document.getElementById(id);
    if (el) {
        el.onclick = () => {
            if (typeof window[callbackName] === 'function') {
                window[callbackName]();
            }
        };
    }
}

// チェックボックス変更でtoggleTask(id)を呼び出し
function domBindCheckbox(elementId, taskId) {
    const el = document.getElementById(elementId);
    if (el) {
        el.onchange = () => {
            if (typeof window['toggleTask'] === 'function') {
                window['toggleTask'](taskId);
            }
        };
    }
}

// 削除ボタンクリックでdeleteTask(id)を呼び出し
function domBindDelete(elementId, taskId) {
    const el = document.getElementById(elementId);
    if (el) {
        el.onclick = () => {
            if (typeof window['deleteTask'] === 'function') {
                window['deleteTask'](taskId);
            }
        };
    }
}
