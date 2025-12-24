# JavaScript Codegen ロードマップ

CmからJavaScriptへのトランスパイラの開発ロードマップです。

## 現状 (Phase 1 完了)

| 機能 | 状態 | 備考 |
|------|------|------|
| 基本的な関数 | ✅ | 線形フローで直接出力 |
| 文字列操作 | ✅ | `len`, `charAt`, `substring`等 |
| 配列・高階関数 | ✅ | `map`, `filter`, `reduce`等 |
| 構造体 | ✅ | オブジェクトリテラルで生成 |
| ジェネリクス | ✅ | モノモーフィズムで対応 |
| インターフェース | ⚠️ | 静的ディスパッチのみ |
| ループ | ⚠️ | switch/dispatchパターン |
| if/else | ⚠️ | switch/dispatchパターン |
| ポインタ | ❌ | JSでは不要・未対応 |
| メモリ管理 | ❌ | GCで自動管理 |

## Phase 2: ループとif/else構造の改善 (次のステップ)

### 2.1 while/for文への変換
- [ ] ループ検出（バックエッジ解析）で already 実装済み
- [ ] while文の生成
- [ ] for-in文の生成（配列イテレーション）

### 2.2 if/else構造の再構築
- [ ] 分岐パターンの検出
- [ ] ネストしたif/else生成
- [ ] 条件式の最適化

## Phase 3: スライス・動的配列のサポート

現在不足している組み込み関数:
- [ ] `cm_slice_push_*` - スライスへの追加
- [ ] `cm_slice_pop_*` - スライスからの削除
- [ ] `cm_slice_new_*` - スライスの作成
- [ ] `cm_slice_len_*` - スライスの長さ

## Phase 4: char型の修正

問題: `println("{c}")` でchar型が数値として出力される

修正案:
- [ ] フォーマット文字列でchar引数を検出
- [ ] `String.fromCharCode()` で変換

## テストファイルの状態

### 修正が必要なスキップファイル

| ファイル | 問題 | 優先度 |
|----------|------|--------|
| `types/char_type.skip` | char出力形式 | 高 |
| `string/string_slice.skip` | 出力形式の不一致 | 中 |
| `iterator/iter_closure.skip` | 未調査 | 中 |
| `iterator/iter_map_filter.skip` | 未調査 | 中 |
| `loops/forin_auto.skip` | `cm_slice_push_*`未実装 | 高 |

### 適切にスキップされているファイル

- `pointer/*` - ポインタはJS不要
- `memory/*` - メモリ管理はJS不要
- `ffi/*` - FFIはJS未対応
- `asm/*` - インラインアセンブリはJS未対応
- `casting/*_cast.skip` - ポインタキャスト

## 技術的な改善項目

### 短期
- [ ] char型印刷の修正
- [ ] 不足するスライスbuiltins追加
- [ ] 不要なskipファイル削除

### 中期  
- [ ] ループ構造の生成（Phase 2）
- [ ] 一時変数のインライニング
- [ ] デッドコード除去

### 長期
- [ ] ソースマップ生成
- [ ] ES Modules対応
- [ ] TypeScript型定義出力
