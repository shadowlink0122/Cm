#!/usr/bin/env python3
# ============================================================
# src/internal層間の依存規律チェック（compiler-architecture-restructure 第1段）
# 各層のソースが#includeで参照してよい層をALLOWEDの隣接リストで固定し、逆依存・新規の層違反をlint時に検出する。
# 依存を追加する場合は設計文書docs/design/v0.17.0/compiler-architecture-restructure.mdの依存図と整合させてからALLOWEDを更新すること
# ============================================================

import os
import re
import sys
import collections

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "internal")
ROOT = os.path.normpath(ROOT)

# 許可する依存辺（依存元層 -> 依存先層の集合）。設計文書の依存図に対応する
# base(span/診断/i18n/デバッグ)が最下層、fmtはbaseのみ(テキストベース整形でパーサ非依存)、
# 終端はcodegen/lint/fmtで、driver(src/cmd)は全層を配線してよいため対象外
ALLOWED = {
    "base": set(),
    "diagnostics": {"base"},
    "syntax": {"base"},
    "fmt": {"base"},
    "macro": {"base", "syntax"},
    "preprocessor": {"base"},
    "hir": {"base", "syntax"},
    "types": {"base", "syntax"},
    "lint": {"base", "diagnostics", "syntax"},
    "mir": {"base", "hir", "syntax"},
    # preprocessorへの依存はモジュール指定子リゾルバ（resolve_module_path）と条件コンパイルの共有（module/graph.cpp）
    "module": {"base", "hir", "mir", "preprocessor", "syntax"},
    "codegen": {"base", "hir", "mir", "syntax"},
}

SOURCE_EXTS = (".cpp", ".hpp", ".h", ".c", ".mm")
INTERNAL_INCLUDE = re.compile(r'#include\s+"(?:\.\./)*internal/([a-z_]+)/')
RELATIVE_INCLUDE = re.compile(r'#include\s+"((?:\.\./)+)([a-z_]+)/')


def layer_of_relative_include(dirpath, ups, first_segment, layers):
    # ../連鎖を辿った先がsrc/internal直下の層ならその層名を返す
    base = dirpath
    for _ in range(ups):
        base = os.path.dirname(base)
    candidate = os.path.normpath(os.path.join(base, first_segment))
    parent, name = os.path.split(candidate)
    if os.path.normpath(parent) == ROOT and name in layers:
        return name
    return None


def collect_edges():
    layers = sorted(d for d in os.listdir(ROOT) if os.path.isdir(os.path.join(ROOT, d)))
    edges = collections.defaultdict(set)  # (src層, dst層) -> {参照元ファイル}
    for layer in layers:
        for dirpath, _, files in os.walk(os.path.join(ROOT, layer)):
            for fn in files:
                if not fn.endswith(SOURCE_EXTS):
                    continue
                path = os.path.join(dirpath, fn)
                try:
                    text = open(path, encoding="utf-8", errors="ignore").read()
                except OSError:
                    continue
                rel = os.path.relpath(path, ROOT)
                for m in INTERNAL_INCLUDE.finditer(text):
                    target = m.group(1)
                    if target in layers and target != layer:
                        edges[(layer, target)].add(rel)
                for m in RELATIVE_INCLUDE.finditer(text):
                    target = layer_of_relative_include(dirpath, m.group(1).count("../"), m.group(2), layers)
                    if target and target != layer:
                        edges[(layer, target)].add(rel)
    return layers, edges


def main():
    layers, edges = collect_edges()
    show_matrix = "--matrix" in sys.argv

    unknown = [l for l in layers if l not in ALLOWED]
    violations = []
    for (src, dst), files in sorted(edges.items()):
        if src in ALLOWED and dst not in ALLOWED.get(src, set()):
            violations.append((src, dst, sorted(files)))

    if show_matrix:
        print("依存行列（行=依存元, 列=依存先, 値=参照ファイル数）")
        width = max(len(l) for l in layers) + 2
        print(" " * width + "  ".join(f"{l[:10]:>10s}" for l in layers))
        for src in layers:
            row = [len(edges.get((src, dst), ())) for dst in layers]
            print(f"{src:<{width}s}" + "  ".join(f"{v:10d}" for v in row))

    ok = True
    if unknown:
        ok = False
        print(f"エラー: ALLOWEDに未登録の層があります: {', '.join(unknown)}")
        print("scripts/check_layer_deps.pyのALLOWEDへ層と許可依存を追記してください")
    for src, dst, files in violations:
        ok = False
        print(f"層違反: {src} -> {dst} は許可されていません（許可: {', '.join(sorted(ALLOWED[src])) or 'なし'}）")
        for f in files[:5]:
            print(f"  参照元: src/internal/{f}")
        if len(files) > 5:
            print(f"  ...他{len(files) - 5}ファイル")

    if ok:
        print(f"✅ 層依存チェック通過（{len(layers)}層・依存辺{len(edges)}本）")
        return 0
    print("設計文書の依存図（docs/design/v0.17.0/compiler-architecture-restructure.md）を参照してください")
    return 1


if __name__ == "__main__":
    sys.exit(main())
