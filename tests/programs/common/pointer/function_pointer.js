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

// Function: add
function add(a, b) {
    let _at_return_0 = 0;
    let _t1000_3 = 0;
    let _t1001_4 = 0;
    let _t1002_5 = 0;

    _t1000_3 = a;
    _t1001_4 = b;
    _t1002_5 = (_t1000_3 + _t1001_4);
    _at_return_0 = _t1002_5;
    return _at_return_0;
}

// Function: sub
function sub(a, b) {
    let _at_return_0 = 0;
    let _t1000_3 = 0;
    let _t1001_4 = 0;
    let _t1002_5 = 0;

    _t1000_3 = a;
    _t1001_4 = b;
    _t1002_5 = (_t1000_3 - _t1001_4);
    _at_return_0 = _t1002_5;
    return _at_return_0;
}

// Function: mul
function mul(a, b) {
    let _at_return_0 = 0;
    let _t1000_3 = 0;
    let _t1001_4 = 0;
    let _t1002_5 = 0;

    _t1000_3 = a;
    _t1001_4 = b;
    _t1002_5 = (_t1000_3 * _t1001_4);
    _at_return_0 = _t1002_5;
    return _at_return_0;
}

// Function: get_value
function get_value() {
    let _at_return_0 = 0;
    let _t1000_1 = 0;

    _t1000_1 = 42;
    _at_return_0 = _t1000_1;
    return _at_return_0;
}

// Function: print_int
function print_int(x) {
    let _at_return_0 = null;
    let _t1000_2 = null;

    console.log(__cm_format_string("{}", [x]));
    return _at_return_0;
}

// Function: apply
function apply(op, a, b) {
    let _at_return_0 = 0;
    let _t1000_4 = 0;
    let _t1001_5 = 0;
    let _t1002_6 = 0;

    _t1000_4 = a;
    _t1001_5 = b;
    _t1002_6 = op(_t1000_4, _t1001_5);
    _at_return_0 = _t1002_6;
    return _at_return_0;
}

// Function: main
function main() {
    let _at_return_0 = 0;
    let add_ptr_1 = null;
    let _t1000_2 = null;
    let result_3 = 0;
    let _t1001_4 = 0;
    let _t1002_5 = 0;
    let _t1003_6 = 0;
    let _t1004_7 = null;
    let _t1005_8 = null;
    let _t1006_9 = 0;
    let _t1007_10 = 0;
    let _t1008_11 = 0;
    let _t1009_12 = null;
    let _t1010_13 = null;
    let _t1011_14 = 0;
    let _t1012_15 = 0;
    let _t1013_16 = 0;
    let _t1014_17 = null;
    let _t1015_18 = null;
    let _t1016_19 = 0;
    let _t1017_20 = 0;
    let _t1018_21 = 0;
    let _t1019_22 = null;
    let getter_23 = null;
    let _t1020_24 = null;
    let _t1021_25 = 0;
    let _t1022_26 = null;
    let printer_27 = null;
    let _t1023_28 = null;
    let _t1024_29 = 0;
    let _t1025_30 = null;
    let _t1026_31 = 0;

    _t1000_2 = add;
    add_ptr_1 = _t1000_2;
    _t1001_4 = 10;
    _t1002_5 = 5;
    _t1003_6 = add_ptr_1(_t1001_4, _t1002_5);
    result_3 = _t1003_6;
    console.log(__cm_format_string("{}", [result_3]));
    _t1005_8 = sub;
    add_ptr_1 = _t1005_8;
    _t1006_9 = 10;
    _t1007_10 = 5;
    _t1008_11 = add_ptr_1(_t1006_9, _t1007_10);
    result_3 = _t1008_11;
    console.log(__cm_format_string("{}", [result_3]));
    _t1010_13 = add;
    _t1011_14 = 20;
    _t1012_15 = 10;
    _t1013_16 = apply(_t1010_13, _t1011_14, _t1012_15);
    result_3 = _t1013_16;
    console.log(__cm_format_string("{}", [result_3]));
    _t1015_18 = mul;
    _t1016_19 = 6;
    _t1017_20 = 7;
    _t1018_21 = apply(_t1015_18, _t1016_19, _t1017_20);
    result_3 = _t1018_21;
    console.log(__cm_format_string("{}", [result_3]));
    _t1020_24 = get_value;
    getter_23 = _t1020_24;
    _t1021_25 = getter_23();
    console.log(__cm_format_string("{}", [_t1021_25]));
    _t1023_28 = print_int;
    printer_27 = _t1023_28;
    _t1024_29 = 100;
    _t1025_30 = printer_27(_t1024_29);
    _t1026_31 = 0;
    _at_return_0 = _t1026_31;
    return _at_return_0;
}


// Entry point
main();
