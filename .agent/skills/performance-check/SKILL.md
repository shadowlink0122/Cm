---
name: performance-check
description: パフォーマンステストと最適化確認
---

# パフォーマンスチェック

コンパイラとランタイムのパフォーマンスを確認するスキルです。

## ベンチマーク実行

### コンパイル時間
```bash
time ./cm compile <file.cm>
```

### 実行時間
```bash
time ./a.out
```

### 比較テスト
```bash
# Python比較
time python3 <file.py>

# C++比較
time g++ -O2 <file.cpp> && time ./a.out
```

## 最適化レベル確認

### 最適化なし
```bash
./cm compile -O0 <file.cm>
```

### 標準最適化
```bash
./cm compile -O2 <file.cm>
```

### 最大最適化
```bash
./cm compile -O3 <file.cm>
```

## プロファイリング

### macOS
```bash
# Instruments
xcrun xctrace record --template 'Time Profiler' --launch ./a.out
```

### Linux
```bash
# perf
perf record ./a.out
perf report
```

## チェックリスト
- [ ] コンパイル時間が妥当
- [ ] 実行時間が期待値内
- [ ] メモリ使用量が適切
- [ ] 最適化効果が確認できる
