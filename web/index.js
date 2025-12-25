"use strict";

function __cm_unwrap(val) {
    if (val && val.__boxed) return val[0];
    return val;
}

// Cm Runtime Helpers
function __cm_slice(arr, start, end) {
    if (start < 0) start = arr.length + start;
    if (end === undefined) end = arr.length;
    else if (end < 0) end = arr.length + end;
    return arr.slice(start, end);
}

function __cm_str_slice(str, start, end) {
    const len = str.length;
    if (start < 0) start = len + start;
    if (start < 0) start = 0;
    if (end === undefined || end === null) end = len;
    else if (end < 0) end = len + end + 1;
    if (end < 0) end = 0;
    if (start > len) start = len;
    if (end > len) end = len;
    if (start > end) return '';
    return str.substring(start, end);
}

function __cm_deep_equal(a, b) {
    if (a === b) return true;
    if (a === null || b === null) return false;
    if (typeof a !== 'object' || typeof b !== 'object') return false;
    if (Array.isArray(a)) {
        if (!Array.isArray(b) || a.length !== b.length) return false;
        for (let i = 0; i < a.length; i++) {
            if (!__cm_deep_equal(a[i], b[i])) return false;
        }
        return true;
    }
    // struct comparison
    const keysA = Object.keys(a);
    const keysB = Object.keys(b);
    if (keysA.length !== keysB.length) return false;
    for (const key of keysA) {
        if (!keysB.includes(key) || !__cm_deep_equal(a[key], b[key])) return false;
    }
    return true;
}

function __cm_array_init(size, defaultVal) {
    return Array(size).fill(typeof defaultVal === 'object' ? null : defaultVal);
}

function __cm_clone(obj) {
    if (obj === null || typeof obj !== 'object') return obj;
    if (Array.isArray(obj)) return obj.map(__cm_clone);
    const result = {};
    for (const key in obj) result[key] = __cm_clone(obj[key]);
    return result;
}

function __cm_format(val, spec) {
    if (!spec) return String(val);
    // char型変換
    if (spec === 'c') return String.fromCharCode(val);
    // 基数指定
    if (spec === 'x') return val.toString(16);
    if (spec === 'X') return val.toString(16).toUpperCase();
    if (spec === 'b') return val.toString(2);
    if (spec === 'o') return val.toString(8);
    // 科学記法
    if (spec === 'e') return val.toExponential();
    if (spec === 'E') return val.toExponential().toUpperCase();
    // 小数点精度 .N
    let precMatch = spec.match(/^\.(\d+)$/);
    if (precMatch) return val.toFixed(parseInt(precMatch[1]));
    // 科学記法+精度 .Ne, .NE
    precMatch = spec.match(/^\.(\d+)([eE])$/);
    if (precMatch) {
        let result = val.toExponential(parseInt(precMatch[1]));
        return precMatch[2] === 'E' ? result.toUpperCase() : result;
    }
    // 幅とアライメント
    let alignMatch = spec.match(/^([<>^]?)(\d+)$/);
    if (alignMatch) {
        let align = alignMatch[1] || '>';
        let width = parseInt(alignMatch[2]);
        let s = String(val);
        if (s.length >= width) return s;
        let pad = ' '.repeat(width - s.length);
        if (align === '<') return s + pad;
        if (align === '>') return pad + s;
        let half = Math.floor(pad.length / 2);
        return pad.slice(0, half) + s + pad.slice(half);
    }
    // ゼロパディング 0>N
    let zeroPadMatch = spec.match(/^0>(\d+)$/);
    if (zeroPadMatch) {
        let width = parseInt(zeroPadMatch[1]);
        return String(val).padStart(width, '0');
    }
    return String(val);
}

function __cm_format_string(format, values) {
    let result = format;
    let idx = 0;
    // エスケープされた波括弧を一時的に置換
    result = result.replace(/\{\{/g, '\x00LBRACE\x00');
    result = result.replace(/\}\}/g, '\x00RBRACE\x00');
    // フォーマット指定子付きプレースホルダを置換 {name:spec} or {:spec}
    result = result.replace(/\{[^}]*\}/g, (match) => {
        let inner = match.slice(1, -1);
        let spec = '';
        let colonIdx = inner.indexOf(':');
        if (colonIdx >= 0) spec = inner.slice(colonIdx + 1);
        return __cm_format(values[idx++], spec);
    });
    // エスケープを復元
    result = result.replace(/\x00LBRACE\x00/g, '{');
    result = result.replace(/\x00RBRACE\x00/g, '}');
    return result;
}

function __cm_output_element() {
    if (typeof document === 'undefined') return null;
    let el = document.getElementById('cm-output');
    if (!el) {
        el = document.createElement('pre');
        el.id = 'cm-output';
        el.style.whiteSpace = 'pre-wrap';
        el.style.fontFamily = 'ui-monospace, SFMono-Regular, Menlo, Consolas, monospace';
        el.style.padding = '24px';
        el.style.margin = '0';
        document.body.appendChild(el);
    }
    return el;
}

function __cm_output(text, newline) {
    const msg = String(text);
    const el = __cm_output_element();
    if (el) {
        el.textContent += msg + (newline ? '\n' : '');
        return;
    }
    if (typeof process !== 'undefined' && process.stdout) {
        if (newline) {
            console.log(msg);
        } else {
            process.stdout.write(msg);
        }
        return;
    }
    if (typeof console !== 'undefined' && console.log) {
        console.log(msg);
    }
}

function __cm_dom_root() {
    if (typeof document === 'undefined') return null;
    let root = document.getElementById('cm-root');
    if (!root) {
        root = document.createElement('div');
        root.id = 'cm-root';
        document.body.appendChild(root);
    }
    return root;
}

function __cm_web_set_html(html) {
    const root = __cm_dom_root();
    if (!root) return;
    root.innerHTML = String(html);
}

function __cm_web_append_html(html) {
    const root = __cm_dom_root();
    if (!root) return;
    root.insertAdjacentHTML('beforeend', String(html));
}

function __cm_web_set_css(css) {
    if (typeof document === 'undefined') return;
    let style = document.getElementById('cm-style');
    if (!style) {
        style = document.createElement('style');
        style.id = 'cm-style';
        document.head.appendChild(style);
    }
    style.textContent = String(css);
}

function __cm_web_get_html() {
    if (typeof document === 'undefined') return "";
    if (!document.documentElement) return "";
    return document.documentElement.outerHTML;
}

function __cm_str_concat(a, b) {
    return String(a) + String(b);
}

// Function: set_html
function set_html(html) {
    let _at_return_0 = null;
    let _t1000_2 = "";
    let _t1001_3 = null;

    _t1000_2 = html;
    _t1001_3 = __cm_web_set_html(_t1000_2);
    return _at_return_0;
}

// Function: set_css
function set_css(css) {
    let _at_return_0 = null;
    let _t1000_2 = "";
    let _t1001_3 = null;

    _t1000_2 = css;
    _t1001_3 = __cm_web_set_css(_t1000_2);
    return _at_return_0;
}

// Function: main
function main() {
    let _at_return_0 = 0;
    let css_1 = "";
    let _t1010_12 = "";
    let html_13 = "";
    let _t1011_14 = "";
    let _t1012_15 = "";
    let _t1013_16 = null;
    let _t1014_17 = "";
    let _t1015_18 = null;
    let _t1016_19 = 0;

    _t1010_12 = "\nbody{margin:0;background:linear-gradient(135deg,#f7f4ef,#d7e6f2);color:#111;}\n#cm-root{min-height:100vh;display:flex;align-items:center;justify-content:center;}\n.page{width:min(900px,92vw);background:#fff;box-shadow:0 24px 60px rgba(0,0,0,0.18);border-radius:28px;padding:40px 48px;font-family:'Georgia','Times New Roman',serif;}\n.eyebrow{letter-spacing:0.2em;text-transform:uppercase;font-size:12px;color:#556;}\nh1{margin:8px 0 12px;font-size:40px;}\n.lead{font-size:18px;line-height:1.6;color:#333;}\n.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:16px;margin-top:24px;}\n.card{background:#f4f7fb;border-radius:16px;padding:16px 18px;}\n.card-title{font-weight:bold;margin-bottom:6px;}\n.chip{display:inline-block;background:#111;color:#fff;border-radius:999px;padding:6px 12px;font-size:12px;margin-top:18px;}";
    css_1 = _t1010_12;
    _t1011_14 = "\n<div class='page'>\n    <div class='eyebrow'>Cm Web</div>\n    <h1>Graphical Output with CSS</h1>\n    <div class='lead'>Cmから生成されたJavaScriptで、HTMLとCSSを直接描画します。</div>\n    <div class='grid'>\n        <div class='card'>\n            <div class='card-title'>Layout</div>\n            <div>Grid cards</div>\n        </div>\n        <div class='card'>\n            <div class='card-title'>Style</div>\n            <div>Gradient + shadow</div>\n        </div>\n        <div class='card'>\n            <div class='card-title'>Speed</div>\n            <div>Direct DOM</div>\n        </div>\n    </div>\n    <div class='chip'>cm --target=web</div>\n</div>";
    html_13 = _t1011_14;
    _t1012_15 = css_1;
    _t1013_16 = set_css(_t1012_15);
    _t1014_17 = html_13;
    _t1015_18 = set_html(_t1014_17);
    _t1016_19 = 0;
    _at_return_0 = _t1016_19;
    return _at_return_0;
}


// Entry point
main();
