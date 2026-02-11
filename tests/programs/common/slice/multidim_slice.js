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
    let matrix_1 = [];
    let _t1000_2 = [Array(3).fill(0)];
    _t1000_2.__boxed = true;
    let _t1001_3 = 0;
    let _t1002_4 = 0;
    let _t1003_5 = 0;
    let _t1004_6 = 0;
    let _t1005_7 = 0;
    let _t1006_8 = 0;
    let _t1007_9 = null;
    let _t1008_10 = 0;
    let _t1009_11 = 0;
    let inner_slice_12 = [];
    let _t1010_13 = [Array(3).fill(0)];
    _t1010_13.__boxed = true;
    let _t1011_14 = 0;
    let _t1012_15 = 0;
    let _t1013_16 = 0;
    let _t1014_17 = 0;
    let _t1015_18 = 0;
    let _t1016_19 = 0;
    let _t1017_20 = null;
    let _t1018_21 = 0;
    let _t1019_22 = 0;
    let inner_slice_23 = [];
    let _t1020_24 = [Array(3).fill(0)];
    _t1020_24.__boxed = true;
    let _t1021_25 = 0;
    let _t1022_26 = 0;
    let _t1023_27 = 0;
    let _t1024_28 = 0;
    let _t1025_29 = 0;
    let _t1026_30 = 0;
    let _t1027_31 = null;
    let _t1028_32 = 0;
    let _t1029_33 = 0;
    let inner_slice_34 = [];
    let row0_35 = [];
    let _t1030_36 = 0;
    let _t1031_37 = [];
    let row1_38 = [];
    let _t1032_39 = 0;
    let _t1033_40 = [];
    let row2_41 = [];
    let _t1034_42 = 0;
    let _t1035_43 = [];
    let _t1036_44 = 0;
    let _t1037_45 = null;
    let _t1038_46 = 0;
    let _t1039_47 = null;
    let _t1040_48 = 0;
    let _t1041_49 = null;
    let _t1042_50 = 0;
    let _t1043_51 = 0;
    let _t1044_52 = 0;
    let _t1045_53 = 0;
    let _t1046_54 = 0;
    let _t1047_55 = 0;
    let _t1048_56 = null;
    let _t1049_57 = 0;
    let _t1050_58 = 0;
    let _t1051_59 = 0;
    let _t1052_60 = 0;
    let _t1053_61 = 0;
    let _t1054_62 = 0;
    let _t1055_63 = null;
    let _t1056_64 = 0;
    let _t1057_65 = 0;
    let _t1058_66 = 0;
    let _t1059_67 = 0;
    let _t1060_68 = 0;
    let _t1061_69 = 0;
    let _t1062_70 = null;
    let sorted_row_71 = [];
    let _t1063_72 = [];
    let _t1064_73 = [];
    let _t1065_74 = 0;
    let _t1066_75 = 0;
    let _t1067_76 = 0;
    let _t1068_77 = 0;
    let _t1069_78 = 0;
    let _t1070_79 = 0;
    let _t1071_80 = null;
    let first_81 = 0;
    let _t1072_82 = [];
    let _t1073_83 = 0;
    let last_84 = 0;
    let _t1074_85 = [];
    let _t1075_86 = 0;
    let _t1076_87 = null;
    let _t1077_88 = 0;

    _t1001_3 = 1;
    _t1002_4 = 0;
    _t1000_2[0][_t1002_4] = _t1001_3;
    _t1003_5 = 2;
    _t1004_6 = 1;
    _t1000_2[0][_t1004_6] = _t1003_5;
    _t1005_7 = 3;
    _t1006_8 = 2;
    _t1000_2[0][_t1006_8] = _t1005_7;
    _t1007_9 = _t1000_2;
    _t1008_10 = 3;
    _t1009_11 = 4;
    inner_slice_12 = [...__cm_unwrap(_t1007_9)];
    __cm_unwrap(matrix_1).push(inner_slice_12);
    _t1011_14 = 4;
    _t1012_15 = 0;
    _t1010_13[0][_t1012_15] = _t1011_14;
    _t1013_16 = 5;
    _t1014_17 = 1;
    _t1010_13[0][_t1014_17] = _t1013_16;
    _t1015_18 = 6;
    _t1016_19 = 2;
    _t1010_13[0][_t1016_19] = _t1015_18;
    _t1017_20 = _t1010_13;
    _t1018_21 = 3;
    _t1019_22 = 4;
    inner_slice_23 = [...__cm_unwrap(_t1017_20)];
    __cm_unwrap(matrix_1).push(inner_slice_23);
    _t1021_25 = 7;
    _t1022_26 = 0;
    _t1020_24[0][_t1022_26] = _t1021_25;
    _t1023_27 = 8;
    _t1024_28 = 1;
    _t1020_24[0][_t1024_28] = _t1023_27;
    _t1025_29 = 9;
    _t1026_30 = 2;
    _t1020_24[0][_t1026_30] = _t1025_29;
    _t1027_31 = _t1020_24;
    _t1028_32 = 3;
    _t1029_33 = 4;
    inner_slice_34 = [...__cm_unwrap(_t1027_31)];
    __cm_unwrap(matrix_1).push(inner_slice_34);
    _t1030_36 = 0;
    _t1031_37 = __cm_unwrap(matrix_1)[_t1030_36];
    row0_35 = _t1031_37;
    _t1032_39 = 1;
    _t1033_40 = __cm_unwrap(matrix_1)[_t1032_39];
    row1_38 = _t1033_40;
    _t1034_42 = 2;
    _t1035_43 = __cm_unwrap(matrix_1)[_t1034_42];
    row2_41 = _t1035_43;
    _t1036_44 = __cm_unwrap(row0_35).length;
    console.log(__cm_format_string("row0.len(): {}", [_t1036_44]));
    _t1038_46 = __cm_unwrap(row1_38).length;
    console.log(__cm_format_string("row1.len(): {}", [_t1038_46]));
    _t1040_48 = __cm_unwrap(row2_41).length;
    console.log(__cm_format_string("row2.len(): {}", [_t1040_48]));
    _t1043_51 = 0;
    _t1042_50 = __cm_unwrap(row0_35)[_t1043_51];
    _t1045_53 = 1;
    _t1044_52 = __cm_unwrap(row0_35)[_t1045_53];
    _t1047_55 = 2;
    _t1046_54 = __cm_unwrap(row0_35)[_t1047_55];
    console.log(__cm_format_string("row0: {}, {}, {}", [_t1042_50, _t1044_52, _t1046_54]));
    _t1050_58 = 0;
    _t1049_57 = __cm_unwrap(row1_38)[_t1050_58];
    _t1052_60 = 1;
    _t1051_59 = __cm_unwrap(row1_38)[_t1052_60];
    _t1054_62 = 2;
    _t1053_61 = __cm_unwrap(row1_38)[_t1054_62];
    console.log(__cm_format_string("row1: {}, {}, {}", [_t1049_57, _t1051_59, _t1053_61]));
    _t1057_65 = 0;
    _t1056_64 = __cm_unwrap(row2_41)[_t1057_65];
    _t1059_67 = 1;
    _t1058_66 = __cm_unwrap(row2_41)[_t1059_67];
    _t1061_69 = 2;
    _t1060_68 = __cm_unwrap(row2_41)[_t1061_69];
    console.log(__cm_format_string("row2: {}, {}, {}", [_t1056_64, _t1058_66, _t1060_68]));
    _t1063_72 = row2_41;
    _t1064_73 = [...__cm_unwrap(_t1063_72)].sort((a, b) => a - b);
    sorted_row_71 = _t1064_73;
    _t1066_75 = 0;
    _t1065_74 = __cm_unwrap(sorted_row_71)[_t1066_75];
    _t1068_77 = 1;
    _t1067_76 = __cm_unwrap(sorted_row_71)[_t1068_77];
    _t1070_79 = 2;
    _t1069_78 = __cm_unwrap(sorted_row_71)[_t1070_79];
    console.log(__cm_format_string("sorted: {}, {}, {}", [_t1065_74, _t1067_76, _t1069_78]));
    _t1072_82 = row1_38;
    _t1073_83 = __cm_unwrap(_t1072_82)[0];
    first_81 = _t1073_83;
    _t1074_85 = row1_38;
    _t1075_86 = __cm_unwrap(_t1074_85)[__cm_unwrap(_t1074_85).length - 1];
    last_84 = _t1075_86;
    console.log(__cm_format_string("first: {}, last: {}", [first_81, last_84]));
    _t1077_88 = 0;
    _at_return_0 = _t1077_88;
    return _at_return_0;
}


// Entry point
main();
