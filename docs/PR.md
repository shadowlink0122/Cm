[English](PR.en.html)

# v0.14.0 Release - Cm言語コンパイラ

## 概要

v0.14.0は**JavaScriptバックエンドの大規模改善**、**Enum Associated Data修正**、**Tagged Union構造体ペイロード修正**に焦点を当てたリリースです。JSテスト通過率が55%から77%に向上し、NativeとJSの出力一致性が大幅に改善されました。

## 🎯 主要な変更

### 1. JSバックエンド大規模リファクタリング

JSコードジェネレータを大幅にリファクタリングし、1,600行以上の不要コードを削除しました。

| 変更 | 詳細 |
|------|------|
| codegen.cpp | -1,618行（大規模整理） |
| emit_expressions.cpp | +124行（式出力改善） |
| emit_statements.cpp | +80行（文出力改善） |
| builtins.hpp | +71行（ビルトイン拡充） |

#### JSテスト通過率

| バージョン | パス | 失敗 | スキップ | 通過率 |
|-----------|------|------|---------|--------|
| v0.13.1 | 206 | 119 | 47 | 55% |
| **v0.14.0** | **285** | **0** | **87** | **77%** |

#### JSコンパイルの使い方

```bash
./cm compile --target=js hello.cm
node output.js
```

### 2. Enum Associated Dataのprintln出力修正

match armのペイロード変数を`println`で出力する際のランタイムハングを修正。

```cm
enum Message {
    Quit,
    Write(string)
}

int main() {
    Message m = Message::Write("Hello");
    match (m) {
        Message::Write(t) => {
            println(t);  // v0.13.1: ハング → v0.14.0: "Hello"
        }
        _ => {}
    }
    return 0;
}
```

### 3. Tagged Union構造体ペイロードサイズ修正

3フィールド以上の構造体ペイロードの破損を修正。

```cm
struct RGB { int r; int g; int b; }
enum Color { None, Set(RGB) }

// v0.13.1: Set(RGB{255, 128, 0}) → b=1に破損
// v0.14.0: Set(RGB{255, 128, 0}) → b=0（正常）
```

---

## 🐛 バグ修正

| 問題 | 原因 | 修正ファイル |
|-----|------|------------|
| println型判定の誤り | AST型チェッカーがmatch armのpayload変数の型を`int`に設定 | `expr_call.cpp` |
| ペイロードロードエラー | Tagged Unionの非構造体ペイロード型が`i32`にハードコード | `mir_to_llvm.cpp` |
| 構造体ペイロードサイズ計算 | `max_payload_size()`がStruct型をデフォルト8バイトで計算 | `types.cpp` |

---

## 🔧 ビルド・テスト改善

### JSスキップファイル整理

| カテゴリ | 理由 |
|---------|------|
| `asm/` | インラインアセンブリはJS非対応 |
| `io/` | ファイルI/OはJS非対応 |
| `net/` | TCP/HTTPはJS非対応 |
| `sync/` | Mutex/Channel/AtomicはJS非対応 |
| `thread/` | スレッドはJS非対応 |

---

## 📁 変更ファイル一覧

### JSバックエンド

| ファイル | 変更内容 |
|---------|---------| 
| `src/codegen/js/codegen.cpp` | 大規模リファクタリング（-1,618行） |
| `src/codegen/js/emit_expressions.cpp` | 式出力改善 |
| `src/codegen/js/emit_statements.cpp` | 文出力改善 |
| `src/codegen/js/builtins.hpp` | ビルトイン関数拡充 |
| `src/codegen/js/runtime.hpp` | ランタイムヘルパー追加 |
| `src/codegen/js/types.hpp` | 型マッピング改善 |

### LLVMバックエンド/MIR修正

| ファイル | 変更内容 |
|---------|---------|
| `src/codegen/llvm/core/types.cpp` | Tagged Unionペイロードサイズ計算修正 |
| `src/codegen/llvm/core/mir_to_llvm.cpp` | ペイロードロード修正 |
| `src/mir/lowering/expr_call.cpp` | println型判定修正 |
| `src/mir/lowering/impl.cpp` | impl lowering改善 |

### テスト

| ファイル | 変更内容 |
|---------|---------|
| `tests/test_programs/enum/associated_data.*` | .error → .expected |
| `tests/test_programs/asm/.skip` 等 | JSスキップファイル追加 |

---

## 🧪 テスト状況

| バックエンド | 通過 | 失敗 | スキップ |
|------------|-----|------|---------|
| JIT (O0) | 363 | 0 | 9 |
| JavaScript | 285 | 0 | 87 |

---

## 📊 統計

- **テスト総数**: 372
- **JITテスト通過**: 363（0失敗）
- **JSテスト通過**: 285（0失敗、+79改善）
- **変更ファイル数**: 27
- **追加行数**: +468
- **削除行数**: -1,708

---

## ✅ チェックリスト

- [x] `make tip` 全テスト通過（363 PASS / 0 FAIL）
- [x] `make tjp` 全テスト通過（285 PASS / 0 FAIL）
- [x] リリースノート更新（`docs/releases/v0.14.0.md`）
- [x] JSチュートリアル追加（`docs/tutorials/ja/compiler/js-compilation.md`）
- [x] ローカルパス情報なし

---

**リリース日**: 2026年2月10日
**バージョン**: v0.14.0