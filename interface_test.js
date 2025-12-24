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

// Struct: Point
function Point() {
    return {
        x: 0,
        y: 0
    };
}

// VTables for interface dispatch
const Point_Printable_vtable = {
    print: Point__print
};

// Function: println
function println(s) {
    let _at_return_0 = null;
    let _t1000_2 = "";
    let _t1001_3 = null;

    _t1000_2 = s;
    console.log(_t1000_2);
    return _at_return_0;
}

// Function: print_it
function print_it(p) {
    let _at_return_0 = null;
    let _t1000_2 = {data: null, vtable: null};
    let _t1001_3 = null;

    _t1000_2 = __cm_clone(p);
    _t1001_3 = _t1000_2.vtable.print(_t1000_2.data);
    return _at_return_0;
}

// Function: main
function main() {
    let _at_return_0 = 0;
    let pt_1 = Point();
    let _t1000_2 = 0;
    let _t1001_3 = 0;
    let _t1002_4 = Point();
    let _t1003_5 = null;
    let _t1004_6 = 0;

    _t1000_2 = 10;
    pt_1.x = _t1000_2;
    _t1001_3 = 20;
    pt_1.y = _t1001_3;
    _t1002_4 = __cm_clone(pt_1);
    _t1003_5 = print_it({ data: _t1002_4, vtable: Point_Printable_vtable });
    _t1004_6 = 0;
    _at_return_0 = _t1004_6;
    return _at_return_0;
}

// Function: Point__print
function Point__print(self) {
    let _at_return_0 = null;
    let _t1000_2 = 0;
    let _t1001_3 = 0;
    let _t1002_4 = null;

    _t1000_2 = self.x;
    _t1001_3 = self.y;
    console.log(__cm_format_string("{} {}", [_t1000_2, _t1001_3]));
    return _at_return_0;
}


// Entry point
main();
