# v0.7.0 テストカバレッジと残課題

## ジェネリクステストカバレッジ

### 実装済み（4テスト）
1. ✅ **basic_generics** - 基本的なジェネリック関数
2. ✅ **generic_constraints** - 型制約（パースのみ）
3. ✅ **generic_with_interface** - インターフェース型引数
4. ✅ **nested_generics** - ネストしたジェネリクス

### 保留中（tests/pending/）- v0.8.0で修正予定
- ⚠️ **generic_with_struct** - ジェネリック構造体メソッド（LLVM関数署名問題）
- ⚠️ **impl_generics** - impl<T>ジェネリクス（LLVM関数署名問題）
- ⚠️ **multiple_type_params** - 複数型パラメータ（GEPエラー）
- ⚠️ **option_type** - Option<T>型（戻り値型の特殊化問題）
- ⚠️ **result_type** - Result<T,E>型（同上）
- ⚠️ **simple_pair** - 単純なPair<T,U>テスト（ジェネリック関数との組み合わせ問題）

## 既知の問題とv0.8.0での修正計画

### 1. impl<T>のLLVM関数署名問題

**問題**:
```
Call parameter type does not match function signature!
  call void @Container__int__print(%Container__int* %local_3)
Function arguments must have first-class types!
void %arg0
```

**原因**: モノモーフィゼーションで関数パラメータの型置換が不完全

**影響**: impl<T>メソッドがLLVMでコンパイルできない

**回避策**: インタプリタでは正常動作

**修正予定**: v0.8.0で関数パラメータ型の置換処理を追加

### 2. 複数型パラメータとジェネリック関数

**問題**:
```
Assertion failed: (Ty && "Invalid GetElementPtrInst indices for type!")
```

**原因**: ジェネリック関数が複数型パラメータ構造体を返す場合のLLVM GEP命令生成エラー

**影響**: `<T, U> Pair<T, U> make_pair(T, U)`のような関数が動作しない

**回避策**: 構造体の直接操作（フィールド代入）は動作

**修正予定**: v0.8.0でGEP命令生成ロジックの修正

### 3. 型制約チェックの実行時検証

**現状**: パースのみ実装、実行時チェックなし

**必要な実装**:
1. インターフェース実装の登録機構
2. 呼び出し時の制約検証
3. 適切なエラーメッセージ

**修正予定**: v0.8.0で完全実装

## v0.7.0で完全動作する機能

### ジェネリクス
- ✅ 単一型パラメータのジェネリック関数
- ✅ 単一型パラメータのジェネリック構造体
- ✅ ネストしたジェネリクス（Option<Option<T>>）
- ✅ インターフェースとジェネリクスの組み合わせ
- ✅ 型推論
- ✅ モノモーフィゼーション

### WASM
- ✅ 基本的なプログラムのコンパイル
- ✅ ジェネリクスのWASMサポート
- ✅ 文字列フォーマット
- ✅ wasmtimeでの実行

### 複数型パラメータ（部分的）
- ✅ 構造体定義（`Pair<T, U>`）
- ✅ フィールドアクセス
- ✅ 異なる型の組み合わせ（`Pair<int, string>`）
- ✅ モノモーフィゼーション（Pair__int__string）

## テスト結果

### LLVMテスト
- **88/88** 全てパス
- ジェネリクステスト: 4つ
- 保留テスト: 6つ（v0.8.0で修正予定）

### WASMテスト
- ✅ hello_wasm - 基本的なプログラム
- ✅ basic_generics - ジェネリクス

## 推奨アクション

### v0.8.0で必須
1. **impl<T>の関数署名修正** - LLVMコンパイルを可能に
2. **複数型パラメータ+ジェネリック関数** - GEPエラーの解決
3. **型制約チェックの完全実装**

### v1.0.0で対応
1. Option/Result型のunion型ベース実装
2. match式
3. ?演算子
