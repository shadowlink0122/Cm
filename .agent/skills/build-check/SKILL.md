---
name: build-check
description: ビルドとテストの完全チェック
---

# ビルド・テスト完全チェック

ビルドからテストまでの完全な検証を行うスキルです。

## 実行手順

### 1. クリーンビルド
```bash
rm -rf build && make build
```

### 2. インタプリタテスト
```bash
make tip
```

### 3. LLVMネイティブテスト
```bash
make tlp
```

### 4. 結果確認
- 全テストがPASSであること
- FAILEDが0であること
- 新しいSKIPが増えていないこと

## チェックリスト
- [ ] ビルドエラーなし
- [ ] コンパイラ警告確認
- [ ] テスト全パス
- [ ] 回帰なし

## トラブルシューティング
- ビルドエラー: `cmake -B build -DCMAKE_BUILD_TYPE=Debug` でデバッグビルド
- テスト失敗: `./cm compile <test.cm> --debug` でデバッグ
