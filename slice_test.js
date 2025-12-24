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
    else if (end < 0) end = len + end;
    if (end < 0) end = 0;
    if (start > len) start = len;
    if (end > len) end = len;
    return str.substring(start, end);
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
    let arr_1 = Array(5).fill(0);
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
    let slice1_13 = [];
    let _t1011_14 = Array(5).fill(0);
    let _t1012_15 = 0;
    let _t1013_16 = 0;
    let _t1014_17 = 0;
    let _t1015_18 = 0;
    let _t1016_19 = [];
    let len1_20 = 0;
    let _t1017_21 = 0;
    let _t1018_22 = null;
    let slice2_23 = [];
    let _t1019_24 = Array(5).fill(0);
    let _t1020_25 = 0;
    let _t1021_26 = 0;
    let _t1022_27 = 0;
    let _t1023_28 = 0;
    let _t1024_29 = [];
    let len2_30 = 0;
    let _t1025_31 = 0;
    let _t1026_32 = null;
    let slice3_33 = [];
    let _t1027_34 = Array(5).fill(0);
    let _t1028_35 = 0;
    let _t1029_36 = 0;
    let _t1030_37 = 0;
    let _t1031_38 = 0;
    let _t1032_39 = [];
    let len3_40 = 0;
    let _t1033_41 = 0;
    let _t1034_42 = null;
    let slice4_43 = [];
    let _t1035_44 = Array(5).fill(0);
    let _t1036_45 = 0;
    let _t1037_46 = 0;
    let _t1038_47 = 0;
    let _t1039_48 = 0;
    let _t1040_49 = 0;
    let _t1041_50 = [];
    let len4_51 = 0;
    let _t1042_52 = 0;
    let _t1043_53 = null;
    let slice5_54 = [];
    let _t1044_55 = Array(5).fill(0);
    let _t1045_56 = 0;
    let _t1046_57 = 0;
    let _t1047_58 = 0;
    let _t1048_59 = 0;
    let _t1049_60 = 0;
    let _t1050_61 = [];
    let len5_62 = 0;
    let _t1051_63 = 0;
    let _t1052_64 = null;
    let slice6_65 = [];
    let _t1053_66 = Array(5).fill(0);
    let _t1054_67 = 0;
    let _t1055_68 = 0;
    let _t1056_69 = 0;
    let _t1057_70 = 0;
    let _t1058_71 = 0;
    let _t1059_72 = 0;
    let _t1060_73 = [];
    let len6_74 = 0;
    let _t1061_75 = 0;
    let _t1062_76 = null;
    let _t1063_77 = 0;

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
    arr_1 = _t1000_2;
    _t1011_14 = arr_1;
    _t1012_15 = 4;
    _t1013_16 = 5;
    _t1014_17 = 1;
    _t1015_18 = 4;
    _t1016_19 = _t1011_14.slice(_t1014_17, _t1015_18);
    slice1_13 = _t1016_19;
    _t1017_21 = slice1_13.length;
    len1_20 = _t1017_21;
    console.log(__cm_format_string("arr[1:4].len() = {}", [len1_20]));
    _t1019_24 = arr_1;
    _t1020_25 = 4;
    _t1021_26 = 5;
    _t1022_27 = 0;
    _t1023_28 = 3;
    _t1024_29 = _t1019_24.slice(_t1022_27, _t1023_28);
    slice2_23 = _t1024_29;
    _t1025_31 = slice2_23.length;
    len2_30 = _t1025_31;
    console.log(__cm_format_string("arr[:3].len() = {}", [len2_30]));
    _t1027_34 = arr_1;
    _t1028_35 = 4;
    _t1029_36 = 5;
    _t1030_37 = 2;
    _t1031_38 = 5;
    _t1032_39 = _t1027_34.slice(_t1030_37, _t1031_38);
    slice3_33 = _t1032_39;
    _t1033_41 = slice3_33.length;
    len3_40 = _t1033_41;
    console.log(__cm_format_string("arr[2:].len() = {}", [len3_40]));
    _t1035_44 = arr_1;
    _t1036_45 = 4;
    _t1037_46 = 5;
    _t1038_47 = 0;
    _t1039_48 = 1;
    _t1040_49 = -_t1039_48;
    _t1041_50 = _t1035_44.slice(_t1038_47, _t1040_49);
    slice4_43 = _t1041_50;
    _t1042_52 = slice4_43.length;
    len4_51 = _t1042_52;
    console.log(__cm_format_string("arr[:-1].len() = {}", [len4_51]));
    _t1044_55 = arr_1;
    _t1045_56 = 4;
    _t1046_57 = 5;
    _t1047_58 = 2;
    _t1048_59 = -_t1047_58;
    _t1049_60 = 5;
    _t1050_61 = _t1044_55.slice(_t1048_59, _t1049_60);
    slice5_54 = _t1050_61;
    _t1051_63 = slice5_54.length;
    len5_62 = _t1051_63;
    console.log(__cm_format_string("arr[-2:].len() = {}", [len5_62]));
    _t1053_66 = arr_1;
    _t1054_67 = 4;
    _t1055_68 = 5;
    _t1056_69 = 3;
    _t1057_70 = -_t1056_69;
    _t1058_71 = 1;
    _t1059_72 = -_t1058_71;
    _t1060_73 = _t1053_66.slice(_t1057_70, _t1059_72);
    slice6_65 = _t1060_73;
    _t1061_75 = slice6_65.length;
    len6_74 = _t1061_75;
    console.log(__cm_format_string("arr[-3:-1].len() = {}", [len6_74]));
    _t1063_77 = 0;
    _at_return_0 = _t1063_77;
    return _at_return_0;
}


// Entry point
main();
