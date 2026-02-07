# エクスポート可能なマクロ宣言

## ステータス
✅ 実装完了（v0.13.0）

---

## 構文

```cm
// オブジェクトマクロ（値の置換）
macro NAME = value;

// エクスポート可能なマクロ
export macro NAME = value;

// 関数マクロ（将来）
macro NAME(params...) { return expr; }
```

---

## 実装詳細

### パーサー変更

1. **`MacroDef` 構造体** (`parser.hpp`)
   - `is_exported` フィールド追加
   
2. **`parse_macro_definition`** (`parser.hpp`)
   - 新構文 `macro NAME = value;` をサポート
   - `export macro` 対応
   
3. **`parse_primary`** (`parser_expr.cpp`)
   - 識別子がマクロ名かチェック
   - `expand_macro()` でトークン展開
   - `parse_expanded_macro_tokens()` でAST生成

4. **パースループ** (`parser.hpp`)
   - `nullptr` 返却時も位置が進んでいればsynchronizeしない

### 廃止された構文

```cm
// 旧構文（廃止）
#macro NAME value;
```

---

## テスト例

```cm
import std::io::println;

macro VERSION = 13;
macro MSG = "hello macro";

int main() {
    println("=== macro test ===");
    println(VERSION);
    println(MSG);
    println("=== complete ===");
    return 0;
}
```

出力:
```
=== macro test ===
13
hello macro
=== complete ===
```

---

## 今後の拡張

1. **関数マクロ**: `macro add(a, b) { return a + b; }`
2. **モジュール間エクスポート**: `export macro` のモジュールシステム連携
3. **条件付きマクロ**: コンパイル時条件分岐
