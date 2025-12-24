#pragma once

#include "emitter.hpp"

namespace cm::codegen::js {

// ランタイムヘルパーコードを出力
inline void emitRuntime(JSEmitter& emitter) {
    emitter.emitLine("// Cm Runtime Helpers");

    // 配列スライス
    emitter.emitLine("function __cm_slice(arr, start, end) {");
    emitter.increaseIndent();
    emitter.emitLine("if (start < 0) start = arr.length + start;");
    emitter.emitLine("if (end === undefined) end = arr.length;");
    emitter.emitLine("else if (end < 0) end = arr.length + end;");
    emitter.emitLine("return arr.slice(start, end);");
    emitter.decreaseIndent();
    emitter.emitLine("}");
    emitter.emitLine();

    // 文字列スライス（負のインデックスは末尾までの長さ+1として扱い、包括的にする）
    emitter.emitLine("function __cm_str_slice(str, start, end) {");
    emitter.increaseIndent();
    emitter.emitLine("const len = str.length;");
    emitter.emitLine("if (start < 0) start = len + start;");
    emitter.emitLine("if (start < 0) start = 0;");
    emitter.emitLine("if (end === undefined || end === null) end = len;");
    emitter.emitLine("else if (end < 0) end = len + end + 1;");  // Recursive/Inclusive adjustment
    emitter.emitLine("if (end < 0) end = 0;");
    emitter.emitLine("if (start > len) start = len;");
    emitter.emitLine("if (end > len) end = len;");
    emitter.emitLine("if (start > end) return '';");
    emitter.emitLine("return str.substring(start, end);");
    emitter.decreaseIndent();
    emitter.emitLine("}");
    emitter.emitLine();

    // 深い比較（配列・構造体用）
    emitter.emitLine("function __cm_deep_equal(a, b) {");
    emitter.increaseIndent();
    emitter.emitLine("if (a === b) return true;");
    emitter.emitLine("if (a === null || b === null) return false;");
    emitter.emitLine("if (typeof a !== 'object' || typeof b !== 'object') return false;");
    emitter.emitLine("if (Array.isArray(a)) {");
    emitter.increaseIndent();
    emitter.emitLine("if (!Array.isArray(b) || a.length !== b.length) return false;");
    emitter.emitLine("for (let i = 0; i < a.length; i++) {");
    emitter.emitLine("    if (!__cm_deep_equal(a[i], b[i])) return false;");
    emitter.emitLine("}");
    emitter.emitLine("return true;");
    emitter.decreaseIndent();
    emitter.emitLine("}");
    emitter.emitLine("// struct comparison");
    emitter.emitLine("const keysA = Object.keys(a);");
    emitter.emitLine("const keysB = Object.keys(b);");
    emitter.emitLine("if (keysA.length !== keysB.length) return false;");
    emitter.emitLine("for (const key of keysA) {");
    emitter.emitLine(
        "    if (!keysB.includes(key) || !__cm_deep_equal(a[key], b[key])) return false;");
    emitter.emitLine("}");
    emitter.emitLine("return true;");
    emitter.decreaseIndent();
    emitter.emitLine("}");
    emitter.emitLine();

    // 配列初期化
    emitter.emitLine("function __cm_array_init(size, defaultVal) {");
    emitter.increaseIndent();
    emitter.emitLine(
        "return Array(size).fill(typeof defaultVal === 'object' ? null : defaultVal);");
    emitter.decreaseIndent();
    emitter.emitLine("}");
    emitter.emitLine();

    // 構造体の深いコピー
    emitter.emitLine("function __cm_clone(obj) {");
    emitter.increaseIndent();
    emitter.emitLine("if (obj === null || typeof obj !== 'object') return obj;");
    emitter.emitLine("if (Array.isArray(obj)) return obj.map(__cm_clone);");
    emitter.emitLine("const result = {};");
    emitter.emitLine("for (const key in obj) result[key] = __cm_clone(obj[key]);");
    emitter.emitLine("return result;");
    emitter.decreaseIndent();
    emitter.emitLine("}");
    emitter.emitLine();

    // フォーマット関数（拡張版）
    emitter.emitLine("function __cm_format(val, spec) {");
    emitter.increaseIndent();
    emitter.emitLine("if (!spec) return String(val);");
    emitter.emitLine("// char型変換");
    emitter.emitLine("if (spec === 'c') return String.fromCharCode(val);");
    emitter.emitLine("// 基数指定");
    emitter.emitLine("if (spec === 'x') return val.toString(16);");
    emitter.emitLine("if (spec === 'X') return val.toString(16).toUpperCase();");
    emitter.emitLine("if (spec === 'b') return val.toString(2);");
    emitter.emitLine("if (spec === 'o') return val.toString(8);");
    emitter.emitLine("// 科学記法");
    emitter.emitLine("if (spec === 'e') return val.toExponential();");
    emitter.emitLine("if (spec === 'E') return val.toExponential().toUpperCase();");
    emitter.emitLine("// 小数点精度 .N");
    emitter.emitLine("let precMatch = spec.match(/^\\.(\\d+)$/);");
    emitter.emitLine("if (precMatch) return val.toFixed(parseInt(precMatch[1]));");
    emitter.emitLine("// 科学記法+精度 .Ne, .NE");
    emitter.emitLine("precMatch = spec.match(/^\\.(\\d+)([eE])$/);");
    emitter.emitLine("if (precMatch) {");
    emitter.increaseIndent();
    emitter.emitLine("let result = val.toExponential(parseInt(precMatch[1]));");
    emitter.emitLine("return precMatch[2] === 'E' ? result.toUpperCase() : result;");
    emitter.decreaseIndent();
    emitter.emitLine("}");
    emitter.emitLine("// 幅とアライメント");
    emitter.emitLine("let alignMatch = spec.match(/^([<>^]?)(\\d+)$/);");
    emitter.emitLine("if (alignMatch) {");
    emitter.increaseIndent();
    emitter.emitLine("let align = alignMatch[1] || '>';");
    emitter.emitLine("let width = parseInt(alignMatch[2]);");
    emitter.emitLine("let s = String(val);");
    emitter.emitLine("if (s.length >= width) return s;");
    emitter.emitLine("let pad = ' '.repeat(width - s.length);");
    emitter.emitLine("if (align === '<') return s + pad;");
    emitter.emitLine("if (align === '>') return pad + s;");
    emitter.emitLine("let half = Math.floor(pad.length / 2);");
    emitter.emitLine("return pad.slice(0, half) + s + pad.slice(half);");
    emitter.decreaseIndent();
    emitter.emitLine("}");
    emitter.emitLine("// ゼロパディング 0>N");
    emitter.emitLine("let zeroPadMatch = spec.match(/^0>(\\d+)$/);");
    emitter.emitLine("if (zeroPadMatch) {");
    emitter.increaseIndent();
    emitter.emitLine("let width = parseInt(zeroPadMatch[1]);");
    emitter.emitLine("return String(val).padStart(width, '0');");
    emitter.decreaseIndent();
    emitter.emitLine("}");
    emitter.emitLine("return String(val);");
    emitter.decreaseIndent();
    emitter.emitLine("}");
    emitter.emitLine();

    // フォーマット文字列関数（printlnで使用）
    emitter.emitLine("function __cm_format_string(format, values) {");
    emitter.increaseIndent();
    emitter.emitLine("let result = format;");
    emitter.emitLine("let idx = 0;");
    emitter.emitLine("// エスケープされた波括弧を一時的に置換");
    emitter.emitLine("result = result.replace(/\\{\\{/g, '\\x00LBRACE\\x00');");
    emitter.emitLine("result = result.replace(/\\}\\}/g, '\\x00RBRACE\\x00');");
    emitter.emitLine("// フォーマット指定子付きプレースホルダを置換 {name:spec} or {:spec}");
    emitter.emitLine("result = result.replace(/\\{[^}]*\\}/g, (match) => {");
    emitter.increaseIndent();
    emitter.emitLine("let inner = match.slice(1, -1);");
    emitter.emitLine("let spec = '';");
    emitter.emitLine("let colonIdx = inner.indexOf(':');");
    emitter.emitLine("if (colonIdx >= 0) spec = inner.slice(colonIdx + 1);");
    emitter.emitLine("return __cm_format(values[idx++], spec);");
    emitter.decreaseIndent();
    emitter.emitLine("});");
    emitter.emitLine("// エスケープを復元");
    emitter.emitLine("result = result.replace(/\\x00LBRACE\\x00/g, '{');");
    emitter.emitLine("result = result.replace(/\\x00RBRACE\\x00/g, '}');");
    emitter.emitLine("return result;");
    emitter.decreaseIndent();
    emitter.emitLine("}");
    emitter.emitLine();

    // 文字列連結ヘルパー
    emitter.emitLine("function __cm_str_concat(a, b) {");
    emitter.increaseIndent();
    emitter.emitLine("return String(a) + String(b);");
    emitter.decreaseIndent();
    emitter.emitLine("}");
    emitter.emitLine();
}

}  // namespace cm::codegen::js
