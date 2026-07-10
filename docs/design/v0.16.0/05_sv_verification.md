# v0.16.0 実装設計 5: SV回路検証フレームワーク

優先度: 追加（ユーザー要望 2026-07-07）
目的: **回路の正しさを有効に検証する仕組み**を段階的に整備する。

> [!NOTE]
> 本設計の `#[sv::testbench]` 属性は設計06でバックエンド中立の `#[test]` へ
> 再設計された（テストビルド限定コンパイル + `cm test` コマンド）。
> 本文書のTB生成機構（step/assert/println変換）はそのまま `#[test]` が引き継ぐ。
> 詳細は [06_test_attribute.md](06_test_attribute.md) を参照。

## 現状の検証手段と限界

| 手段 | できること | 限界 |
|---|---|---|
| `//! test: in=..., cycles=N -> out=...` | 単発ベクタの入力→待機→比較 | 系列刺激・プロトコル・途中状態の検証ができない |
| `std::debug::assert` → 即時アサーション | 常時成立すべき性質の監視 | 刺激を書けない |
| verilatorリント / iverilogシミュレーション | 構文・実行 | 期待値はexpectファイル頼み |

## 3層アーキテクチャ

### Layer 1: Cmテストベンチ関数（本設計で実装）

`#[sv::testbench]` を付けた関数を**Cmで書けるサイクル精度テストベンチ**として
SVのTBモジュール（initialブロック）へ変換する:

```cm
#[sv::testbench]
void tb() {
    din = 5;
    step(1);                      // 1クロック進める
    assert(dout == 5, "pass1");   // PASS/FAIL表示 + 失敗時 $fatal
    din = 7;
    step(2);
    assert(dout == 7, "pass2");
    println("all done");          // $display
}
```

- **`step(n)`**: SVでは `repeat (n) @(posedge clk); #1;` に変換
  （NBA確定後に観測するため1タイムユニット遅延を挿入）
- **`assert(cond, msg)`**: 成立時 `PASS: msg`、不成立時 `FAIL: msg` を表示し
  `$fatal` で終了（シミュレーション exit code ≠ 0 → テストランナーが失敗検出）
- **`println("...")`**: `$display` に変換（文字列リテラルのみ）
- 代入文はブロッキング代入としてDUT入力ポートを駆動
- `//! test:` ベクタとの関係: testbench関数があればそちらを優先
  （両方書いた場合はtestbench関数のみ使用）
- 実行系バックエンドでは呼ばれない限りDCEで除去される（SV専用機能）。
  `step` はSVテストベンチ文脈専用の組み込み

**実装**: MirFunctionにHIR文ポインタを保持（MirInitialBlockと同じ寿命モデル）し、
generateTestbenchがベクタ刺激の代わりにHIR文列を変換して出力する。

### Layer 2: JITゴールデンモデル照合（設計のみ・次段階）

**同一Cmソースを実行系バックエンドで動かして期待値を得る**差分検証。
Cmならではの強み: RTLとリファレンスモデルが同一ソースなので、
期待値を人手で書かずに「SVシミュレーション ⇔ JIT実行」の一致を機械検証できる。

```
cm verify --target=sv design.cm
  1. testbench関数の刺激列を抽出
  2. JIT: モジュール状態（グローバル）を保持したままprocess関数を
     ステップ実行し、各stepの出力を記録（ゴールデン）
  3. iverilog: 生成SV+TBを実行し各stepの出力を記録
  4. サイクル毎に突き合わせ、相違をサイクル番号・信号名付きで報告
```

**既知の課題（要設計）**: SVのノンブロッキング代入（<=、右辺は旧値）と
ソフトウェア逐次実行の意味論差。単一alwaysブロックの典型設計では一致するが、
複数ブロック間の同一サイクル読み書きで乖離しうる。対応案:
プロセス実行前に状態スナップショットを取り、読みはスナップショット・
書きはコミットバッファへ、とするダブルバッファ実行モード（MIRインタプリタ拡張）。

### Layer 3: 性質検証（将来）

- `assert property`（SVA時相アサーション）: クロック付き性質の宣言
- カバレッジ計測（casez/分岐到達）

## テスト計画（Layer 1）

- `tests/sv/simulation/tb_function.cm`: 系列刺激（値変更→step→assert×複数）を
  含むtestbench関数で、PASS行と完走をexpect検証
- 失敗系: assert不成立時に exit≠0 になること（.errorテスト）
