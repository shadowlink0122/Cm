// このサンプル用の最小React互換実装（本番では実際の react パッケージを npm install する）。
// createElement と、関数コンポーネント・配列children・propsをサポートする。
function createElement(type, props, ...children) {
    return { type, props: props || {}, children: children.flat() };
}
function createElementList(type, props, children) {
    return { type, props: props || {}, children: (children || []).flat() };
}
module.exports = { createElement, createElementList };
