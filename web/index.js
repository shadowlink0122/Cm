"use strict";

// Function: BadgeStyle__css
function BadgeStyle__css(self) {
    return ["background-color: ", self["background-color"], "; ", "border-radius: ", self["border-radius"], "; ", "color: ", self["color"], "; ", "font-size: ", self["font-size"], "; ", "letter-spacing: ", self["letter-spacing"], "; ", "padding: ", self["padding"], "; ", "text-transform: ", self["text-transform"], "; "].join("");
}

// Function: HeroStyle__css
function HeroStyle__css(self) {
    return ["background: ", self["background"], "; ", "badge-tag { ", BadgeStyle__css(self["badge-tag"]), " } ", "border-radius: ", self["border-radius"], "; ", "box-shadow: ", self["box-shadow"], "; ", "color: ", self["color"], "; ", "padding: ", self["padding"], "; "].join("");
}

// Function: ButtonHover__css
function ButtonHover__css(self) {
    return ["background-color: ", self["background-color"], "; ", "transform: ", self["transform"], "; "].join("");
}

// Function: ButtonStyle__css
function ButtonStyle__css(self) {
    return ["background-color: ", self["background-color"], "; ", "border-radius: ", self["border-radius"], "; ", "color: ", self["color"], "; ", "font-weight: ", self["font-weight"], "; ", "hover { ", ButtonHover__css(self["hover"]), " } ", "letter-spacing: ", self["letter-spacing"], "; ", "padding: ", self["padding"], "; "].join("");
}

// Function: BodyStyle__css
function BodyStyle__css(self) {
    return ["background: ", self["background"], "; ", "color: ", self["color"], "; ", "font-family: ", self["font-family"], "; ", "margin: ", self["margin"], "; "].join("");
}

// Function: RootStyle__css
function RootStyle__css(self) {
    return ["align-items: ", self["align-items"], "; ", "display: ", self["display"], "; ", "justify-content: ", self["justify-content"], "; ", "min-height: ", self["min-height"], "; ", "padding: ", self["padding"], "; "].join("");
}

// Function: set_html
function set_html(html) {
    cm.web.set.html(html);
    return;
}

// Function: set_css
function set_css(css) {
    cm.web.set.css(css);
    return;
}

// Function: make_palette
function make_palette() {
    let _t1000_1 = {};

    _t1000_1.ink = "#121212";
    _t1000_1.surface = "#f8f4f0";
    _t1000_1.accent = "#ff6b4a";
    _t1000_1.accent_soft = "#ffe0d6";
    return __cm_clone(_t1000_1);
}

// Function: make_badge_style
function make_badge_style(pal) {
    let _t1000_2 = {};

    _t1000_2["background-color"] = pal.ink;
    _t1000_2["color"] = "#ffffff";
    _t1000_2["letter-spacing"] = "0.22em";
    _t1000_2["text-transform"] = "uppercase";
    _t1000_2["font-size"] = "11px";
    _t1000_2["padding"] = "6px 12px";
    _t1000_2["border-radius"] = "999px";
    return __cm_clone(_t1000_2);
}

// Function: make_hero_style
function make_hero_style(pal, badge) {
    let _t1000_3 = {};

    _t1000_3["background"] = "linear-gradient(135deg,#ffffff,#f2e9e3)";
    _t1000_3["color"] = pal.ink;
    _t1000_3["padding"] = "40px 46px";
    _t1000_3["border-radius"] = "28px";
    _t1000_3["box-shadow"] = "0 24px 60px rgba(0,0,0,0.16)";
    _t1000_3["badge-tag"] = __cm_clone(badge);
    return __cm_clone(_t1000_3);
}

// Function: make_button_style
function make_button_style(pal) {
    let _t1000_3 = {};
    let _t1003_6 = {};

    _t1000_3["background-color"] = "#ff5531";
    _t1000_3["transform"] = "translateY(-1px)";
    _t1003_6["background-color"] = pal.accent;
    _t1003_6["color"] = "#ffffff";
    _t1003_6["padding"] = "12px 18px";
    _t1003_6["border-radius"] = "999px";
    _t1003_6["letter-spacing"] = "0.08em";
    _t1003_6["font-weight"] = "600";
    _t1003_6["hover"] = __cm_clone(_t1000_3);
    return __cm_clone(_t1003_6);
}

// Function: make_body_style
function make_body_style(pal) {
    let _t1000_2 = {};

    _t1000_2["margin"] = "0";
    _t1000_2["background"] = pal.surface;
    _t1000_2["color"] = pal.ink;
    _t1000_2["font-family"] = "'Georgia','Times New Roman',serif";
    return __cm_clone(_t1000_2);
}

// Function: make_root_style
function make_root_style() {
    let _t1000_1 = {};

    _t1000_1["min-height"] = "100vh";
    _t1000_1["display"] = "flex";
    _t1000_1["align-items"] = "center";
    _t1000_1["justify-content"] = "center";
    _t1000_1["padding"] = "32px";
    return __cm_clone(_t1000_1);
}

// Function: main
function main() {
    let _t1000_2 = {};
    let _t1002_5 = {};
    let _t1003_7 = {};
    let _t1005_10 = {};
    let _t1007_13 = {};
    let _t1010_17 = {};
    let _t1012_20 = "";
    let _t1014_23 = "";
    let _t1016_26 = "";
    let _t1018_29 = "";
    let _t1019_31 = "";

    _t1000_2 = make_palette();
    _t1002_5 = make_body_style(_t1000_2);
    _t1003_7 = make_root_style();
    _t1005_10 = make_badge_style(_t1000_2);
    _t1007_13 = make_button_style(_t1000_2);
    _t1010_17 = make_hero_style(_t1000_2, _t1005_10);
    _t1012_20 = BodyStyle__css(_t1002_5);
    _t1014_23 = RootStyle__css(_t1003_7);
    _t1016_26 = HeroStyle__css(_t1010_17);
    _t1018_29 = ButtonStyle__css(_t1007_13);
    _t1019_31 = __cm_format_string("\nbody{{{}}}\n#cm-root{{{}}}\n.hero{{{}}}\n.cta{{{}}}", [_t1012_20, _t1014_23, _t1016_26, _t1018_29]);
    set_css(_t1019_31);
    set_html("<div class='hero'>\n    <badge-tag>Cm CSS</badge-tag>\n    <h1>Module-based CSS</h1>\n    <p>CSS構造体を分割して、htmlへ文字列で埋め込みます。</p>\n    <button class='cta'>Launch</button>\n</div>");
    return 0;
}

// Cm Runtime Helpers
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
    // フォーマット指定子付きプレースホルダを置換 {name:spec} or {:spec}
    result = result.replace(/\{[^}]*\}/g, (match) => {
        let inner = match.slice(1, -1);
        let spec = '';
        let colonIdx = inner.indexOf(':');
        if (colonIdx >= 0) spec = inner.slice(colonIdx + 1);
        return __cm_format(values[idx++], spec);
    });
    result = result.replace(/\}\}/g, '\x00RBRACE\x00');
    // エスケープを復元
    result = result.replace(/\x00LBRACE\x00/g, '{');
    result = result.replace(/\x00RBRACE\x00/g, '}');
    return result;
}

// Cm Web Runtime
(function() {
    if (typeof globalThis === "undefined") return;
    const root = globalThis.cm || (globalThis.cm = {});
    const web = root.web || (root.web = {});
    const set = web.set || (web.set = {});
    const append = web.append || (web.append = {});
    const get = web.get || (web.get = {});
    function ensureRoot() {
        if (typeof document === "undefined") return null;
        let el = document.getElementById("cm-root");
        if (!el) el = document.body || document.documentElement;
        return el;
    }
    function ensureStyle() {
        if (typeof document === "undefined") return null;
        let style = document.getElementById("cm-style");
        if (!style) {
            style = document.createElement("style");
            style.id = "cm-style";
            (document.head || document.documentElement).appendChild(style);
        }
        return style;
    }
    set.html = set.html || function(html) {
        const el = ensureRoot();
        if (!el) return;
        el.innerHTML = html;
    };
    append.html = append.html || function(html) {
        const el = ensureRoot();
        if (!el) return;
        el.insertAdjacentHTML("beforeend", html);
    };
    set.css = set.css || function(css) {
        const style = ensureStyle();
        if (!style) return;
        style.textContent = css;
    };
    get.html = get.html || function() {
        const el = ensureRoot();
        return el ? el.innerHTML : "";
    };
})();


// Entry point
main();
