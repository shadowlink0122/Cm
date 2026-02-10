[English](PR.en.html)

# v0.14.0 Release - Cm言語コンパイラ

## 概要

v0.14.0は**JavaScriptバックエンドの大規模改善**、**演算子オーバーロードの設計改善**、**Enum Associated Data修正**、**Tagged Union構造体ペイロード修正**に焦点を当てたリリースです。JSテスト通過率が55%から77%に向上し、演算子システムでは`impl T`構文と全10個の複合代入演算子をサポートしました。

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
| **v0.14.0** | **289** | **0** | **87** | **77%** |

#### JSコンパイルの使い方

```bash
./cm compile --target=js hello.cm
node output.js
```

### 2. 演算子オーバーロード改善

#### `impl T { operator ... }` 構文

演算子を`impl T for InterfaceName`ではなく、直接`impl T { operator ... }`で定義可能になりました。

```cm
struct Vec2 { int x; int y; }

impl Vec2 {
    operator Vec2 +(Vec2 other) {
        return Vec2{x: self.x + other.x, y: self.y + other.y};
    }
}
```

#### 複合代入演算子

二項演算子を定義すると、対応する複合代入演算子が自動的に使えます。

```cm
Vec2 v = Vec2{x: 10, y: 20};
v += Vec2{x: 5, y: 3};   // v = v + Vec2{5, 3} と同等
v -= Vec2{x: 2, y: 1};   // v = v - Vec2{2, 1} と同等
```

**サポートする複合代入演算子**: `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`

#### ビット演算子オーバーロード

`&`, `|`, `^`, `<<`, `>>` の全ビット演算子をオーバーロード可能になりました。

#### interface存在チェック

`impl T for I` の `I` が宣言済みinterfaceでない場合、コンパイルエラーになります。

### 3. Enum Associated Dataのprintln出力修正

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

### 4. Tagged Union構造体ペイロードサイズ修正

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

### 型チェッカー/パーサー

| ファイル | 変更内容 |
|---------|---------|
| `src/frontend/parser/parser.hpp` | `impl T { operator ... }` 構文パース対応 |
| `src/frontend/types/checking/expr.cpp` | 複合代入演算子の構造体オーバーロード対応 |
| `src/frontend/types/checking/decl.cpp` | interface存在チェック・operator自動登録 |

### チュートリアル・ドキュメント

| ファイル | 変更内容 |
|---------|---------|
| `docs/tutorials/ja/advanced/operators.md` | 演算子チュートリアル全面改訂 |
| `docs/tutorials/en/advanced/operators.md` | 英語版演算子チュートリアル全面改訂 |
| `docs/tutorials/ja/basics/operators.md` | ビット演算子チュートリアル追加 |
| `docs/releases/v0.14.0.md` | リリースノート更新 |

### テスト

| ファイル | 変更内容 |
|---------|---------|
| `tests/test_programs/interface/operator_comprehensive.*` | 全演算子オーバーロードテスト |
| `tests/test_programs/interface/operator_add.*` | impl T構文テスト |
| `tests/test_programs/enum/associated_data.*` | .error → .expected |
| `tests/test_programs/asm/.skip` 等 | JSスキップファイル追加 |

---

## 🧪 テスト状況

| バックエンド | 通過 | 失敗 | スキップ |
|------------|-----|------|---------|
| JIT (O0) | 368 | 0 | 8 |
| LLVM Native | 368 | 0 | 8 |
| JavaScript | 289 | 0 | 87 |

---

## 📊 統計

- **テスト総数**: 376
- **JIT/LLVMテスト通過**: 368（0失敗）
- **JSテスト通過**: 289（0失敗、+83改善）
- **変更ファイル数**: 98
- **追加行数**: +5,373
- **削除行数**: -2,473

---

## ✅ チェックリスト

- [x] `make tip` 全テスト通過（368 PASS / 0 FAIL）
- [x] `make tlp` 全テスト通過（368 PASS / 0 FAIL）
- [x] `make tjp` 全テスト通過（289 PASS / 0 FAIL）
- [x] リリースノート更新（`docs/releases/v0.14.0.md`）
- [x] チュートリアル更新（演算子オーバーロード ja/en）
- [x] JSチュートリアル追加（`docs/tutorials/ja/compiler/js-compilation.md`）
- [x] ローカルパス情報なし

---

**リリース日**: 2026年2月10日
**バージョン**: v0.14.0