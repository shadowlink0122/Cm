# R12: matchの負数パターン不可・網羅matchのreturn漏れ誤検知

**ステータス:** 修正済み（構文網羅バグ調査で検出）
**重大度:** Medium

## 症状（実測: cm 0.17.0、プローブ `.tmp/bughunt6/syntax1/`）

### バグ1 matchパターンに負数リテラルが書けない

```cm
match (x) {
    -1 => { return "minus one"; }   // 範囲 -5...-1 も同様
    _ => { return "other"; }
}
```
全経路で`Expected match pattern`（行番号なし、rc=1）。範囲どころか単独の負数リテラルすら不可。パターン解析がリテラルトークンを直接判定するため単項マイナスを受けない。負の整数を分類したい実コードが書けない。

### バグ2 全arm returnの網羅的matchに「falls off the end」を誤検知（--strictでビルド阻害）

```cm
string f(int x) {
    match (x) {
        1 => { return "one"; }
        _ => { return "other"; }
    }
}
```
全armがreturnし`_`で網羅済みだが、`cm check`が`warning: non-void function 'f' has a path that falls off the end without returning a value`、**`--strict`ではerrorに昇格しrc=1**。jit/native/wasm/js/tsの実行は正常。H6/L4で導入した確定代入・return網羅検査（definite-assignment-and-correctness-lints）のフロー解析が、match文を「必ずいずれかのarmでreturnする」と認識できていない。

### 健全だった点

`...`（三点）範囲パターン（`1...5`）は両端含む・先勝ち・char対応・網羅性強制（`_`なしはNon-exhaustiveエラー）が9実行経路で一致。範囲は`..`ではなく`...`である点に注意。

## 修正方針

- バグ1: matchパターンパーサへ単項マイナス付き整数リテラル（および負数範囲`-5...-1`）を受理する処理を追加。
- バグ2: return網羅検査のCFGへmatch文を追加し、「全armがreturnし、かつ網羅的（`_`ありまたは全variant被覆）なmatchは値を返す」と認識させる。switch文が既に正しく扱えているなら同じ経路へmatchを載せる。

## テスト計画

- `tests/common/match/`へ: 負数パターン・負数範囲パターンの値一致（全バックエンド）。
- return網羅検査の回帰: 全arm returnの網羅matchが`--strict`でも警告ゼロ・非網羅や一部armがreturnしないmatchは正しく警告、をエラーテストで固定。
## 実装記録（2026-08-08）

2件とも修正した。

- バグ1: matchパターンパーサ（`parse_match_pattern_element`）で単項マイナス付き数値リテラルを受理するようにした（単独`-1`と範囲端`-10...-5`の両方）。下流の網羅性検査がLiteralExprの`int64_t`を直読みし、HIR loweringも式を直接lowerするため、UnaryExprでラップせず値を符号反転したLiteralExprへ畳み込む表現を選んだ（下流変更ゼロ）。
- バグ2: return網羅解析（`stmt_terminates`）へmatch文（ExprStmt内のMatchExpr）を追加した。網羅性は2系統で判定する: (1) ASTフォールバック＝ガード無しのワイルドカード/変数束縛armの存在、(2) `MatchExpr::known_exhaustive`フラグ＝網羅性検査（`check_match_exhaustiveness`）が全ケース被覆を確定した時点でセットする（enum全variant被覆で`_`が無いmatchもこれで認識される。falls-off解析は本体チェック後に走るため順序は安全）。全armがブロック形式で終端する場合のみ終端とみなす（式形式armは関数からreturnしないため対象外）。

テスト: `tests/common/match/negative_pattern.cm`（-1・-10...-5・境界-4の値一致、全バックエンド）、`tests/common/match/exhaustive_return.cm`（ワイルドカード網羅とenum全variant被覆の両方が--strictで警告ゼロを手動確認、実行値も固定）。

残課題: Orパターン内の負数（`-1 | -2`）はOrパターン経路が同じ関数を再帰するため動作するはずだが個別テストは未追加。範囲パターンの網羅性検査（`-5...-1`で全域被覆の認識）は従来からスキップされており本修正の対象外。
