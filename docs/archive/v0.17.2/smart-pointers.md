# スマートポインタ（std::mem::smart — UniquePtr / SharedPtr）

## 目的

ヒープ確保した値の解放をRAIIで自動化するスマートポインタを、コンパイラ変更なしのセルフホストライブラリ（Cm実装）として提供する。
将来のセルフホスティング（AST等の所有権管理）の基盤となる。

## 実現可能性の検証結果（プロトタイプ実測）

- ジェネリックstruct＋`impl<T>`のコンストラクタ/デストラクタ・`__sizeof__(T)`・`std::mem::alloc/dealloc`・スコープ末尾dtorはすべて動作する（`Vector<T>`が同型の先行実装）。
- `move` は元のdtorを無効化して所有権を移す（1回だけ解放される）。関数への `move` 渡し・`return move local;` も正しく動作する。
- **暗黙コピー（`b = a;`）は浅いコピーで両方のdtorが走る**（二重解放）。コピーコンストラクタのフックは存在しない。
- **`return local;` はローカルのdtorが先に走りdanglingになる**。所有型を返す関数は必ず `return move local;` と書く必要がある。
- 単項 `*`/`->` のオーバーロードは不可のため、アクセスは `get()/set()/raw()` メソッドで提供する。
- JS/TSターゲットはポインタ操作禁止（GCのため不要）、SVは対象外。native系（jit/native/wasm/uefi/baremetal）限定モジュールとする。

## API設計

```cm
import std::mem::smart::*;

// UniquePtr<T>: 単独所有。共有不可・移動のみ
UniquePtr<int> u(42);        // 値をヒープへ移して所有
int v = u.get();             // 読み出し（コピー）
u.set(43);                   // 書き換え
int* p = u.raw();            // 借用ビュー（所有権は移動しない）
UniquePtr<int> u2 = move u;  // 所有権の移動（uは無効化）
int* released = u2.release();  // 所有権を放棄（解放は呼び出し側の責任）
u2.reset();                  // 即時解放してnull化

// SharedPtr<T>: 参照カウントによる共有。共有は明示clone（Rustと同じ流儀）
SharedPtr<int> a(42);
SharedPtr<int> b = a.clone();  // rc=2
int n = a.use_count();
// 最後の所有者のdtorでペイロードと制御カウントを解放
```

## 設計判断

- **共有は明示 `clone()`**: 暗黙コピーはコピーフックが無く参照カウントを増やせないため、`=` での共有は二重解放になる。Rustの `Arc::clone` と同じ明示規律とし、チュートリアル・docコメントで「代入は `move` 必須・共有は `clone()` 必須」を明記する。
- **制御カウントは独立ヒープセル（int*）**: ペイロードと別確保のシンプルな構成（weak対応時に制御ブロック構造体へ拡張する）。
- **確保は `std::mem::alloc/dealloc` 経由**: `set_allocator_fns` によるカスタムアロケータ差し替えを透過させる（Vectorと同方針）。
- **デストラクタ内は解放処理をインライン**: dtorからのメソッド呼び出しへの依存を避ける。
- 宣言だけの `UniquePtr<int> u;` はフィールドゼロ初期化（v0.17.0確定）によりnull状態で、dtorは何もしない。

## 既知の制約（言語側の将来課題）

- dtorを持つstructの暗黙コピー（`b = a;`）はコンパイラが検出しない（v0.17.2で警告/エラー化を検討 — selfhosting-stdlib設計参照）。
- `return move` を忘れた所有型の返却はdanglingになる（同上の検出候補）。

## テスト計画

- `tests/common/stdlib/smart/`（`//! platform: !js|sv`）: unique基本（get/set/raw/スコープ解放）、move移動・関数渡し・factory返却、release/reset、shared clone/use_count/段階的解放、構造体ペイロード。
- 解放順・回数はdtor内printlnの出力順で検証する。
