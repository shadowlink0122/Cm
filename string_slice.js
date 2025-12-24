"use strict";

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

// Function: println
function println(s) {
    let _at_return_0 = null;
    let _t1000_2 = "";
    let _t1001_3 = null;

    _t1000_2 = s;
    console.log(_t1000_2);
    return _at_return_0;
}

// Function: main
function main() {
    let _at_return_0 = 0;
    let s_1 = "";
    let _t1000_2 = "";
    let sub1_3 = "";
    let _t1001_4 = "";
    let _t1002_5 = 0;
    let _t1003_6 = 0;
    let _t1004_7 = "";
    let _t1005_8 = null;
    let sub2_9 = "";
    let _t1006_10 = "";
    let _t1007_11 = 0;
    let _t1008_12 = 0;
    let _t1009_13 = "";
    let _t1010_14 = null;
    let sub3_15 = "";
    let _t1011_16 = "";
    let _t1012_17 = 0;
    let _t1013_18 = 0;
    let _t1014_19 = "";
    let _t1015_20 = null;
    let sub4_21 = "";
    let _t1016_22 = "";
    let _t1017_23 = 0;
    let _t1018_24 = 0;
    let _t1019_25 = "";
    let _t1020_26 = null;
    let sub5_27 = "";
    let _t1021_28 = "";
    let _t1022_29 = 0;
    let _t1023_30 = 0;
    let _t1024_31 = 0;
    let _t1025_32 = 0;
    let _t1026_33 = "";
    let _t1027_34 = null;
    let sub6_35 = "";
    let _t1028_36 = "";
    let _t1029_37 = 0;
    let _t1030_38 = 0;
    let _t1031_39 = "";
    let _t1032_40 = null;
    let _t1033_41 = 0;

    _t1000_2 = "Hello, World!";
    s_1 = _t1000_2;
    _t1001_4 = s_1;
    _t1002_5 = 0;
    _t1003_6 = 5;
    _t1004_7 = __cm_str_slice(_t1001_4, _t1002_5, _t1003_6);
    sub1_3 = _t1004_7;
    console.log(__cm_format_string("s[0:5] = {}", [sub1_3]));
    _t1006_10 = s_1;
    _t1007_11 = 7;
    _t1008_12 = 12;
    _t1009_13 = __cm_str_slice(_t1006_10, _t1007_11, _t1008_12);
    sub2_9 = _t1009_13;
    console.log(__cm_format_string("s[7:12] = {}", [sub2_9]));
    _t1011_16 = s_1;
    _t1012_17 = 0;
    _t1013_18 = 5;
    _t1014_19 = __cm_str_slice(_t1011_16, _t1012_17, _t1013_18);
    sub3_15 = _t1014_19;
    console.log(__cm_format_string("s[:5] = {}", [sub3_15]));
    _t1016_22 = s_1;
    _t1017_23 = 7;
    _t1018_24 = -1;
    _t1019_25 = __cm_str_slice(_t1016_22, _t1017_23, _t1018_24);
    sub4_21 = _t1019_25;
    console.log(__cm_format_string("s[7:] = {}", [sub4_21]));
    _t1021_28 = s_1;
    _t1022_29 = 6;
    _t1023_30 = -_t1022_29;
    _t1024_31 = 1;
    _t1025_32 = -_t1024_31;
    _t1026_33 = __cm_str_slice(_t1021_28, _t1023_30, _t1025_32);
    sub5_27 = _t1026_33;
    console.log(__cm_format_string("s[-6:-1] = {}", [sub5_27]));
    _t1028_36 = s_1;
    _t1029_37 = 0;
    _t1030_38 = -1;
    _t1031_39 = __cm_str_slice(_t1028_36, _t1029_37, _t1030_38);
    sub6_35 = _t1031_39;
    console.log(__cm_format_string("s[:] = {}", [sub6_35]));
    _t1033_41 = 0;
    _at_return_0 = _t1033_41;
    return _at_return_0;
}


// Entry point
main();
