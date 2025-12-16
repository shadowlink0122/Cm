# Pending Tests

このディレクトリには、まだLLVMバックエンドでの動作に問題があるテストファイルを保存しています。

## Option<T>型とResult<T, E>型

### 問題
ジェネリック関数の戻り値がジェネリック構造体（例：`Option<T>`）の場合、モノモーフィゼーションが正しく行われない。

### 症状
```
LLVM コード生成エラー: LLVM module verification failed:
Cannot allocate unsized type
  %local_3 = alloca %Option__T, align 8
```

### 状態
- インタプリタでは正常動作
- LLVMバックエンドでは失敗

### テスト方法（インタプリタ）
```bash
./cm run tests/pending/option_type.cm
./cm run tests/pending/result_type.cm
```

### 修正予定
v0.7.x または v0.8.0 で修正予定
