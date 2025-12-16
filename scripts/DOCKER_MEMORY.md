# Docker メモリ設定ガイド

## クイックスタート

現在のメモリ: **7.6GB** → 推奨: **12GB以上**

### 設定手順（3分で完了）

#### 1. Docker Desktop を開く
メニューバーの🐳アイコンをクリック → "Settings" を選択

#### 2. Resources → Memory を調整
```
左メニュー: Resources
    ↓
Advanced タブ
    ↓
Memory スライダー: 12 GB に設定
    ↓
Apply & Restart ボタンをクリック
```

#### 3. 確認
```bash
docker system info | grep Memory
# 期待: Total Memory: 12GiB
```

## 視覚的な設定場所

```
Docker Desktop
├── Settings (⚙️)
    ├── Resources
        ├── Advanced
            ├── CPUs: 8 ←そのまま
            ├── Memory: 12 GB ←ここを変更！
            ├── Swap: 2 GB ←任意
            └── [Apply & Restart] ←クリック
```

## 設定後のテスト

### 簡単な確認
```bash
# メモリ設定が反映されているか確認
docker run --rm ubuntu free -h
```

### CI環境テストの再実行
```bash
./scripts/test-in-docker.sh
```

## よくある質問

### Q: システムメモリが16GBしかない場合は？
**A:** 12GBに設定してください。システムに4GB残るので問題ありません。

### Q: 設定を変更したのにエラーが出る
**A:** 以下を試してください：
```bash
# 1. Dockerをクリーンアップ
docker system prune -a

# 2. Docker Desktopを再起動
# メニューバー → Quit Docker Desktop
# 再度Docker Desktopを起動

# 3. 確認
docker system info | grep Memory
```

### Q: それでもOOMエラーが出る
**A:** カテゴリごとに分割実行：
```bash
# basicカテゴリのみテスト
docker run --rm -v "$PWD:/workspace" -w /workspace cm-ci-test \
    bash -c "tests/unified_test_runner.sh -b llvm -c basic"
```

## トラブルシューティング

### エラー: "Cannot allocate memory"
```bash
# スワップも含めてメモリを確保
docker run --rm \
    --memory="10g" \
    --memory-swap="14g" \
    -v "$PWD:/workspace" \
    -w /workspace \
    cm-ci-test bash -c "make test-llvm"
```

### Docker Desktop が起動しない
1. Docker Desktop を完全終了
2. `~/Library/Containers/com.docker.docker` を削除（設定リセット）
3. Docker Desktop を再インストール

## 参考情報

### 現在の設定値
- CPUs: 8
- Memory: 7.654 GiB (**不足**)
- 推奨: 12 GiB 以上

### メモリ使用量の目安
- ビルド: 2-3 GB
- Unit Tests: 1 GB
- Interpreter Tests: 1-2 GB
- LLVM Tests: 3-5 GB per test（シリアル実行）
- 合計: 8-12 GB

### システムリソースの確認
```bash
# macOS のメモリ確認
sysctl hw.memsize | awk '{print $2/1024/1024/1024 " GB"}'

# Docker に割り当て可能なメモリ
# システムメモリの 75% まで推奨
```

## まとめ

**推奨設定:**
- Memory: 12 GB（最小10GB）
- Swap: 2 GB
- CPUs: そのまま（8）

**設定後:**
```bash
./scripts/test-in-docker.sh
```

これで全テストが正常に完了するはずです！✨
