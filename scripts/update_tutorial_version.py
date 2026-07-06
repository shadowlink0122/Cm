#!/usr/bin/env python3
# チュートリアルの「現行バージョンバッジ」をVERSIONファイルの値に更新する。
#
# 使い方:
#   python3 scripts/update_tutorial_version.py           # 更新を適用
#   python3 scripts/update_tutorial_version.py --check   # 乖離があれば非0終了（CI用）
#
# 更新対象はバージョンバッジ（現行版を示す表記）のみ:
#   - ルートindexのタイトル「Cm言語チュートリアル vX.Y.Z」（前後ナビ内の同表記も追従）
#   - 「**対象バージョン:** vX.Y.Z」 / 「**Target [Vv]ersion:** vX.Y.Z」
#   - 「実装状況一覧（vX.Y.Z）」
#   - 「サポート状況（vX.Y.Z）」 / 「Support Status（vX.Y.Z）」
#   - 「### vX.Y.Z テスト結果」 / 「### vX.Y.Z Test Results」
#     （テスト数値は実測値のため自動更新しない。スイート実行後に手動更新すること）
#
# 「v0.11.0以降」「（v0.14.0で追加）」やリリースノートへのリンクなど、
# 機能の導入時期を示す履歴的言及は意図的に対象外とする。

import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
TUTORIALS = os.path.join(ROOT, "docs", "tutorials")

VER_RE = r"v\d+\.\d+\.\d+"

# (置換パターン, 置換後テンプレート)。{V} が新バージョンに展開される
BADGE_PATTERNS = [
    (rf"(Cm言語チュートリアル ){VER_RE}", r"\g<1>{V}"),
    (rf"(Cm Language Tutorials ){VER_RE}", r"\g<1>{V}"),
    (rf"(\*\*対象バージョン:\*\* ){VER_RE}", r"\g<1>{V}"),
    (rf"(\*\*Target [Vv]ersion:\*\* ){VER_RE}", r"\g<1>{V}"),
    (rf"(実装状況一覧（){VER_RE}(）)", r"\g<1>{V}\g<2>"),
    (rf"((?:サポート状況|Support Status)（){VER_RE}(）)", r"\g<1>{V}\g<2>"),
    (rf"(### ){VER_RE}( テスト結果| Test Results)", r"\g<1>{V}\g<2>"),
]


def main() -> int:
    check_only = "--check" in sys.argv

    with open(os.path.join(ROOT, "VERSION")) as f:
        version = "v" + f.read().strip()

    changed = []
    for root, _dirs, files in os.walk(TUTORIALS):
        for fn in sorted(files):
            if not fn.endswith(".md"):
                continue
            path = os.path.join(root, fn)
            with open(path) as f:
                original = f.read()
            updated = original
            for pat, repl in BADGE_PATTERNS:
                updated = re.sub(pat, repl.replace("{V}", version), updated)
            if updated != original:
                changed.append(os.path.relpath(path, ROOT))
                if not check_only:
                    with open(path, "w") as f:
                        f.write(updated)

    if check_only:
        if changed:
            print(f"❌ チュートリアルのバージョンバッジが VERSION ({version}) と乖離:")
            for p in changed:
                print(f"   {p}")
            print("   → python3 scripts/update_tutorial_version.py で更新してください")
            return 1
        print(f"✅ チュートリアルのバージョンバッジは VERSION ({version}) と一致")
        return 0

    if changed:
        print(f"✅ {len(changed)}ファイルのバージョンバッジを {version} に更新:")
        for p in changed:
            print(f"   {p}")
        print("注意: jsバックエンドのテスト結果数値は実測値のため手動更新が必要です")
        print("      （tests/unified_test_runner.sh -b js の結果を反映）")
    else:
        print(f"✅ 更新不要（全バッジが {version} と一致）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
