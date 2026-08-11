// JSランタイムヘルパーの実装
#include "internal/codegen/js/emit/runtime.hpp"

#include <string>
#include <unordered_set>

namespace cm::codegen::js {

// ランタイムヘルパーコードを出力
void emitRuntime(JSEmitter& emitter, const std::unordered_set<std::string>& used_helpers) {
    auto needs = [&](const char* name) { return used_helpers.count(name) > 0; };
    bool need_format = needs("__cm_format") || needs("__cm_format_string");

    emitter.emitLine("// Cm Runtime Helpers");

    // 64ビット整数のBigInt正規化（H5）: BigIntはそのまま、Numberは切り捨ててBigInt化。
    // len()等のNumber戻り値と64bit演算の混在をTypeErrorなしで吸収する（冪等）
    if (needs("__cm_big")) {
        emitter.emitLine("function __cm_big(v) {");
        emitter.increaseIndent();
        emitter.emitLine("return typeof v === \"bigint\" ? v : BigInt(Math.trunc(v));");
        emitter.decreaseIndent();
        emitter.emitLine("}");
        emitter.emitLine();
    }

    // BigInt透過のtrunc（H5）: BigIntはそのまま、Numberは小数切り捨て
    if (needs("__cm_trunc")) {
        emitter.emitLine("function __cm_trunc(v) {");
        emitter.increaseIndent();
        emitter.emitLine("return typeof v === \"bigint\" ? v : Math.trunc(v);");
        emitter.decreaseIndent();
        emitter.emitLine("}");
        emitter.emitLine();
    }

    // ボックス化された値を展開
    if (needs("__cm_unwrap")) {
        emitter.emitLine("function __cm_unwrap(val) {");
        emitter.increaseIndent();
        emitter.emitLine("if (val && val.__boxed) return val[0];");
        emitter.emitLine("return val;");
        emitter.decreaseIndent();
        emitter.emitLine("}");
        emitter.emitLine();
    }

    // ポインタ等値比較ヘルパー（null/undefinedを許容し、fat pointerは__arr+__idx、それ以外は参照同一性で比較する）
    // fat pointer先への書き込み。fat pointerなら指し先へ、構造体直接参照ならObject.assignで各フィールドを更新する（TS上は (p: any, v: any)）
    if (needs("__cm_deref_set")) {
        emitter.emitLine("function __cm_deref_set(p, v) {");
        emitter.increaseIndent();
        emitter.emitLine(
            "if (p && p.__arr !== undefined) { p.__arr[p.__idx] = v; } else { Object.assign(p, v); "
            "}");
        emitter.decreaseIndent();
        emitter.emitLine("}");
        emitter.emitLine();
    }

    // fat pointer（{__arr, __idx}）の間接参照。生値・構造体直接参照はそのまま返す（TS上は (p: any) => any）
    if (needs("__cm_deref")) {
        emitter.emitLine("function __cm_deref(p) {");
        emitter.increaseIndent();
        emitter.emitLine("return (p && p.__arr !== undefined) ? p.__arr[p.__idx] : p;");
        emitter.decreaseIndent();
        emitter.emitLine("}");
        emitter.emitLine();
    }

    if (needs("__cm_ptr_eq")) {
        emitter.emitLine("function __cm_ptr_eq(a, b) {");
        emitter.increaseIndent();
        emitter.emitLine("if (a == null || b == null || a === 0 || b === 0) {");
        emitter.increaseIndent();
        emitter.emitLine("return (a == null || a === 0) && (b == null || b === 0);");
        emitter.decreaseIndent();
        emitter.emitLine("}");
        emitter.emitLine(
            "if (a.__arr !== undefined && b.__arr !== undefined) { return a.__arr === b.__arr && "
            "a.__idx === b.__idx; }");
        emitter.emitLine("return a === b;");
        emitter.decreaseIndent();
        emitter.emitLine("}");
        emitter.emitLine();
    }

    // ポインタ演算ヘルパー
    if (needs("__cm_ptr_add")) {
        emitter.emitLine("function __cm_ptr_add(p, n) {");
        emitter.increaseIndent();
        emitter.emitLine("return {__arr: p.__arr, __idx: p.__idx + n};");
        emitter.decreaseIndent();
        emitter.emitLine("}");
        emitter.emitLine();
    }

    if (needs("__cm_ptr_sub")) {
        emitter.emitLine("function __cm_ptr_sub(p, n) {");
        emitter.increaseIndent();
        emitter.emitLine("return {__arr: p.__arr, __idx: p.__idx - n};");
        emitter.decreaseIndent();
        emitter.emitLine("}");
        emitter.emitLine();
    }

    // 配列スライス
    if (needs("__cm_slice")) {
        emitter.emitLine("function __cm_slice(arr, start, end) {");
        emitter.increaseIndent();
        emitter.emitLine("if (start < 0) start = arr.length + start;");
        emitter.emitLine("if (end === undefined) end = arr.length;");
        emitter.emitLine("else if (end < 0) end = arr.length + end;");
        emitter.emitLine("return arr.slice(start, end);");
        emitter.decreaseIndent();
        emitter.emitLine("}");
        emitter.emitLine();
    }

    // 文字列スライス
    if (needs("__cm_str_slice")) {
        emitter.emitLine("function __cm_str_slice(str, start, end) {");
        emitter.increaseIndent();
        // H5: BigInt添字（long由来）をNumberへ正規化
        emitter.emitLine("start = Number(start);");
        emitter.emitLine("if (end !== undefined && end !== null) end = Number(end);");
        // 添字はコードポイント単位（H9第3段。スプレッドでサロゲートペアを1要素に分解）
        emitter.emitLine("const cps = [...str];");
        emitter.emitLine("const len = cps.length;");
        emitter.emitLine("if (start < 0) start = len + start;");
        emitter.emitLine("if (start < 0) start = 0;");
        emitter.emitLine("if (end === undefined || end === null) end = len;");
        emitter.emitLine("else if (end < 0) end = len + end + 1;");
        emitter.emitLine("if (end < 0) end = 0;");
        emitter.emitLine("if (start > len) start = len;");
        emitter.emitLine("if (end > len) end = len;");
        emitter.emitLine("if (start > end) return '';");
        emitter.emitLine("return cps.slice(start, end).join(\"\");");
        emitter.decreaseIndent();
        emitter.emitLine("}");
        emitter.emitLine();
    }

    // 深い比較
    if (needs("__cm_deep_equal")) {
        emitter.emitLine("function __cm_deep_equal(a, b) {");
        emitter.increaseIndent();
        emitter.emitLine("if (a === b) return true;");
        emitter.emitLine("if (a === null || b === null) return false;");
        emitter.emitLine("if (typeof a !== 'object' || typeof b !== 'object') return false;");
        emitter.emitLine("if (Array.isArray(a)) {");
        emitter.increaseIndent();
        emitter.emitLine("if (!Array.isArray(b) || a.length !== b.length) return false;");
        emitter.emitLine("for (let i = 0; i < a.length; i++) {");
        emitter.increaseIndent();
        emitter.emitLine("if (!__cm_deep_equal(a[i], b[i])) return false;");
        emitter.decreaseIndent();
        emitter.emitLine("}");
        emitter.emitLine("return true;");
        emitter.decreaseIndent();
        emitter.emitLine("}");
        emitter.emitLine("// struct comparison");
        emitter.emitLine("const keysA = Object.keys(a);");
        emitter.emitLine("const keysB = Object.keys(b);");
        emitter.emitLine("if (keysA.length !== keysB.length) return false;");
        emitter.emitLine("for (const key of keysA) {");
        emitter.increaseIndent();
        emitter.emitLine(
            "if (!keysB.includes(key) || !__cm_deep_equal(a[key], b[key])) return false;");
        emitter.decreaseIndent();
        emitter.emitLine("}");
        emitter.emitLine("return true;");
        emitter.decreaseIndent();
        emitter.emitLine("}");
        emitter.emitLine();
    }

    // 配列初期化
    if (needs("__cm_array_init")) {
        emitter.emitLine("function __cm_array_init(size, defaultVal) {");
        emitter.increaseIndent();
        emitter.emitLine(
            "return Array(size).fill(typeof defaultVal === 'object' ? null : defaultVal);");
        emitter.decreaseIndent();
        emitter.emitLine("}");
        emitter.emitLine();
    }

    // 構造体の深いコピー
    if (needs("__cm_clone")) {
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
    }

    // double整形（非有限値・-0の表記をnativeランタイムと一致させる: inf/-inf/nan/0）
    if (needs("__cm_fmt_double")) {
        emitter.emitLine("function __cm_fmt_double(v) {");
        emitter.increaseIndent();
        emitter.emitLine("if (v === Infinity) return \"inf\";");
        emitter.emitLine("if (v === -Infinity) return \"-inf\";");
        emitter.emitLine("if (v !== v) return \"nan\";");
        emitter.emitLine("return String(v);");
        emitter.decreaseIndent();
        emitter.emitLine("}");
        emitter.emitLine();
    }

    // フォーマット関数
    if (need_format) {
        emitter.emitLine("function __cm_format(val, spec) {");
        emitter.increaseIndent();
        emitter.emitLine(
            "if (!spec) return (typeof val === 'number') ? __cm_fmt_double(val) : String(val);");
        emitter.emitLine("// char型変換");
        emitter.emitLine("if (spec === 'c') return String.fromCharCode(val);");
        emitter.emitLine(
            "// 基数指定（負数は型幅の2の補数へ正規化: "
            "bigint=64bit、number整数=32bit。nativeと一致させる）");
        emitter.emitLine(
            "const __radix = (v) => (typeof v === 'bigint') ? BigInt.asUintN(64, v) : "
            "(Number.isInteger(v) ? (v >>> 0) : v);");
        emitter.emitLine("if (spec === 'x') return __radix(val).toString(16);");
        emitter.emitLine("if (spec === 'X') return __radix(val).toString(16).toUpperCase();");
        emitter.emitLine("if (spec === 'b') return __radix(val).toString(2);");
        emitter.emitLine("if (spec === 'o') return __radix(val).toString(8);");
        emitter.emitLine(
            "// "
            "科学記法（指数はC/printf互換の2桁ゼロ埋めが正準仕様。native/jitと一致させる。R20）");
        emitter.emitLine("const __exp2 = (s) => s.replace(/([eE][+-])(\\d)$/, '$10$2');");
        emitter.emitLine("if (spec === 'e') return __exp2(val.toExponential(6));");
        emitter.emitLine("if (spec === 'E') return __exp2(val.toExponential(6)).toUpperCase();");
        emitter.emitLine("// 小数点精度 .N");
        emitter.emitLine("let precMatch = spec.match(/^\\.(\\d+)$/);");
        emitter.emitLine("if (precMatch) return val.toFixed(parseInt(precMatch[1]));");
        emitter.emitLine("// 科学記法+精度 .Ne, .NE");
        emitter.emitLine("precMatch = spec.match(/^\\.(\\d+)([eE])$/);");
        emitter.emitLine("if (precMatch) {");
        emitter.increaseIndent();
        emitter.emitLine("let result = __exp2(val.toExponential(parseInt(precMatch[1])));");
        emitter.emitLine("return precMatch[2] === 'E' ? result.toUpperCase() : result;");
        emitter.decreaseIndent();
        emitter.emitLine("}");
        emitter.emitLine(
            "// 幅・整列・ゼロ埋め・基数の複合（[fill][<>^]・0埋め・幅N・基数x/X/b/o。"
            "native/jitのcm_apply_numeric_specと同一規則。R20）");
        emitter.emitLine("let gm = spec.match(/^(?:(.)?([<>^]))?(0?)(\\d+)([xXbo]?)$/);");
        emitter.emitLine("if (gm) {");
        emitter.increaseIndent();
        emitter.emitLine("let fill = gm[1] || (gm[3] ? '0' : ' ');");
        emitter.emitLine("let align = gm[2] || '>';");
        emitter.emitLine("let width = parseInt(gm[4]);");
        emitter.emitLine("let t = gm[5];");
        emitter.emitLine("let s;");
        emitter.emitLine("if (t === 'x') s = __radix(val).toString(16);");
        emitter.emitLine("else if (t === 'X') s = __radix(val).toString(16).toUpperCase();");
        emitter.emitLine("else if (t === 'b') s = __radix(val).toString(2);");
        emitter.emitLine("else if (t === 'o') s = __radix(val).toString(8);");
        emitter.emitLine("else s = String(val);");
        emitter.emitLine("if (s.length >= width) return s;");
        emitter.emitLine(
            "if (fill === '0' && align === '>' && s[0] === '-') return '-' + "
            "s.slice(1).padStart(width - 1, '0');");
        emitter.emitLine("let pad = fill.repeat(width - s.length);");
        emitter.emitLine("if (align === '<') return s + pad;");
        emitter.emitLine("if (align === '>') return pad + s;");
        emitter.emitLine("let half = Math.floor(pad.length / 2);");
        emitter.emitLine("return pad.slice(0, half) + s + pad.slice(half);");
        emitter.decreaseIndent();
        emitter.emitLine("}");
        emitter.emitLine("return String(val);");
        emitter.decreaseIndent();
        emitter.emitLine("}");
        emitter.emitLine();
    }

    // フォーマット文字列関数
    if (needs("__cm_format_string")) {
        emitter.emitLine("function __cm_format_string(format, values) {");
        emitter.increaseIndent();
        emitter.emitLine("let result = format;");
        emitter.emitLine("let idx = 0;");
        emitter.emitLine("// エスケープされた波括弧を一時的に置換");
        emitter.emitLine("result = result.replace(/\\{\\{/g, '\\x00LBRACE\\x00');");
        emitter.emitLine("// フォーマット指定子付きプレースホルダを置換 {name:spec} or {:spec}");
        emitter.emitLine("result = result.replace(/\\{[^}]*\\}/g, (match) => {");
        emitter.increaseIndent();
        emitter.emitLine("let inner = match.slice(1, -1);");
        emitter.emitLine("let spec = '';");
        emitter.emitLine("let colonIdx = inner.indexOf(':');");
        emitter.emitLine("if (colonIdx >= 0) spec = inner.slice(colonIdx + 1);");
        emitter.emitLine(
            "// 値が尽きたプレースホルダはリテラル保持する（R20: パース不能な内容の位置ずれで"
            "後続まで巻き込みundefined置換するデータ破壊をやめ、native/jitの安全側と揃える）");
        emitter.emitLine("if (idx >= values.length) return match;");
        emitter.emitLine("return __cm_format(values[idx++], spec);");
        emitter.decreaseIndent();
        emitter.emitLine("});");
        emitter.emitLine("result = result.replace(/\\}\\}/g, '\\x00RBRACE\\x00');");
        emitter.emitLine("// エスケープを復元");
        emitter.emitLine("result = result.replace(/\\x00LBRACE\\x00/g, '{');");
        emitter.emitLine("result = result.replace(/\\x00RBRACE\\x00/g, '}');");
        emitter.emitLine("return result;");
        emitter.decreaseIndent();
        emitter.emitLine("}");
        emitter.emitLine();
    }

    // 文字列連結ヘルパー
    if (needs("__cm_str_concat")) {
        emitter.emitLine("function __cm_str_concat(a, b) {");
        emitter.increaseIndent();
        emitter.emitLine("return String(a) + String(b);");
        emitter.decreaseIndent();
        emitter.emitLine("}");
        emitter.emitLine();
    }
}

// Webランタイム出力
void emitWebRuntime(JSEmitter& emitter) {
    emitter.emitLine("// Cm Web Runtime");
    emitter.emitLine("(function() {");
    emitter.increaseIndent();
    emitter.emitLine("if (typeof globalThis === \"undefined\") return;");
    emitter.emitLine("const root = globalThis.cm || (globalThis.cm = {});");
    emitter.emitLine("const web = root.web || (root.web = {});");
    emitter.emitLine("const set = web.set || (web.set = {});");
    emitter.emitLine("const append = web.append || (web.append = {});");
    emitter.emitLine("const get = web.get || (web.get = {});");
    emitter.emitLine("function ensureRoot() {");
    emitter.increaseIndent();
    emitter.emitLine("if (typeof document === \"undefined\") return null;");
    emitter.emitLine("let el = document.getElementById(\"cm-root\");");
    emitter.emitLine("if (!el) el = document.body || document.documentElement;");
    emitter.emitLine("return el;");
    emitter.decreaseIndent();
    emitter.emitLine("}");
    emitter.emitLine("function ensureStyle() {");
    emitter.increaseIndent();
    emitter.emitLine("if (typeof document === \"undefined\") return null;");
    emitter.emitLine("let style = document.getElementById(\"cm-style\");");
    emitter.emitLine("if (!style) {");
    emitter.increaseIndent();
    emitter.emitLine("style = document.createElement(\"style\");");
    emitter.emitLine("style.id = \"cm-style\";");
    emitter.emitLine("(document.head || document.documentElement).appendChild(style);");
    emitter.decreaseIndent();
    emitter.emitLine("}");
    emitter.emitLine("return style;");
    emitter.decreaseIndent();
    emitter.emitLine("}");
    emitter.emitLine("set.html = set.html || function(html) {");
    emitter.increaseIndent();
    emitter.emitLine("const el = ensureRoot();");
    emitter.emitLine("if (!el) return;");
    emitter.emitLine("el.innerHTML = html;");
    emitter.decreaseIndent();
    emitter.emitLine("};");
    emitter.emitLine("append.html = append.html || function(html) {");
    emitter.increaseIndent();
    emitter.emitLine("const el = ensureRoot();");
    emitter.emitLine("if (!el) return;");
    emitter.emitLine("el.insertAdjacentHTML(\"beforeend\", html);");
    emitter.decreaseIndent();
    emitter.emitLine("};");
    emitter.emitLine("set.css = set.css || function(css) {");
    emitter.increaseIndent();
    emitter.emitLine("const style = ensureStyle();");
    emitter.emitLine("if (!style) return;");
    emitter.emitLine("style.textContent = css;");
    emitter.decreaseIndent();
    emitter.emitLine("};");
    emitter.emitLine("append.css = append.css || function(css) {");
    emitter.increaseIndent();
    emitter.emitLine("const style = ensureStyle();");
    emitter.emitLine("if (!style) return;");
    emitter.emitLine("style.textContent += css;");
    emitter.decreaseIndent();
    emitter.emitLine("};");
    emitter.emitLine("get.html = get.html || function() {");
    emitter.increaseIndent();
    emitter.emitLine("const el = ensureRoot();");
    emitter.emitLine("return el ? el.innerHTML : \"\";");
    emitter.decreaseIndent();
    emitter.emitLine("};");
    emitter.emitLine("get.css = get.css || function() {");
    emitter.increaseIndent();
    emitter.emitLine("const style = ensureStyle();");
    emitter.emitLine("return style ? style.textContent : \"\";");
    emitter.decreaseIndent();
    emitter.emitLine("};");
    emitter.emitLine("set.title = set.title || function(title) {");
    emitter.increaseIndent();
    emitter.emitLine("if (typeof document !== \"undefined\") document.title = title;");
    emitter.decreaseIndent();
    emitter.emitLine("};");
    emitter.emitLine("web.on = web.on || function(event, selectorOrCallback, callback) {");
    emitter.increaseIndent();
    emitter.emitLine("if (typeof document === \"undefined\") return;");
    emitter.emitLine("if (typeof selectorOrCallback === \"function\") {");
    emitter.increaseIndent();
    emitter.emitLine("document.addEventListener(event, selectorOrCallback);");
    emitter.decreaseIndent();
    emitter.emitLine("} else {");
    emitter.increaseIndent();
    emitter.emitLine("document.addEventListener(event, function(e) {");
    emitter.increaseIndent();
    emitter.emitLine("if (e.target && e.target.matches(selectorOrCallback)) callback(e);");
    emitter.decreaseIndent();
    emitter.emitLine("});");
    emitter.decreaseIndent();
    emitter.emitLine("}");
    emitter.decreaseIndent();
    emitter.emitLine("};");
    emitter.emitLine("web.query = web.query || function(selector) {");
    emitter.increaseIndent();
    emitter.emitLine("if (typeof document === \"undefined\") return null;");
    emitter.emitLine("return document.querySelector(selector);");
    emitter.decreaseIndent();
    emitter.emitLine("};");
    emitter.emitLine("web.queryAll = web.queryAll || function(selector) {");
    emitter.increaseIndent();
    emitter.emitLine("if (typeof document === \"undefined\") return [];");
    emitter.emitLine("return Array.from(document.querySelectorAll(selector));");
    emitter.decreaseIndent();
    emitter.emitLine("};");
    emitter.decreaseIndent();
    emitter.emitLine("})();");
    emitter.emitLine();
}

}  // namespace cm::codegen::js
