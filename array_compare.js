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
    let a_1 = Array(3).fill(0);
    let _t1000_2 = Array(3).fill(0);
    let _t1001_3 = 0;
    let _t1002_4 = 0;
    let _t1003_5 = 0;
    let _t1004_6 = 0;
    let _t1005_7 = 0;
    let _t1006_8 = 0;
    let b_9 = Array(3).fill(0);
    let _t1007_10 = Array(3).fill(0);
    let _t1008_11 = 0;
    let _t1009_12 = 0;
    let _t1010_13 = 0;
    let _t1011_14 = 0;
    let _t1012_15 = 0;
    let _t1013_16 = 0;
    let c_17 = Array(3).fill(0);
    let _t1014_18 = Array(3).fill(0);
    let _t1015_19 = 0;
    let _t1016_20 = 0;
    let _t1017_21 = 0;
    let _t1018_22 = 0;
    let _t1019_23 = 0;
    let _t1020_24 = 0;
    let _t1021_25 = Array(3).fill(0);
    let _t1022_26 = Array(3).fill(0);
    let _t1023_27 = 0;
    let _t1024_28 = 0;
    let _t1025_29 = 0;
    let _t1026_30 = false;
    let _t1027_31 = null;
    let _t1028_32 = Array(3).fill(0);
    let _t1029_33 = Array(3).fill(0);
    let _t1030_34 = 0;
    let _t1031_35 = 0;
    let _t1032_36 = 0;
    let _t1033_37 = false;
    let _t1034_38 = false;
    let _t1035_39 = null;
    let s1_40 = [];
    let _t1036_41 = 0;
    let _t1037_42 = 0;
    let _t1038_43 = 0;
    let s2_44 = [];
    let _t1039_45 = 0;
    let _t1040_46 = 0;
    let _t1041_47 = 0;
    let s3_48 = [];
    let _t1042_49 = 0;
    let _t1043_50 = 0;
    let _t1044_51 = 0;
    let s4_52 = [];
    let _t1045_53 = 0;
    let _t1046_54 = 0;
    let _t1047_55 = [];
    let _t1048_56 = [];
    let _t1049_57 = false;
    let _t1050_58 = null;
    let _t1051_59 = [];
    let _t1052_60 = [];
    let _t1053_61 = false;
    let _t1054_62 = false;
    let _t1055_63 = null;
    let _t1056_64 = [];
    let _t1057_65 = [];
    let _t1058_66 = false;
    let _t1059_67 = false;
    let _t1060_68 = null;
    let _t1061_69 = 0;

    let __block = 0;
    __dispatch: while (true) {
        switch (__block) {
            case 0:
                _t1001_3 = 1;
                _t1002_4 = 0;
                _t1000_2[_t1002_4] = _t1001_3;
                _t1003_5 = 2;
                _t1004_6 = 1;
                _t1000_2[_t1004_6] = _t1003_5;
                _t1005_7 = 3;
                _t1006_8 = 2;
                _t1000_2[_t1006_8] = _t1005_7;
                a_1 = _t1000_2;
                _t1008_11 = 1;
                _t1009_12 = 0;
                _t1007_10[_t1009_12] = _t1008_11;
                _t1010_13 = 2;
                _t1011_14 = 1;
                _t1007_10[_t1011_14] = _t1010_13;
                _t1012_15 = 3;
                _t1013_16 = 2;
                _t1007_10[_t1013_16] = _t1012_15;
                b_9 = _t1007_10;
                _t1015_19 = 1;
                _t1016_20 = 0;
                _t1014_18[_t1016_20] = _t1015_19;
                _t1017_21 = 2;
                _t1018_22 = 1;
                _t1014_18[_t1018_22] = _t1017_21;
                _t1019_23 = 4;
                _t1020_24 = 2;
                _t1014_18[_t1020_24] = _t1019_23;
                c_17 = _t1014_18;
                _t1021_25 = a_1;
                _t1022_26 = b_9;
                _t1023_27 = 3;
                _t1024_28 = 3;
                _t1025_29 = 4;
                _t1026_30 = __cm_deep_equal(_t1021_25, _t1022_26);
                __block = 1;
                continue __dispatch;
                break;
            case 1:
                if (_t1026_30) {
                    __block = 2;
                    continue __dispatch;
                }
                __block = 3;
                continue __dispatch;
                break;
            case 2:
                console.log("a == b: true");
                __block = 5;
                continue __dispatch;
                break;
            case 3:
                __block = 4;
                continue __dispatch;
                break;
            case 4:
                _t1028_32 = a_1;
                _t1029_33 = c_17;
                _t1030_34 = 3;
                _t1031_35 = 3;
                _t1032_36 = 4;
                _t1033_37 = __cm_deep_equal(_t1028_32, _t1029_33);
                __block = 6;
                continue __dispatch;
                break;
            case 5:
                __block = 4;
                continue __dispatch;
                break;
            case 6:
                _t1034_38 = !_t1033_37;
                if (_t1034_38) {
                    __block = 7;
                    continue __dispatch;
                }
                __block = 8;
                continue __dispatch;
                break;
            case 7:
                console.log("a != c: true");
                __block = 10;
                continue __dispatch;
                break;
            case 8:
                __block = 9;
                continue __dispatch;
                break;
            case 9:
                _t1036_41 = 1;
                s1_40.push(_t1036_41);
                __block = 11;
                continue __dispatch;
                break;
            case 10:
                __block = 9;
                continue __dispatch;
                break;
            case 11:
                _t1037_42 = 2;
                s1_40.push(_t1037_42);
                __block = 12;
                continue __dispatch;
                break;
            case 12:
                _t1038_43 = 3;
                s1_40.push(_t1038_43);
                __block = 13;
                continue __dispatch;
                break;
            case 13:
                _t1039_45 = 1;
                s2_44.push(_t1039_45);
                __block = 14;
                continue __dispatch;
                break;
            case 14:
                _t1040_46 = 2;
                s2_44.push(_t1040_46);
                __block = 15;
                continue __dispatch;
                break;
            case 15:
                _t1041_47 = 3;
                s2_44.push(_t1041_47);
                __block = 16;
                continue __dispatch;
                break;
            case 16:
                _t1042_49 = 1;
                s3_48.push(_t1042_49);
                __block = 17;
                continue __dispatch;
                break;
            case 17:
                _t1043_50 = 2;
                s3_48.push(_t1043_50);
                __block = 18;
                continue __dispatch;
                break;
            case 18:
                _t1044_51 = 4;
                s3_48.push(_t1044_51);
                __block = 19;
                continue __dispatch;
                break;
            case 19:
                _t1045_53 = 1;
                s4_52.push(_t1045_53);
                __block = 20;
                continue __dispatch;
                break;
            case 20:
                _t1046_54 = 2;
                s4_52.push(_t1046_54);
                __block = 21;
                continue __dispatch;
                break;
            case 21:
                _t1047_55 = s1_40;
                _t1048_56 = s2_44;
                _t1049_57 = __cm_deep_equal(_t1047_55, _t1048_56);
                __block = 22;
                continue __dispatch;
                break;
            case 22:
                if (_t1049_57) {
                    __block = 23;
                    continue __dispatch;
                }
                __block = 24;
                continue __dispatch;
                break;
            case 23:
                console.log("s1 == s2: true");
                __block = 26;
                continue __dispatch;
                break;
            case 24:
                __block = 25;
                continue __dispatch;
                break;
            case 25:
                _t1051_59 = s1_40;
                _t1052_60 = s3_48;
                _t1053_61 = __cm_deep_equal(_t1051_59, _t1052_60);
                __block = 27;
                continue __dispatch;
                break;
            case 26:
                __block = 25;
                continue __dispatch;
                break;
            case 27:
                _t1054_62 = !_t1053_61;
                if (_t1054_62) {
                    __block = 28;
                    continue __dispatch;
                }
                __block = 29;
                continue __dispatch;
                break;
            case 28:
                console.log("s1 != s3: true");
                __block = 31;
                continue __dispatch;
                break;
            case 29:
                __block = 30;
                continue __dispatch;
                break;
            case 30:
                _t1056_64 = s1_40;
                _t1057_65 = s4_52;
                _t1058_66 = __cm_deep_equal(_t1056_64, _t1057_65);
                __block = 32;
                continue __dispatch;
                break;
            case 31:
                __block = 30;
                continue __dispatch;
                break;
            case 32:
                _t1059_67 = !_t1058_66;
                if (_t1059_67) {
                    __block = 33;
                    continue __dispatch;
                }
                __block = 34;
                continue __dispatch;
                break;
            case 33:
                console.log("s1 != s4: true (different length)");
                __block = 36;
                continue __dispatch;
                break;
            case 34:
                __block = 35;
                continue __dispatch;
                break;
            case 35:
                _t1061_69 = 0;
                _at_return_0 = _t1061_69;
                return _at_return_0;
                break;
            case 36:
                __block = 35;
                continue __dispatch;
                break;
            case 37:
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
