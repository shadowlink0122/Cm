---
description: セキュリティチェック - ローカルパス情報の検出と削除
---

# セキュリティチェック

## 目的
コードやドキュメントにローカル環境の情報（ユーザーパス等）が含まれていないことを確認する。

## ⚠️ 重要：ローカルパス情報は絶対禁止

以下のようなパスは**絶対に**コミットしてはいけない：
- `/Users/<username>/...`
- `/home/<username>/...`
- `C:\Users\<username>\...`
- その他のマシン固有のパス

## チェックコマンド

// turbo
1. ローカルパスの検索（docs, .agent）
```bash
grep -rn "/Users/\|/home/\|C:\\\\Users\\\\" docs/ .agent/ --include="*.md" --include="*.txt" --include="*.yaml"
```

// turbo
2. 結果が空であることを確認

## 修正方法

ローカルパスが見つかった場合：
```bash
# macOS/Linux
find docs -type f -name "*.md" -exec sed -i '' 's|/Users/<username>/Documents/git/<project>/||g' {} \;

# または相対パスに変換
# 例: `/Users/user/project/src/foo.cpp` → `src/foo.cpp`
```

## 正しいパス形式

- ✅ `src/codegen/llvm/mir_to_llvm.cpp`
- ✅ `docs/design/architecture.md`
- ✅ `tests/test_programs/types/union_array.cm`
- ❌ `/Users/shadowlink/Documents/git/Cm/src/...`
- ❌ `/home/user/project/docs/...`

## コミット前チェック

このチェックは以下のタイミングで必ず実行：
1. PR作成前
2. ドキュメント編集後
3. 新規ファイル追加時
