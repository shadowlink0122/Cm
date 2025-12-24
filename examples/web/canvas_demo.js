"use strict";

// Cm Runtime Helpers
function __cm_slice(arr, start, end) {
    if (start < 0) start = arr.length + start;
    if (end === undefined) end = arr.length;
    else if (end < 0) end = arr.length + end;
    return arr.slice(start, end);
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
    if (spec === 'x') return val.toString(16);
    if (spec === 'X') return val.toString(16).toUpperCase();
    if (spec === 'b') return val.toString(2);
    if (spec === 'o') return val.toString(8);
    return String(val);
}

function __cm_format_string(format, values) {
    let result = format;
    let idx = 0;
    result = result.replace(/\{\}/g, () => values[idx++]);
    return result;
}

// Function: main
function main() {
    let _at_return = 0;
    let width = 0;
    let _t1000 = 0;
    let height = 0;
    let _t1001 = 0;
    let _t1002 = null;
    let _t1003 = null;
    let _t1004 = null;
    let x = 0;
    let _t1005 = 0;
    let y = 0;
    let _t1006 = 0;
    let w = 0;
    let _t1007 = 0;
    let h = 0;
    let _t1008 = 0;
    let _t1009 = null;
    let cx = 0;
    let _t1010 = 0;
    let _t1011 = 0;
    let _t1012 = 0;
    let _t1013 = 0;
    let _t1014 = 0;
    let cy = 0;
    let _t1015 = 0;
    let _t1016 = 0;
    let _t1017 = 0;
    let _t1018 = 0;
    let _t1019 = 0;
    let _t1020 = null;
    let _t1021 = 0;

    let __block = 0;
    __dispatch: while (true) {
        switch (__block) {
            case 0:
                _t1000 = 400;
                width = _t1000;
                _t1001 = 300;
                height = _t1001;
                console.log("Canvas Example");
                __block = 1;
                continue __dispatch;
                break;
            case 1:
                console.log(__cm_format_string("Width: {}", [width]));
                __block = 2;
                continue __dispatch;
                break;
            case 2:
                console.log(__cm_format_string("Height: {}", [height]));
                __block = 3;
                continue __dispatch;
                break;
            case 3:
                _t1005 = 50;
                x = _t1005;
                _t1006 = 50;
                y = _t1006;
                _t1007 = 100;
                w = _t1007;
                _t1008 = 75;
                h = _t1008;
                console.log(__cm_format_string("Rectangle: ({}, {}, {}, {})", [x, y, w, h]));
                __block = 4;
                continue __dispatch;
                break;
            case 4:
                _t1010 = x;
                _t1011 = w;
                _t1012 = 2;
                _t1013 = Math.trunc(_t1011 / _t1012);
                _t1014 = (_t1010 + _t1013);
                cx = _t1014;
                _t1015 = y;
                _t1016 = h;
                _t1017 = 2;
                _t1018 = Math.trunc(_t1016 / _t1017);
                _t1019 = (_t1015 + _t1018);
                cy = _t1019;
                console.log(__cm_format_string("Center: ({}, {})", [cx, cy]));
                __block = 5;
                continue __dispatch;
                break;
            case 5:
                _t1021 = 0;
                _at_return = _t1021;
                return _at_return;
                break;
            case 6:
                _at_return = 0;
                return _at_return;
                break;
            default:
                break __dispatch;
        }
    }
}


// Entry point
main();
