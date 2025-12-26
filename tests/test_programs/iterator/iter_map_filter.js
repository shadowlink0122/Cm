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
    let arr_1 = [Array(5).fill(0)];
    arr_1.__boxed = true;
    let _t1000_2 = Array(5).fill(0);
    let _t1001_3 = 0;
    let _t1002_4 = 0;
    let _t1003_5 = 0;
    let _t1004_6 = 0;
    let _t1005_7 = 0;
    let _t1006_8 = 0;
    let _t1007_9 = 0;
    let _t1008_10 = 0;
    let _t1009_11 = 0;
    let _t1010_12 = 0;
    let double_it_13 = null;
    let _t1011_14 = null;
    let doubled_15 = [];
    let _t1012_16 = null;
    let _t1013_17 = 0;
    let _t1014_18 = null;
    let _t1015_19 = [];
    let first_20 = 0;
    let _t1016_21 = [];
    let _t1017_22 = 0;
    let _t1018_23 = null;
    let is_even_24 = null;
    let _t1019_25 = null;
    let evens_26 = [];
    let _t1020_27 = null;
    let _t1021_28 = 0;
    let _t1022_29 = null;
    let _t1023_30 = [];
    let even_len_31 = 0;
    let _t1024_32 = 0;
    let _t1025_33 = null;
    let _t1026_34 = 0;

    _t1001_3 = 1;
    _t1002_4 = 0;
    _t1000_2[_t1002_4] = _t1001_3;
    _t1003_5 = 2;
    _t1004_6 = 1;
    _t1000_2[_t1004_6] = _t1003_5;
    _t1005_7 = 3;
    _t1006_8 = 2;
    _t1000_2[_t1006_8] = _t1005_7;
    _t1007_9 = 4;
    _t1008_10 = 3;
    _t1000_2[_t1008_10] = _t1007_9;
    _t1009_11 = 5;
    _t1010_12 = 4;
    _t1000_2[_t1010_12] = _t1009_11;
    arr_1[0] = _t1000_2;
    _t1011_14 = __lambda_0;
    double_it_13 = _t1011_14;
    _t1012_16 = arr_1;
    _t1013_17 = 5;
    _t1014_18 = double_it_13;
    _t1015_19 = __cm_unwrap(_t1012_16).map(_t1014_18);
    doubled_15 = _t1015_19;
    _t1016_21 = doubled_15;
    _t1017_22 = __cm_unwrap(_t1016_21)[0];
    first_20 = _t1017_22;
    console.log(__cm_format_string("doubled[0]: {}", [first_20]));
    _t1019_25 = __lambda_1;
    is_even_24 = _t1019_25;
    _t1020_27 = arr_1;
    _t1021_28 = 5;
    _t1022_29 = is_even_24;
    _t1023_30 = __cm_unwrap(_t1020_27).filter(_t1022_29);
    evens_26 = _t1023_30;
    _t1024_32 = __cm_unwrap(evens_26).length;
    even_len_31 = _t1024_32;
    console.log(__cm_format_string("evens.len(): {}", [even_len_31]));
    _t1026_34 = 0;
    _at_return_0 = _t1026_34;
    return _at_return_0;
}

// Function: __lambda_0
function __lambda_0(x) {
    let _at_return_0 = 0;
    let _t1000_2 = 0;
    let _t1001_3 = 0;
    let _t1002_4 = 0;

    _t1000_2 = x;
    _t1001_3 = 2;
    _t1002_4 = (_t1000_2 * _t1001_3);
    _at_return_0 = _t1002_4;
    return _at_return_0;
}

// Function: __lambda_1
function __lambda_1(x) {
    let _at_return_0 = false;
    let _t1000_2 = 0;
    let _t1001_3 = 0;
    let _t1002_4 = 0;
    let _t1003_5 = 0;
    let _t1004_6 = false;

    _t1000_2 = x;
    _t1001_3 = 2;
    _t1002_4 = (_t1000_2 % _t1001_3);
    _t1003_5 = 0;
    _t1004_6 = (_t1002_4 === _t1003_5);
    _at_return_0 = _t1004_6;
    return _at_return_0;
}


// Entry point
main();
