// react-dom/server の最小互換: 仮想要素ツリーを静的HTML文字列へ変換する
function escapeHtml(s) {
    return String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
}
function renderToStaticMarkup(node) {
    if (node === null || node === undefined) return "";
    if (typeof node === "string" || typeof node === "number") return escapeHtml(node);
    if (typeof node.type === "function") return renderToStaticMarkup(node.type(node.props));
    const attrs = Object.keys(node.props || {})
        .filter((k) => k !== "children" && node.props[k] !== undefined && node.props[k] !== null)
        .map((k) => " " + (k === "className" ? "class" : k) + '="' + escapeHtml(node.props[k]) + '"')
        .join("");
    const kids = (node.children || []).map(renderToStaticMarkup).join("");
    return "<" + node.type + attrs + ">" + kids + "</" + node.type + ">";
}
module.exports = { renderToStaticMarkup };
