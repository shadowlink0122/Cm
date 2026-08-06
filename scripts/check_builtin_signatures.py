#!/usr/bin/env python3
# ============================================================
# ビルトインシグネチャの突き合わせ検査（runtime-builtin-registry 第3段）
# レジストリ表（codegen/common/builtin_registry.hpp）とnative/wasmランタイムC実装の関数シグネチャを比較し、
# 幅・個数・可変長の乖離（N4/V5/M14族: wasmのcall_indirect検査やABI残留値として実行時にしか現れなかった不一致）をlint時に検出する。
# あわせてnative/wasm二重実装同士のシグネチャ乖離も検査する。
# ============================================================

import os
import re
import sys
import collections

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
REGISTRY = os.path.join(ROOT, "src", "internal", "codegen", "common", "builtin_registry.hpp")
RUNTIME_DIRS = {
    "native": os.path.join(ROOT, "src", "internal", "codegen", "llvm", "native"),
    "wasm": os.path.join(ROOT, "src", "internal", "codegen", "llvm", "wasm"),
}

# C型→レジストリ型タグの写像。ポインタは全てPtrへ正規化する
# （sizeof/呼び出し規約の幅が一致していればLLVM宣言と互換なため）
C_TYPE_MAP = {
    "void": "Void",
    "bool": "I8",  # C側boolはABI上1バイト（レジストリのI1/I8いずれとも互換扱い）
    "char": "I8",
    "int8_t": "I8",
    "uint8_t": "I8",
    "int16_t": "I16",
    "uint16_t": "I16",
    "short": "I16",
    "int": "I32",
    "int32_t": "I32",
    "uint32_t": "I32",
    "unsigned": "I32",
    "int64_t": "I64",
    "uint64_t": "I64",
    "long long": "I64",
    "unsigned long long": "I64",
    "unsigned int": "I32",
    "unsigned char": "I8",
    "signed char": "I8",
    "_Bool": "I8",
    "float": "F32",
    "double": "F64",
}

# boolの表現差（LLVM宣言=I1、C実装=I8）は互換として扱う
COMPATIBLE = {("I1", "I8"), ("I8", "I1")}

# wasm32で幅が変わるためエクスポートシグネチャでの使用を禁止する型（M14）
FORBIDDEN_WASM_TYPES = {"size_t", "long", "unsigned long", "intptr_t", "uintptr_t", "ssize_t"}


def parse_registry():
    sigs = {}
    text = open(REGISTRY, encoding="utf-8").read()
    for m in re.finditer(
        r'\{"([^"]+)",\s*(?:"[^"]*"|nullptr),\s*TypeTag::(\w+),\s*\{([^}]*)\},\s*\d+,\s*(true|false)\}', text
    ):
        name, ret, args_str, vararg = m.groups()
        args = re.findall(r"TypeTag::(\w+)", args_str)
        sigs[name] = {"ret": ret, "args": args, "vararg": vararg == "true"}
    return sigs


def c_type_to_tag(ctype, filename, warnings):
    t = ctype.strip()
    t = re.sub(r"\bconst\b", "", t).strip()
    if "*" in t:
        return "Ptr"
    t = re.sub(r"\s+", " ", t)
    if t in FORBIDDEN_WASM_TYPES:
        if "/wasm/" in filename.replace(os.sep, "/"):
            warnings.append(f"{filename}: エクスポート関数の {t} はwasm32で幅が変わります（int64_t等の固定幅を使用）")
        return "I64" if t != "unsigned long" else "I64"
    return C_TYPE_MAP.get(t)


COMMON_DIR = os.path.join(ROOT, "src", "internal", "codegen", "common")


def parse_c_functions(directory):
    funcs = {}
    # native/wasm共通コア（common/*.inc。第4段の一本化ソース）は両プラットフォームの実装として扱う
    sources = [os.path.join(COMMON_DIR, fn) for fn in sorted(os.listdir(COMMON_DIR)) if fn.endswith(".inc")]
    sources += [os.path.join(directory, fn) for fn in sorted(os.listdir(directory)) if fn.endswith(".c")]
    for path in sources:
        _parse_c_file(path, funcs)
    return funcs


def _parse_c_file(path, funcs):
    if True:
        fn = os.path.basename(path)
        if False:
            pass
        text = open(path, encoding="utf-8", errors="ignore").read()
        # 定義のみ（末尾が'{'）。staticは内部関数のため除外
        for m in re.finditer(
            r"^([A-Za-z_][A-Za-z0-9_ \t\*]*?)\s+(cm_\w+|__builtin_\w+)\s*\(([^;{)]*(?:\([^)]*\)[^;{)]*)*)\)\s*\{",
            text,
            re.M,
        ):
            ret_c, name, args_c = m.groups()
            if re.search(r"\bstatic\b", ret_c):
                continue
            # 同名の重複定義（プラットフォーム内の別ファイル）も全件検査対象に保持する
            funcs.setdefault(name, []).append(
                {"ret_c": ret_c.strip(), "args_c": args_c.strip(), "file": path}
            )


def sig_from_c(fn, warnings):
    ret = c_type_to_tag(fn["ret_c"], fn["file"], warnings)
    args = []
    vararg = False
    args_c = fn["args_c"].strip()
    if args_c in ("", "void"):
        return ret, [], vararg
    depth = 0
    parts = []
    cur = ""
    for ch in args_c:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        parts.append(cur)
    for p in parts:
        p = p.strip()
        if p == "...":
            vararg = True
            continue
        # 関数ポインタ引数はポインタ
        if "(" in p:
            args.append("Ptr")
            continue
        # 末尾の識別子（引数名）を除去して型部分を得る
        ptype = re.sub(r"\b[A-Za-z_]\w*$", "", p).strip()
        if not ptype:
            ptype = p
        args.append(c_type_to_tag(ptype, fn["file"], warnings))
    return ret, args, vararg


def tags_equal(a, b):
    if a == b:
        return True
    return (a, b) in COMPATIBLE


def main():
    registry = parse_registry()
    warnings = []
    impls = {plat: parse_c_functions(d) for plat, d in RUNTIME_DIRS.items()}

    errors = []
    checked = 0
    # 1) レジストリ vs 各ランタイム実装
    for plat, funcs in impls.items():
        for name, defs in funcs.items():
            if name not in registry:
                continue
            for fn in defs:
                ret, args, vararg = sig_from_c(fn, warnings)
                reg = registry[name]
                if ret is None or None in args:
                    errors.append(
                        f"[{plat}] {name}: 未知のC型です（C_TYPE_MAPへ追加してください）: {fn['ret_c']}({fn['args_c']})"
                    )
                    continue
                checked += 1
                if not tags_equal(reg["ret"], ret):
                    errors.append(
                        f"[{plat}] {name}: 戻り型が不一致（レジストリ={reg['ret']}, C実装={ret}） {fn['file']}"
                    )
                if reg["vararg"] != vararg:
                    errors.append(
                        f"[{plat}] {name}: 可変長フラグが不一致（レジストリ={reg['vararg']}, C実装={vararg}） {fn['file']}"
                    )
                    continue
                if not vararg and len(reg["args"]) != len(args):
                    errors.append(
                        f"[{plat}] {name}: 引数個数が不一致（レジストリ={len(reg['args'])}, C実装={len(args)}） {fn['file']}"
                    )
                    continue
                for i, (ra, ca) in enumerate(zip(reg["args"], args)):
                    if not tags_equal(ra, ca):
                        errors.append(
                            f"[{plat}] {name}: 引数{i}の型が不一致（レジストリ={ra}, C実装={ca}） {fn['file']}"
                        )

    # 2) 共通.incとプラットフォーム.cの重複定義検査（runtime-hof-common-source 第2段）
    # 共通コアへ一本化した関数がnative/wasm側の.cへ再実装されるとリンク時の多重定義・実装ドリフトの再発源になるため機械的に禁止する
    dup_checked = 0
    for plat, funcs in impls.items():
        for name, defs in funcs.items():
            inc_files = sorted({d["file"] for d in defs if d["file"].endswith(".inc")})
            c_files = sorted({d["file"] for d in defs if d["file"].endswith(".c")})
            if inc_files:
                dup_checked += 1
            if inc_files and c_files:
                errors.append(
                    f"[{plat}] {name}: 共通コア（{os.path.basename(inc_files[0])}）に定義があるのにプラットフォーム側で重複定義されています: "
                    + ", ".join(os.path.relpath(f, ROOT) for f in c_files)
                )

    # 3) native vs wasm の二重実装同士
    cross_checked = 0
    for name in sorted(set(impls["native"]) & set(impls["wasm"])):
        n_ret, n_args, n_va = sig_from_c(impls["native"][name][0], [])
        w_ret, w_args, w_va = sig_from_c(impls["wasm"][name][0], [])
        if None in ([n_ret, w_ret] + n_args + w_args):
            continue
        cross_checked += 1
        if not tags_equal(n_ret, w_ret) or n_va != w_va or len(n_args) != len(w_args) or any(
            not tags_equal(a, b) for a, b in zip(n_args, w_args)
        ):
            errors.append(
                f"[native/wasm] {name}: 二重実装のシグネチャが乖離（native={n_ret}({','.join(n_args)}), wasm={w_ret}({','.join(w_args)})）"
            )

    for w in sorted(set(warnings)):
        print(f"警告: {w}")
    for e in errors:
        print(f"シグネチャ不一致: {e}")
    if errors:
        print("レジストリ（codegen/common/builtin_registry.hpp）とランタイムC実装のどちらが正か確認して修正してください")
        return 1
    print(
        f"✅ ビルトインシグネチャ検査通過（レジストリ照合{checked}件・native/wasm突き合わせ{cross_checked}件・共通コア重複検査{dup_checked // 2}件）"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
