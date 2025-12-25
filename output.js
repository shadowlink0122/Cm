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

function __cm_str_concat(a, b) {
    return String(a) + String(b);
}

// Struct: Palette
function Palette() {
    return {
        ink: "",
        surface: "",
        accent: "",
        accent_soft: ""
    };
}

// Struct: BadgeStyle
function BadgeStyle() {
    return {
        "background-color": "",
        "color": "",
        "letter-spacing": "",
        "text-transform": "",
        "font-size": "",
        "padding": "",
        "border-radius": ""
    };
}

// Struct: HeroStyle
function HeroStyle() {
    return {
        "background": "",
        "color": "",
        "padding": "",
        "border-radius": "",
        "box-shadow": "",
        "badge-tag": BadgeStyle()
    };
}

// Struct: ButtonHover
function ButtonHover() {
    return {
        "background-color": "",
        "transform": ""
    };
}

// Struct: ButtonStyle
function ButtonStyle() {
    return {
        "background-color": "",
        "color": "",
        "padding": "",
        "border-radius": "",
        "letter-spacing": "",
        "font-weight": "",
        "hover": ButtonHover()
    };
}

// Struct: BodyStyle
function BodyStyle() {
    return {
        "margin": "",
        "background": "",
        "color": "",
        "font-family": ""
    };
}

// Struct: RootStyle
function RootStyle() {
    return {
        "min-height": "",
        "display": "",
        "align-items": "",
        "justify-content": "",
        "padding": ""
    };
}

// Function: BadgeStyle__css
function BadgeStyle__css(self) {
    let _0_0 = "";
    let _result_2 = "";
    let _field0_3 = "";
    let _css_lit0_4 = "";
    let _css_concat0_5 = "";
    let _fstr0_6 = "";
    let _css_val1_7 = "";
    let _css_lit2_8 = "";
    let _css_concat2_9 = "";
    let _field1_10 = "";
    let _css_lit3_11 = "";
    let _css_concat3_12 = "";
    let _fstr1_13 = "";
    let _css_val4_14 = "";
    let _css_lit5_15 = "";
    let _css_concat5_16 = "";
    let _field2_17 = "";
    let _css_lit6_18 = "";
    let _css_concat6_19 = "";
    let _fstr2_20 = "";
    let _css_val7_21 = "";
    let _css_lit8_22 = "";
    let _css_concat8_23 = "";
    let _field3_24 = "";
    let _css_lit9_25 = "";
    let _css_concat9_26 = "";
    let _fstr3_27 = "";
    let _css_val10_28 = "";
    let _css_lit11_29 = "";
    let _css_concat11_30 = "";
    let _field4_31 = "";
    let _css_lit12_32 = "";
    let _css_concat12_33 = "";
    let _fstr4_34 = "";
    let _css_val13_35 = "";
    let _css_lit14_36 = "";
    let _css_concat14_37 = "";
    let _field5_38 = "";
    let _css_lit15_39 = "";
    let _css_concat15_40 = "";
    let _fstr5_41 = "";
    let _css_val16_42 = "";
    let _css_lit17_43 = "";
    let _css_concat17_44 = "";
    let _field6_45 = "";
    let _css_lit18_46 = "";
    let _css_concat18_47 = "";
    let _fstr6_48 = "";
    let _css_val19_49 = "";
    let _css_lit20_50 = "";
    let _css_concat20_51 = "";

    _result_2 = "";
    _field0_3 = self["background-color"];
    _css_lit0_4 = "background-color: ";
    _css_concat0_5 = (_result_2 + _css_lit0_4);
    _fstr0_6 = _field0_3;
    _css_val1_7 = (_css_concat0_5 + _fstr0_6);
    _css_lit2_8 = "; ";
    _css_concat2_9 = (_css_val1_7 + _css_lit2_8);
    _field1_10 = self["border-radius"];
    _css_lit3_11 = "border-radius: ";
    _css_concat3_12 = (_css_concat2_9 + _css_lit3_11);
    _fstr1_13 = _field1_10;
    _css_val4_14 = (_css_concat3_12 + _fstr1_13);
    _css_lit5_15 = "; ";
    _css_concat5_16 = (_css_val4_14 + _css_lit5_15);
    _field2_17 = self["color"];
    _css_lit6_18 = "color: ";
    _css_concat6_19 = (_css_concat5_16 + _css_lit6_18);
    _fstr2_20 = _field2_17;
    _css_val7_21 = (_css_concat6_19 + _fstr2_20);
    _css_lit8_22 = "; ";
    _css_concat8_23 = (_css_val7_21 + _css_lit8_22);
    _field3_24 = self["font-size"];
    _css_lit9_25 = "font-size: ";
    _css_concat9_26 = (_css_concat8_23 + _css_lit9_25);
    _fstr3_27 = _field3_24;
    _css_val10_28 = (_css_concat9_26 + _fstr3_27);
    _css_lit11_29 = "; ";
    _css_concat11_30 = (_css_val10_28 + _css_lit11_29);
    _field4_31 = self["letter-spacing"];
    _css_lit12_32 = "letter-spacing: ";
    _css_concat12_33 = (_css_concat11_30 + _css_lit12_32);
    _fstr4_34 = _field4_31;
    _css_val13_35 = (_css_concat12_33 + _fstr4_34);
    _css_lit14_36 = "; ";
    _css_concat14_37 = (_css_val13_35 + _css_lit14_36);
    _field5_38 = self["padding"];
    _css_lit15_39 = "padding: ";
    _css_concat15_40 = (_css_concat14_37 + _css_lit15_39);
    _fstr5_41 = _field5_38;
    _css_val16_42 = (_css_concat15_40 + _fstr5_41);
    _css_lit17_43 = "; ";
    _css_concat17_44 = (_css_val16_42 + _css_lit17_43);
    _field6_45 = self["text-transform"];
    _css_lit18_46 = "text-transform: ";
    _css_concat18_47 = (_css_concat17_44 + _css_lit18_46);
    _fstr6_48 = _field6_45;
    _css_val19_49 = (_css_concat18_47 + _fstr6_48);
    _css_lit20_50 = "; ";
    _css_concat20_51 = (_css_val19_49 + _css_lit20_50);
    _0_0 = _css_concat20_51;
    return _0_0;
}

// Function: HeroStyle__css
function HeroStyle__css(self) {
    let _0_0 = "";
    let _result_2 = "";
    let _field0_3 = "";
    let _css_lit0_4 = "";
    let _css_concat0_5 = "";
    let _fstr0_6 = "";
    let _css_val1_7 = "";
    let _css_lit2_8 = "";
    let _css_concat2_9 = "";
    let _field1_10 = BadgeStyle();
    let _css_lit3_11 = "";
    let _css_concat3_12 = "";
    let _fstr1_13 = "";
    let _css_val4_14 = "";
    let _css_lit5_15 = "";
    let _css_concat5_16 = "";
    let _field2_17 = "";
    let _css_lit6_18 = "";
    let _css_concat6_19 = "";
    let _fstr2_20 = "";
    let _css_val7_21 = "";
    let _css_lit8_22 = "";
    let _css_concat8_23 = "";
    let _field3_24 = "";
    let _css_lit9_25 = "";
    let _css_concat9_26 = "";
    let _fstr3_27 = "";
    let _css_val10_28 = "";
    let _css_lit11_29 = "";
    let _css_concat11_30 = "";
    let _field4_31 = "";
    let _css_lit12_32 = "";
    let _css_concat12_33 = "";
    let _fstr4_34 = "";
    let _css_val13_35 = "";
    let _css_lit14_36 = "";
    let _css_concat14_37 = "";
    let _field5_38 = "";
    let _css_lit15_39 = "";
    let _css_concat15_40 = "";
    let _fstr5_41 = "";
    let _css_val16_42 = "";
    let _css_lit17_43 = "";
    let _css_concat17_44 = "";

    _result_2 = "";
    _field0_3 = self["background"];
    _css_lit0_4 = "background: ";
    _css_concat0_5 = (_result_2 + _css_lit0_4);
    _fstr0_6 = _field0_3;
    _css_val1_7 = (_css_concat0_5 + _fstr0_6);
    _css_lit2_8 = "; ";
    _css_concat2_9 = (_css_val1_7 + _css_lit2_8);
    _field1_10 = self["badge-tag"];
    _css_lit3_11 = "badge-tag { ";
    _css_concat3_12 = (_css_concat2_9 + _css_lit3_11);
    _fstr1_13 = BadgeStyle__css(_field1_10);
    _css_val4_14 = (_css_concat3_12 + _fstr1_13);
    _css_lit5_15 = " } ";
    _css_concat5_16 = (_css_val4_14 + _css_lit5_15);
    _field2_17 = self["border-radius"];
    _css_lit6_18 = "border-radius: ";
    _css_concat6_19 = (_css_concat5_16 + _css_lit6_18);
    _fstr2_20 = _field2_17;
    _css_val7_21 = (_css_concat6_19 + _fstr2_20);
    _css_lit8_22 = "; ";
    _css_concat8_23 = (_css_val7_21 + _css_lit8_22);
    _field3_24 = self["box-shadow"];
    _css_lit9_25 = "box-shadow: ";
    _css_concat9_26 = (_css_concat8_23 + _css_lit9_25);
    _fstr3_27 = _field3_24;
    _css_val10_28 = (_css_concat9_26 + _fstr3_27);
    _css_lit11_29 = "; ";
    _css_concat11_30 = (_css_val10_28 + _css_lit11_29);
    _field4_31 = self["color"];
    _css_lit12_32 = "color: ";
    _css_concat12_33 = (_css_concat11_30 + _css_lit12_32);
    _fstr4_34 = _field4_31;
    _css_val13_35 = (_css_concat12_33 + _fstr4_34);
    _css_lit14_36 = "; ";
    _css_concat14_37 = (_css_val13_35 + _css_lit14_36);
    _field5_38 = self["padding"];
    _css_lit15_39 = "padding: ";
    _css_concat15_40 = (_css_concat14_37 + _css_lit15_39);
    _fstr5_41 = _field5_38;
    _css_val16_42 = (_css_concat15_40 + _fstr5_41);
    _css_lit17_43 = "; ";
    _css_concat17_44 = (_css_val16_42 + _css_lit17_43);
    _0_0 = _css_concat17_44;
    return _0_0;
}

// Function: ButtonHover__css
function ButtonHover__css(self) {
    let _0_0 = "";
    let _result_2 = "";
    let _field0_3 = "";
    let _css_lit0_4 = "";
    let _css_concat0_5 = "";
    let _fstr0_6 = "";
    let _css_val1_7 = "";
    let _css_lit2_8 = "";
    let _css_concat2_9 = "";
    let _field1_10 = "";
    let _css_lit3_11 = "";
    let _css_concat3_12 = "";
    let _fstr1_13 = "";
    let _css_val4_14 = "";
    let _css_lit5_15 = "";
    let _css_concat5_16 = "";

    _result_2 = "";
    _field0_3 = self["background-color"];
    _css_lit0_4 = "background-color: ";
    _css_concat0_5 = (_result_2 + _css_lit0_4);
    _fstr0_6 = _field0_3;
    _css_val1_7 = (_css_concat0_5 + _fstr0_6);
    _css_lit2_8 = "; ";
    _css_concat2_9 = (_css_val1_7 + _css_lit2_8);
    _field1_10 = self["transform"];
    _css_lit3_11 = "transform: ";
    _css_concat3_12 = (_css_concat2_9 + _css_lit3_11);
    _fstr1_13 = _field1_10;
    _css_val4_14 = (_css_concat3_12 + _fstr1_13);
    _css_lit5_15 = "; ";
    _css_concat5_16 = (_css_val4_14 + _css_lit5_15);
    _0_0 = _css_concat5_16;
    return _0_0;
}

// Function: ButtonStyle__css
function ButtonStyle__css(self) {
    let _0_0 = "";
    let _result_2 = "";
    let _field0_3 = "";
    let _css_lit0_4 = "";
    let _css_concat0_5 = "";
    let _fstr0_6 = "";
    let _css_val1_7 = "";
    let _css_lit2_8 = "";
    let _css_concat2_9 = "";
    let _field1_10 = "";
    let _css_lit3_11 = "";
    let _css_concat3_12 = "";
    let _fstr1_13 = "";
    let _css_val4_14 = "";
    let _css_lit5_15 = "";
    let _css_concat5_16 = "";
    let _field2_17 = "";
    let _css_lit6_18 = "";
    let _css_concat6_19 = "";
    let _fstr2_20 = "";
    let _css_val7_21 = "";
    let _css_lit8_22 = "";
    let _css_concat8_23 = "";
    let _field3_24 = "";
    let _css_lit9_25 = "";
    let _css_concat9_26 = "";
    let _fstr3_27 = "";
    let _css_val10_28 = "";
    let _css_lit11_29 = "";
    let _css_concat11_30 = "";
    let _field4_31 = ButtonHover();
    let _css_lit12_32 = "";
    let _css_concat12_33 = "";
    let _fstr4_34 = "";
    let _css_val13_35 = "";
    let _css_lit14_36 = "";
    let _css_concat14_37 = "";
    let _field5_38 = "";
    let _css_lit15_39 = "";
    let _css_concat15_40 = "";
    let _fstr5_41 = "";
    let _css_val16_42 = "";
    let _css_lit17_43 = "";
    let _css_concat17_44 = "";
    let _field6_45 = "";
    let _css_lit18_46 = "";
    let _css_concat18_47 = "";
    let _fstr6_48 = "";
    let _css_val19_49 = "";
    let _css_lit20_50 = "";
    let _css_concat20_51 = "";

    _result_2 = "";
    _field0_3 = self["background-color"];
    _css_lit0_4 = "background-color: ";
    _css_concat0_5 = (_result_2 + _css_lit0_4);
    _fstr0_6 = _field0_3;
    _css_val1_7 = (_css_concat0_5 + _fstr0_6);
    _css_lit2_8 = "; ";
    _css_concat2_9 = (_css_val1_7 + _css_lit2_8);
    _field1_10 = self["border-radius"];
    _css_lit3_11 = "border-radius: ";
    _css_concat3_12 = (_css_concat2_9 + _css_lit3_11);
    _fstr1_13 = _field1_10;
    _css_val4_14 = (_css_concat3_12 + _fstr1_13);
    _css_lit5_15 = "; ";
    _css_concat5_16 = (_css_val4_14 + _css_lit5_15);
    _field2_17 = self["color"];
    _css_lit6_18 = "color: ";
    _css_concat6_19 = (_css_concat5_16 + _css_lit6_18);
    _fstr2_20 = _field2_17;
    _css_val7_21 = (_css_concat6_19 + _fstr2_20);
    _css_lit8_22 = "; ";
    _css_concat8_23 = (_css_val7_21 + _css_lit8_22);
    _field3_24 = self["font-weight"];
    _css_lit9_25 = "font-weight: ";
    _css_concat9_26 = (_css_concat8_23 + _css_lit9_25);
    _fstr3_27 = _field3_24;
    _css_val10_28 = (_css_concat9_26 + _fstr3_27);
    _css_lit11_29 = "; ";
    _css_concat11_30 = (_css_val10_28 + _css_lit11_29);
    _field4_31 = self["hover"];
    _css_lit12_32 = "hover { ";
    _css_concat12_33 = (_css_concat11_30 + _css_lit12_32);
    _fstr4_34 = ButtonHover__css(_field4_31);
    _css_val13_35 = (_css_concat12_33 + _fstr4_34);
    _css_lit14_36 = " } ";
    _css_concat14_37 = (_css_val13_35 + _css_lit14_36);
    _field5_38 = self["letter-spacing"];
    _css_lit15_39 = "letter-spacing: ";
    _css_concat15_40 = (_css_concat14_37 + _css_lit15_39);
    _fstr5_41 = _field5_38;
    _css_val16_42 = (_css_concat15_40 + _fstr5_41);
    _css_lit17_43 = "; ";
    _css_concat17_44 = (_css_val16_42 + _css_lit17_43);
    _field6_45 = self["padding"];
    _css_lit18_46 = "padding: ";
    _css_concat18_47 = (_css_concat17_44 + _css_lit18_46);
    _fstr6_48 = _field6_45;
    _css_val19_49 = (_css_concat18_47 + _fstr6_48);
    _css_lit20_50 = "; ";
    _css_concat20_51 = (_css_val19_49 + _css_lit20_50);
    _0_0 = _css_concat20_51;
    return _0_0;
}

// Function: BodyStyle__css
function BodyStyle__css(self) {
    let _0_0 = "";
    let _result_2 = "";
    let _field0_3 = "";
    let _css_lit0_4 = "";
    let _css_concat0_5 = "";
    let _fstr0_6 = "";
    let _css_val1_7 = "";
    let _css_lit2_8 = "";
    let _css_concat2_9 = "";
    let _field1_10 = "";
    let _css_lit3_11 = "";
    let _css_concat3_12 = "";
    let _fstr1_13 = "";
    let _css_val4_14 = "";
    let _css_lit5_15 = "";
    let _css_concat5_16 = "";
    let _field2_17 = "";
    let _css_lit6_18 = "";
    let _css_concat6_19 = "";
    let _fstr2_20 = "";
    let _css_val7_21 = "";
    let _css_lit8_22 = "";
    let _css_concat8_23 = "";
    let _field3_24 = "";
    let _css_lit9_25 = "";
    let _css_concat9_26 = "";
    let _fstr3_27 = "";
    let _css_val10_28 = "";
    let _css_lit11_29 = "";
    let _css_concat11_30 = "";

    _result_2 = "";
    _field0_3 = self["background"];
    _css_lit0_4 = "background: ";
    _css_concat0_5 = (_result_2 + _css_lit0_4);
    _fstr0_6 = _field0_3;
    _css_val1_7 = (_css_concat0_5 + _fstr0_6);
    _css_lit2_8 = "; ";
    _css_concat2_9 = (_css_val1_7 + _css_lit2_8);
    _field1_10 = self["color"];
    _css_lit3_11 = "color: ";
    _css_concat3_12 = (_css_concat2_9 + _css_lit3_11);
    _fstr1_13 = _field1_10;
    _css_val4_14 = (_css_concat3_12 + _fstr1_13);
    _css_lit5_15 = "; ";
    _css_concat5_16 = (_css_val4_14 + _css_lit5_15);
    _field2_17 = self["font-family"];
    _css_lit6_18 = "font-family: ";
    _css_concat6_19 = (_css_concat5_16 + _css_lit6_18);
    _fstr2_20 = _field2_17;
    _css_val7_21 = (_css_concat6_19 + _fstr2_20);
    _css_lit8_22 = "; ";
    _css_concat8_23 = (_css_val7_21 + _css_lit8_22);
    _field3_24 = self["margin"];
    _css_lit9_25 = "margin: ";
    _css_concat9_26 = (_css_concat8_23 + _css_lit9_25);
    _fstr3_27 = _field3_24;
    _css_val10_28 = (_css_concat9_26 + _fstr3_27);
    _css_lit11_29 = "; ";
    _css_concat11_30 = (_css_val10_28 + _css_lit11_29);
    _0_0 = _css_concat11_30;
    return _0_0;
}

// Function: RootStyle__css
function RootStyle__css(self) {
    let _0_0 = "";
    let _result_2 = "";
    let _field0_3 = "";
    let _css_lit0_4 = "";
    let _css_concat0_5 = "";
    let _fstr0_6 = "";
    let _css_val1_7 = "";
    let _css_lit2_8 = "";
    let _css_concat2_9 = "";
    let _field1_10 = "";
    let _css_lit3_11 = "";
    let _css_concat3_12 = "";
    let _fstr1_13 = "";
    let _css_val4_14 = "";
    let _css_lit5_15 = "";
    let _css_concat5_16 = "";
    let _field2_17 = "";
    let _css_lit6_18 = "";
    let _css_concat6_19 = "";
    let _fstr2_20 = "";
    let _css_val7_21 = "";
    let _css_lit8_22 = "";
    let _css_concat8_23 = "";
    let _field3_24 = "";
    let _css_lit9_25 = "";
    let _css_concat9_26 = "";
    let _fstr3_27 = "";
    let _css_val10_28 = "";
    let _css_lit11_29 = "";
    let _css_concat11_30 = "";
    let _field4_31 = "";
    let _css_lit12_32 = "";
    let _css_concat12_33 = "";
    let _fstr4_34 = "";
    let _css_val13_35 = "";
    let _css_lit14_36 = "";
    let _css_concat14_37 = "";

    _result_2 = "";
    _field0_3 = self["align-items"];
    _css_lit0_4 = "align-items: ";
    _css_concat0_5 = (_result_2 + _css_lit0_4);
    _fstr0_6 = _field0_3;
    _css_val1_7 = (_css_concat0_5 + _fstr0_6);
    _css_lit2_8 = "; ";
    _css_concat2_9 = (_css_val1_7 + _css_lit2_8);
    _field1_10 = self["display"];
    _css_lit3_11 = "display: ";
    _css_concat3_12 = (_css_concat2_9 + _css_lit3_11);
    _fstr1_13 = _field1_10;
    _css_val4_14 = (_css_concat3_12 + _fstr1_13);
    _css_lit5_15 = "; ";
    _css_concat5_16 = (_css_val4_14 + _css_lit5_15);
    _field2_17 = self["justify-content"];
    _css_lit6_18 = "justify-content: ";
    _css_concat6_19 = (_css_concat5_16 + _css_lit6_18);
    _fstr2_20 = _field2_17;
    _css_val7_21 = (_css_concat6_19 + _fstr2_20);
    _css_lit8_22 = "; ";
    _css_concat8_23 = (_css_val7_21 + _css_lit8_22);
    _field3_24 = self["min-height"];
    _css_lit9_25 = "min-height: ";
    _css_concat9_26 = (_css_concat8_23 + _css_lit9_25);
    _fstr3_27 = _field3_24;
    _css_val10_28 = (_css_concat9_26 + _fstr3_27);
    _css_lit11_29 = "; ";
    _css_concat11_30 = (_css_val10_28 + _css_lit11_29);
    _field4_31 = self["padding"];
    _css_lit12_32 = "padding: ";
    _css_concat12_33 = (_css_concat11_30 + _css_lit12_32);
    _fstr4_34 = _field4_31;
    _css_val13_35 = (_css_concat12_33 + _fstr4_34);
    _css_lit14_36 = "; ";
    _css_concat14_37 = (_css_val13_35 + _css_lit14_36);
    _0_0 = _css_concat14_37;
    return _0_0;
}

// Function: set_html
function set_html(html) {
    let _at_return_0 = null;
    let _t1000_2 = "";
    let _t1001_3 = null;

    _t1000_2 = html;
    _t1001_3 = cm.web.set.html(_t1000_2);
    return _at_return_0;
}

// Function: set_css
function set_css(css) {
    let _at_return_0 = null;
    let _t1000_2 = "";
    let _t1001_3 = null;

    _t1000_2 = css;
    _t1001_3 = cm.web.set.css(_t1000_2);
    return _at_return_0;
}

// Function: make_palette
function make_palette() {
    let _at_return_0 = Palette();
    let _t1000_1 = ();
    let _t1001_2 = "";
    let _t1002_3 = "";
    let _t1003_4 = "";
    let _t1004_5 = "";

    _t1001_2 = "#121212";
    _t1000_1.field0 = _t1001_2;
    _t1002_3 = "#f8f4f0";
    _t1000_1.field0 = _t1002_3;
    _t1003_4 = "#ff6b4a";
    _t1000_1.field0 = _t1003_4;
    _t1004_5 = "#ffe0d6";
    _t1000_1.field0 = _t1004_5;
    _at_return_0 = __cm_clone(_t1000_1);
    return _at_return_0;
}

// Function: make_badge_style
function make_badge_style(pal) {
    let _at_return_0 = BadgeStyle();
    let _t1000_2 = ();
    let _t1001_3 = Palette();
    let _t1002_4 = "";
    let _t1003_5 = "";
    let _t1004_6 = "";
    let _t1005_7 = "";
    let _t1006_8 = "";
    let _t1007_9 = "";
    let _t1008_10 = "";

    _t1001_3 = __cm_clone(pal);
    _t1002_4 = _t1001_3.ink;
    _t1000_2.field0 = _t1002_4;
    _t1003_5 = "#ffffff";
    _t1000_2.field0 = _t1003_5;
    _t1004_6 = "0.22em";
    _t1000_2.field0 = _t1004_6;
    _t1005_7 = "uppercase";
    _t1000_2.field0 = _t1005_7;
    _t1006_8 = "11px";
    _t1000_2.field0 = _t1006_8;
    _t1007_9 = "6px 12px";
    _t1000_2.field0 = _t1007_9;
    _t1008_10 = "999px";
    _t1000_2.field0 = _t1008_10;
    _at_return_0 = __cm_clone(_t1000_2);
    return _at_return_0;
}

// Function: make_hero_style
function make_hero_style(pal, badge) {
    let _at_return_0 = HeroStyle();
    let _t1000_3 = ();
    let _t1001_4 = "";
    let _t1002_5 = Palette();
    let _t1003_6 = "";
    let _t1004_7 = "";
    let _t1005_8 = "";
    let _t1006_9 = "";
    let _t1007_10 = BadgeStyle();

    _t1001_4 = "linear-gradient(135deg,#ffffff,#f2e9e3)";
    _t1000_3.field0 = _t1001_4;
    _t1002_5 = __cm_clone(pal);
    _t1003_6 = _t1002_5.ink;
    _t1000_3.field0 = _t1003_6;
    _t1004_7 = "40px 46px";
    _t1000_3.field0 = _t1004_7;
    _t1005_8 = "28px";
    _t1000_3.field0 = _t1005_8;
    _t1006_9 = "0 24px 60px rgba(0,0,0,0.16)";
    _t1000_3.field0 = _t1006_9;
    _t1007_10 = __cm_clone(badge);
    _t1000_3.field0 = __cm_clone(_t1007_10);
    _at_return_0 = __cm_clone(_t1000_3);
    return _at_return_0;
}

// Function: make_button_style
function make_button_style(pal) {
    let _at_return_0 = ButtonStyle();
    let hover_2 = ButtonHover();
    let _t1000_3 = ButtonHover();
    let _t1001_4 = "";
    let _t1002_5 = "";
    let _t1003_6 = ();
    let _t1004_7 = Palette();
    let _t1005_8 = "";
    let _t1006_9 = "";
    let _t1007_10 = "";
    let _t1008_11 = "";
    let _t1009_12 = "";
    let _t1010_13 = "";
    let _t1011_14 = ButtonHover();

    _t1001_4 = "#ff5531";
    _t1000_3["background-color"] = _t1001_4;
    _t1002_5 = "translateY(-1px)";
    _t1000_3["transform"] = _t1002_5;
    hover_2 = __cm_clone(_t1000_3);
    _t1004_7 = __cm_clone(pal);
    _t1005_8 = _t1004_7.accent;
    _t1003_6.field0 = _t1005_8;
    _t1006_9 = "#ffffff";
    _t1003_6.field0 = _t1006_9;
    _t1007_10 = "12px 18px";
    _t1003_6.field0 = _t1007_10;
    _t1008_11 = "999px";
    _t1003_6.field0 = _t1008_11;
    _t1009_12 = "0.08em";
    _t1003_6.field0 = _t1009_12;
    _t1010_13 = "600";
    _t1003_6.field0 = _t1010_13;
    _t1011_14 = __cm_clone(hover_2);
    _t1003_6.field0 = __cm_clone(_t1011_14);
    _at_return_0 = __cm_clone(_t1003_6);
    return _at_return_0;
}

// Function: make_body_style
function make_body_style(pal) {
    let _at_return_0 = BodyStyle();
    let _t1000_2 = ();
    let _t1001_3 = "";
    let _t1002_4 = Palette();
    let _t1003_5 = "";
    let _t1004_6 = Palette();
    let _t1005_7 = "";
    let _t1006_8 = "";

    _t1001_3 = "0";
    _t1000_2.field0 = _t1001_3;
    _t1002_4 = __cm_clone(pal);
    _t1003_5 = _t1002_4.surface;
    _t1000_2.field0 = _t1003_5;
    _t1004_6 = __cm_clone(pal);
    _t1005_7 = _t1004_6.ink;
    _t1000_2.field0 = _t1005_7;
    _t1006_8 = "'Georgia','Times New Roman',serif";
    _t1000_2.field0 = _t1006_8;
    _at_return_0 = __cm_clone(_t1000_2);
    return _at_return_0;
}

// Function: make_root_style
function make_root_style() {
    let _at_return_0 = RootStyle();
    let _t1000_1 = ();
    let _t1001_2 = "";
    let _t1002_3 = "";
    let _t1003_4 = "";
    let _t1004_5 = "";
    let _t1005_6 = "";

    _t1001_2 = "100vh";
    _t1000_1.field0 = _t1001_2;
    _t1002_3 = "flex";
    _t1000_1.field0 = _t1002_3;
    _t1003_4 = "center";
    _t1000_1.field0 = _t1003_4;
    _t1004_5 = "center";
    _t1000_1.field0 = _t1004_5;
    _t1005_6 = "32px";
    _t1000_1.field0 = _t1005_6;
    _at_return_0 = __cm_clone(_t1000_1);
    return _at_return_0;
}

// Function: main
function main() {
    let _at_return_0 = 0;
    let pal_1 = Palette();
    let _t1000_2 = Palette();
    let body_3 = BodyStyle();
    let _t1001_4 = Palette();
    let _t1002_5 = BodyStyle();
    let root_6 = RootStyle();
    let _t1003_7 = RootStyle();
    let badge_8 = BadgeStyle();
    let _t1004_9 = Palette();
    let _t1005_10 = BadgeStyle();
    let button_11 = ButtonStyle();
    let _t1006_12 = Palette();
    let _t1007_13 = ButtonStyle();
    let hero_14 = HeroStyle();
    let _t1008_15 = Palette();
    let _t1009_16 = BadgeStyle();
    let _t1010_17 = HeroStyle();
    let body_css_18 = "";
    let _t1011_19 = BodyStyle();
    let _t1012_20 = "";
    let root_css_21 = "";
    let _t1013_22 = RootStyle();
    let _t1014_23 = "";
    let hero_css_24 = "";
    let _t1015_25 = HeroStyle();
    let _t1016_26 = "";
    let button_css_27 = "";
    let _t1017_28 = ButtonStyle();
    let _t1018_29 = "";
    let css_30 = "";
    let _t1019_31 = "";
    let html_32 = "";
    let _t1020_33 = "";
    let _t1021_34 = "";
    let _t1022_35 = null;
    let _t1023_36 = "";
    let _t1024_37 = null;
    let _t1025_38 = 0;

    _t1000_2 = make_palette();
    pal_1 = __cm_clone(_t1000_2);
    _t1001_4 = __cm_clone(pal_1);
    _t1002_5 = make_body_style(_t1001_4);
    body_3 = __cm_clone(_t1002_5);
    _t1003_7 = make_root_style();
    root_6 = __cm_clone(_t1003_7);
    _t1004_9 = __cm_clone(pal_1);
    _t1005_10 = make_badge_style(_t1004_9);
    badge_8 = __cm_clone(_t1005_10);
    _t1006_12 = __cm_clone(pal_1);
    _t1007_13 = make_button_style(_t1006_12);
    button_11 = __cm_clone(_t1007_13);
    _t1008_15 = __cm_clone(pal_1);
    _t1009_16 = __cm_clone(badge_8);
    _t1010_17 = make_hero_style(_t1008_15, _t1009_16);
    hero_14 = __cm_clone(_t1010_17);
    _t1011_19 = __cm_clone(body_3);
    _t1012_20 = BodyStyle__css(_t1011_19);
    body_css_18 = _t1012_20;
    _t1013_22 = __cm_clone(root_6);
    _t1014_23 = RootStyle__css(_t1013_22);
    root_css_21 = _t1014_23;
    _t1015_25 = __cm_clone(hero_14);
    _t1016_26 = HeroStyle__css(_t1015_25);
    hero_css_24 = _t1016_26;
    _t1017_28 = __cm_clone(button_11);
    _t1018_29 = ButtonStyle__css(_t1017_28);
    button_css_27 = _t1018_29;
    _t1019_31 = __cm_format_string("\nbody{{body_css}}\n#cm-root{{root_css}}\n.hero{{hero_css}}\n.cta{{button_css}}", []);
    css_30 = _t1019_31;
    _t1020_33 = "<div class='hero'>\n    <badge-tag>Cm CSS</badge-tag>\n    <h1>Module-based CSS</h1>\n    <p>CSS構造体を分割して、htmlへ文字列で埋め込みます。</p>\n    <button class='cta'>Launch</button>\n</div>";
    html_32 = _t1020_33;
    _t1021_34 = css_30;
    _t1022_35 = set_css(_t1021_34);
    _t1023_36 = html_32;
    _t1024_37 = set_html(_t1023_36);
    _t1025_38 = 0;
    _at_return_0 = _t1025_38;
    return _at_return_0;
}


// Entry point
main();
