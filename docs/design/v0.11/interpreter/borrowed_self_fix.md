# インタプリタにおけるBorrowed Selfフィールドアクセス修正設計

## 概要
本文書は、MIRインタプリタにおいて`self`が借用参照（borrowed reference）である場合に`self.field`へのアクセスが失敗する問題、現在の部分的な修正内容、および完全な解決に向けたロードマップについて記述します。

## 問題点
構造体のメソッドが呼び出される際、通常`self`は参照（`PointerValue`）として渡されます。
- **データ構造**: `PointerValue`は`target_local`を保持しており、これは*呼び出し元*のスタックフレームにおける変数の`LocalId`です。
- **実行フロー**:
    1. 呼び出し元コンテキスト（`Ctx A`）に変数`p`（LocalId: 1、値: `Point { x: 10, y: 20 }`）が存在。
    2. 呼び出し元が`PointerValue { target_local: 1 }`を作成。
    3. 呼び出し元がメソッド`debug(self: &Point)`を実行。
    4. インタプリタが`debug`用の新しいコンテキスト（`Ctx B`）を作成。
    5. `PointerValue`が`Ctx B`に引数`self`として渡される。
    6. `debug`内で`self.x`にアクセスしようとすると、`self`のデリファレンスが発生。
    7. **失敗**: インタプリタは`Ctx B`内で`target_local: 1`を解決しようとする。しかし、`Ctx B`におけるLocalId 1は存在しないか、その関数内の全く別のローカル変数を指しているため、正しい値が取得できない。

## 現在の部分的な修正（実装済み）
読み取り専用アクセス（`Debug`や`Display`の実装には十分）をサポートするため、`embedded_value`メカニズムを導入しました。

### 主な変更点
1.  **`PointerValue`の拡張**: `std::shared_ptr<Value> embedded_value`を追加。
2.  **評価ロジック**:
    - `MirRvalue::Ref`が評価される際、参照先の値の**スナップショット（コピー）**を作成し、`embedded_value`に格納。
    - `MirPlace`（Deref）が評価される際、`embedded_value`を確認。存在する場合、`target_local`のルックアップを行わずにその値を直接使用する。

### 制約事項
- **スナップショットの意味論**: `embedded_value`は参照作成時点の値の*コピー*である。
- **変更の不反映**: 参照経由で`embedded_value`を変更しても、呼び出し元のコンテキストにある元の変数には**反映されない**。
- **コンストラクタの不具合**: コンストラクタやミュータブルなメソッドはローカルのスナップショットを変更してしまうため、元のオブジェクトは変更されないままとなる。これにより、`basic/constructor_overload`のようなテストが失敗する（変更されたはずの値が空/デフォルト値のままになる）。

## 提案する完全な解決策：外部参照マップ（External Ref Map）
複雑なグローバルヒープやスタックウォーキング機構を実装せずに、関数境界を越えたミュータブルな参照を完全にサポートするため、「外部参照マップ」を渡す方式を提案します。

### 設計
1.  **構造**:
    ```cpp
    // 呼び出し元のLocalIdから、呼び出し元コンテキスト内の実際のValueへのポインタへのマップ
    using ExternalRefMap = std::unordered_map<LocalId, Value*>;
    ```

2.  **インタプリタの更新**:
    - `execute_function`, `execute_call`, `execute_constructor`を更新し、オプションで`ExternalRefMap`を受け取れるようにする。
    - 呼び出しの引数を準備する際：
        - `PointerValue`型の引数を特定。
        - 各`PointerValue`について、現在アクティブな`ctx.locals`から実際の`Value`を検索。
        - マップに`{ result.target_local, &ctx.locals[result.target_local] }`を追加。

3.  **デリファレンスロジック（`Evaluator::load_from_place`）**:
    - `PointerValue`を解決する際：
        1. 現在のコンテキストの`ExternalRefMap`に`target_local`が存在するか確認。
        2. 見つかった場合、マップ内の`Value*`を使用（これは*呼び出し元*の実際のメモリを指す）。
        3. 見つからない場合、通常のローカルルックアップにフォールバック（同関数内のローカル変数への参照の場合）。

### 実装ステップ
1.  **`ExecutionContext`の修正**: メンバに`ExternalRefMap* external_refs = nullptr;`を追加。
2.  **呼び出し箇所の更新**:
    - `execute_call`内で、渡される引数に対してのみ効率的にマップを構築。
3.  **Derefのリファクタリング**:
    - `embedded_value`を削除（実際のポインタがあれば不要になるため）。
    - `load_from_place`を更新し、`external_refs`を参照するように変更。

## 結論
現在の`embedded_value`による修正は、文字列補間での`self.field`読み取りを可能にするための一時的な回避策です。コンストラクタやミュータブルなメソッドを正しく動作させるためには、上記で説明した外部参照マップ（External References Map）の設計を実装する必要があります。
