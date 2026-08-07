# R19: TS出力がlong/ulongフィールドへnumberリテラルを代入しtscを通らない

**ステータス:** 未修正（バックエンド網羅バグ調査で検出）
**重大度:** Medium（TS第一級保証「生成TSがtscを型エラーなく通る」に違反。実行値は正）

## 症状（実測: cm 0.17.0、プローブ `.tmp/bughunt7/{ts,verify}/`）

long/ulong型のstructフィールドへ、JS number精度に収まる小さいリテラルを代入すると、`bigint`フィールドへ`number`リテラルを直接代入するTSを生成する。tscでは`Type 'number' is not assignable to type 'bigint'`（TS2322）になる。

```cm
struct Big { ulong v; long w; }
int main() { Big b = { v: 5, w: 7 }; println("{b.v} {b.w}"); return 0; }
```
生成TS（該当部）:
```ts
export interface Big {
    v: bigint;
    w: bigint;
}
// ...
_t1000_2.v = 5;   // ← TS2322: number を bigint フィールドへ
_t1000_2.w = 7;
```
`cm run --target=ts`の実行値は`5 7`で正しい（JSに型強制がないため実行は通る）。しかしリリースノートが謳う「生成TSが`tsc --noEmit`を型エラーなく通る」第一級保証に反する。

真因の見立て: 整数リテラルのローワリングが、bigintコンテキスト（long/ulongフィールドへの代入）でも「JS number精度に収まる値は`number`で出力」する。閾値超（>2^53程度）は`bigint`リテラル（`9000000000000000000n`）化されるため、値の大小で型健全性が変わる。なお`long five() { return 5; }`の戻り値経路は現行バイナリでは`5n`（bigintリテラル）を正しく生成しており、この欠陥は**フィールド代入サイト固有**（戻り値・let初期化サイトは修正済み）。

補足（軽微・未確定）: `typedef Value = int | string`等のunion typedefがTSの`number | string`ユニオン型でなく`any`へ退化する型注釈品質ギャップの疑いがあるが、バックエンド網羅調査のプローブでは確定に至らず（要追検証）。

## 修正方針

TSコード生成の整数リテラル出力で、代入先・格納先の型がlong/ulong（bigint）なら値の大小に関わらず`bigint`リテラル（`5n`）を出す。戻り値・let初期化サイトで実施済みの判定を、structフィールド代入サイト（フィールドの宣言型がbigintか）にも適用する。理想は「期待型がbigintの整数リテラルは常にbigintリテラル化」を単一のローワリングヘルパへ集約する（サイトごとの取りこぼしを防ぐ）。

## テスト計画

`tests/ts/cases/`へ: long/ulongのフィールド代入・配列要素・関数引数・複合代入で生成TSがbigintリテラルを使うことの検証（生成物のgrepまたはtsc導入後の型検査）。実行値のts/js/jit一致も併せて固定。
