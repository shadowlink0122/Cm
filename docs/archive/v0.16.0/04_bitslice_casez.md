# v0.16.0 実装設計 4: ビットスライス構文と casez / unique case

優先度: 4〜5関連: [roadmap.md](../../design/v0.16.0/roadmap.html) A4/B2/B3

> **✅ 2026-07-07 4.1実装済み**: ビットスライスは既存のPython風スライス構文
> （SliceExpr）を流用し、オブジェクトが bit[N]/整数型のときにSV流解釈
> （降順・両端含む）とする方式で実装。`+:` は新トークン。
> 脱糖はHIRレベルのシフト+マスクで全バックエンド統一
> （SVへの `[hi:lo]` 直接出力は将来最適化）。
> 制限: 範囲・幅は整数リテラルのみ・最大64bit。
> **✅ 2026-07-07 4.2実装済み**: `0b1?00` don't careリテラル
> （matchパターン専用、(x&mask)==value のif-elseチェーンへ脱糖）と
> SVの `unique case` 出力（match網羅性・switch default生成を根拠に常時付与）。
> casezの**直接出力**のみ将来最適化として残す
> （現状のif-else脱糖でも意味論・合成とも正しい）。

## 4.1 ビットスライス構文

### 目標

`bit[N]` 型の部分ビットの読み書きをユーザー構文として提供する（現在は内部生成のみで、ユーザーは1ビットずつのインデックスしか書けない）。

```cm
bit[16] word = 0;
bit[8] hi = word[15:8];      // 固定範囲（降順、SVと同じ向き）
bit[8] lo = word[7:0];
word[11:4] = 0xFF;           // 部分代入

uint i = 2;
bit[4] nib = word[i*4 +: 4]; // 可変базы+幅（インデックスドパートセレクト）
```

### 設計方針

- **構文**: `expr[hi:lo]`（定数範囲）と `expr[base +: width]`（widthは定数）。パーサはIndexExprを拡張し `RangeIndex { hi, lo }` / `PartSelect { base, width }` を追加
- **型**: 結果型は `bit[hi-lo+1]` / `bit[width]`（コンパイル時に幅確定）。幅不一致の代入は既存の幅検査に乗せる
- **他バックエンド**: LLVM/JS/interp ではシフト+マスクに脱糖（`(word >> lo) & ((1 << w) - 1)`、代入は read-modify-write）。これによりビット操作コードが全バックエンドで共有可能になる
- **SV出力**: そのまま `[hi:lo]` / `[base +: W]` を出力

## 4.2 casez / unique case

### 目標

match式のワイルドカード・網羅性情報をSVの検証機能へ写像する。

```cm
match (opcode) {
    0b1?00 => { ... }   // ? = don't care（新リテラル構文）
    0b0000 => { ... }
    _ => { ... }
}
```

```systemverilog
unique casez (opcode)
    4'b1?00: begin ... end
    4'b0000: begin ... end
    default: begin ... end
endcase
```

### 設計方針

- **`?` 入り2進リテラル**: `0b1?00` を新リテラル種（value+maskペア）として字句解析に追加。match のパターン位置でのみ許可
- 実行系バックエンドでは `(x & mask) == value` に脱糖（全バックエンド共通）
- **unique**: match式は網羅性・重複検査済みのため、SV出力では常に`unique case` / `unique casez` を出せる（シミュレーション時の重複ヒット検出が無償で手に入る）
- `priority case` は Cm に fallthrough 概念がないため出力しない

## テスト

- ビットスライス: 全実行系バックエンドで同値性テスト（cross_backend_semantics方式）
  + SVゴールデン + lint
- casez: デコーダFSMのシミュレーション一致テスト
