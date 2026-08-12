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

// Function: cm_println_string
function cm_println_string(s) {
    let _at_return_0 = null;

    return _at_return_0;
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
    let x_1 = [0];
    let _t1000_2 = 0;
    let p_3 = [null];
    let _t1001_4 = null;
    let _t1002_5 = 0;
    let _t1003_6 = 0;
    let _t1004_7 = false;
    let _t1005_8 = null;
    let _t1006_9 = 0;
    let _t1007_10 = null;
    let _t1008_11 = 0;
    let _t1009_12 = 0;
    let _t1010_13 = false;
    let _t1011_14 = null;
    let _t1012_15 = 0;
    let _t1013_16 = 0;
    let _t1014_17 = 0;
    let _t1015_18 = 0;
    let _t1016_19 = false;
    let _t1017_20 = null;
    let _t1018_21 = 0;
    let _t1019_22 = null;
    let _t1020_23 = 0;
    let _t1021_24 = 0;
    let _t1022_25 = false;
    let _t1023_26 = null;
    let _t1024_27 = 0;
    let q_28 = null;
    let _t1025_29 = null;
    let _t1026_30 = 0;
    let _t1027_31 = 0;
    let _t1028_32 = 0;
    let _t1029_33 = false;
    let _t1030_34 = null;
    let _t1031_35 = 0;
    let pp_36 = null;
    let _t1032_37 = null;
    let _t1033_38 = 0;
    let _t1034_39 = 0;
    let _t1035_40 = 0;
    let _t1036_41 = false;
    let _t1037_42 = null;
    let _t1038_43 = 0;
    let _t1039_44 = null;
    let _t1040_45 = 0;

    let __block = 0;
    __dispatch: while (true) {
        switch (__block) {
            case 0:
                _t1000_2 = 10;
                x_1[0] = _t1000_2;
                _t1001_4 = x_1;
                p_3[0] = _t1001_4;
                _t1002_5 = x_1[0];
                _t1003_6 = 10;
                _t1004_7 = (_t1002_5 !== _t1003_6);
                if (_t1004_7) {
                    __block = 1;
                    continue __dispatch;
                }
                __block = 2;
                continue __dispatch;
                break;
            case 1:
                console.log("FAILED: x init");
                __block = 4;
                continue __dispatch;
                break;
            case 2:
                __block = 3;
                continue __dispatch;
                break;
            case 3:
                _t1007_10 = p_3[0];
                _t1008_11 = _t1007_10[0];
                _t1009_12 = 10;
                _t1010_13 = (_t1008_11 !== _t1009_12);
                if (_t1010_13) {
                    __block = 6;
                    continue __dispatch;
                }
                __block = 7;
                continue __dispatch;
                break;
            case 4:
                _t1006_9 = 1;
                _at_return_0 = _t1006_9;
                return _at_return_0;
                break;
            case 5:
                __block = 3;
                continue __dispatch;
                break;
            case 6:
                console.log("FAILED: *p init");
                __block = 9;
                continue __dispatch;
                break;
            case 7:
                __block = 8;
                continue __dispatch;
                break;
            case 8:
                _t1013_16 = 20;
                p_3[0][0] = _t1013_16;
                _t1014_17 = x_1[0];
                _t1015_18 = 20;
                _t1016_19 = (_t1014_17 !== _t1015_18);
                if (_t1016_19) {
                    __block = 11;
                    continue __dispatch;
                }
                __block = 12;
                continue __dispatch;
                break;
            case 9:
                _t1012_15 = 1;
                _at_return_0 = _t1012_15;
                return _at_return_0;
                break;
            case 10:
                __block = 8;
                continue __dispatch;
                break;
            case 11:
                console.log("FAILED: x after *p=20");
                __block = 14;
                continue __dispatch;
                break;
            case 12:
                __block = 13;
                continue __dispatch;
                break;
            case 13:
                _t1019_22 = p_3[0];
                _t1020_23 = _t1019_22[0];
                _t1021_24 = 20;
                _t1022_25 = (_t1020_23 !== _t1021_24);
                if (_t1022_25) {
                    __block = 16;
                    continue __dispatch;
                }
                __block = 17;
                continue __dispatch;
                break;
            case 14:
                _t1018_21 = 1;
                _at_return_0 = _t1018_21;
                return _at_return_0;
                break;
            case 15:
                __block = 13;
                continue __dispatch;
                break;
            case 16:
                console.log("FAILED: *p after *p=20");
                __block = 19;
                continue __dispatch;
                break;
            case 17:
                __block = 18;
                continue __dispatch;
                break;
            case 18:
                _t1025_29 = p_3[0];
                q_28 = _t1025_29;
                _t1026_30 = 30;
                q_28[0] = _t1026_30;
                _t1027_31 = x_1[0];
                _t1028_32 = 30;
                _t1029_33 = (_t1027_31 !== _t1028_32);
                if (_t1029_33) {
                    __block = 21;
                    continue __dispatch;
                }
                __block = 22;
                continue __dispatch;
                break;
            case 19:
                _t1024_27 = 1;
                _at_return_0 = _t1024_27;
                return _at_return_0;
                break;
            case 20:
                __block = 18;
                continue __dispatch;
                break;
            case 21:
                console.log("FAILED: x after *q=30");
                __block = 24;
                continue __dispatch;
                break;
            case 22:
                __block = 23;
                continue __dispatch;
                break;
            case 23:
                _t1032_37 = p_3;
                pp_36 = _t1032_37;
                _t1033_38 = 40;
                pp_36[0][0] = _t1033_38;
                _t1034_39 = x_1[0];
                _t1035_40 = 40;
                _t1036_41 = (_t1034_39 !== _t1035_40);
                if (_t1036_41) {
                    __block = 26;
                    continue __dispatch;
                }
                __block = 27;
                continue __dispatch;
                break;
            case 24:
                _t1031_35 = 1;
                _at_return_0 = _t1031_35;
                return _at_return_0;
                break;
            case 25:
                __block = 23;
                continue __dispatch;
                break;
            case 26:
                console.log("FAILED: x after **pp=40");
                __block = 29;
                continue __dispatch;
                break;
            case 27:
                __block = 28;
                continue __dispatch;
                break;
            case 28:
                console.log("PASSED");
                __block = 31;
                continue __dispatch;
                break;
            case 29:
                _t1038_43 = 1;
                _at_return_0 = _t1038_43;
                return _at_return_0;
                break;
            case 30:
                __block = 28;
                continue __dispatch;
                break;
            case 31:
                _t1040_45 = 0;
                _at_return_0 = _t1040_45;
                return _at_return_0;
                break;
            case 32:
                _at_return_0 = 0;
                return _at_return_0;
                break;
            default:
                break __dispatch;
        }
    }
}


// Entry point
main();
