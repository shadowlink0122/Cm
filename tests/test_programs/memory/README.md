# Memory Tests

このディレクトリにはメモリアドレスやポインタ関連のテストが含まれています。

## expectファイルについて

これらのテストは実行時のメモリアドレスを表示するため、実行結果が非決定的です。
そのため、expectファイルは意図的に作成していません。

自動テストランナーは、expectファイルがないテストをスキップします。

## テストファイル

- `address_interpolation.cm` - アドレス補間機能のテスト（`{&variable}` 構文）

## 手動テスト方法

```bash
# インタープリタで実行
./cm run tests/test_programs/memory/address_interpolation.cm

# LLVMバックエンドで実行
./cm compile tests/test_programs/memory/address_interpolation.cm -o test_addr
./test_addr
```