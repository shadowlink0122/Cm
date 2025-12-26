# Cm言語 実装済み機能一覧

*最終更新: 2025年1月 (v0.10.0)*

## ✅ 実装済み機能

### 型システム

| 機能 | 状態 | 備考 |
|------|------|------|
| 基本型 (int, uint, float, double, bool, char, string) | ✅ | |
| 符号なし整数 (u8, u16, u32, u64) | ✅ | |
| 符号付き整数 (i8, i16, i32, i64) | ✅ | |
| ufloat, udouble | ✅ | 非負制約付き浮動小数点 |
| ポインタ (T*) | ✅ | C互換 |
| 配列 (T[N]) | ✅ | 固定長 |
| 構造体 | ✅ | ネスト、配列メンバ対応 |
| enum | ✅ | 負の値、オートインクリメント |
| typedef | ✅ | エイリアス、リテラル型、ユニオン型 |
| const/static | ✅ | |
| auto型推論 | ✅ | |

### インターフェース・メソッド

| 機能 | 状態 | 備考 |
|------|------|------|
| interface定義 | ✅ | |
| impl Interface for Type | ✅ | |
| selfキーワード | ✅ | |
| ジェネリックインターフェース | ✅ | `interface Eq<T>` |
| ジェネリックimpl | ✅ | `impl<T> Container<T>` |
| with自動実装 | ✅ | Eq, Ord, Clone, Hash |
| operator定義 | ✅ | `operator bool ==(T other)` |
| privateメソッド | ✅ | |

### ジェネリクス

| 機能 | 状態 | 備考 |
|------|------|------|
| 関数ジェネリクス | ✅ | `<T> T max(T a, T b)` |
| 構造体ジェネリクス | ✅ | `struct Pair<T, U>` |
| 型推論 | ✅ | 呼び出し時 |
| モノモーフィゼーション | ✅ | 全バックエンド |
| 型制約 | ✅ | `<T: Ord>` |
| where句 | ✅ | |

### 制御構造

| 機能 | 状態 | 備考 |
|------|------|------|
| if/else | ✅ | |
| while/for | ✅ | |
| for-in | ✅ | 配列イテレーション |
| switch | ✅ | 整数、char、enum |
| match | ✅ | リテラル、enum、ワイルドカード |
| defer | ✅ | ブロック終了時実行 |
| break/continue | ✅ | |

### 配列・文字列

| 機能 | 状態 | 備考 |
|------|------|------|
| 配列宣言 `T[N] arr` | ✅ | |
| 配列リテラル `[1, 2, 3]` | ✅ | |
| 配列インデックス | ✅ | |
| 配列→ポインタ変換 | ✅ | |
| .size()/.len() | ✅ | |
| .indexOf(), .includes() | ✅ | |
| .some(), .every() | ✅ | |
| 文字列メソッド | ✅ | substring, indexOf, trim等 |
| 文字列スライス `s[a:b]` | ✅ | 負のインデックス対応 |
| 文字列補間 `{var}` | ✅ | ポインタ、アドレス対応 |

### モジュールシステム

| 機能 | 状態 | 備考 |
|------|------|------|
| import文 | ✅ | 相対・絶対パス |
| export文 | ✅ | 関数、構造体、定数 |
| 名前空間 | ✅ | `ns::func()` |
| 再エクスポート | ✅ | `export { M };` |
| エイリアス | ✅ | `import M as Alias` |
| 階層的インポート | ✅ | `import std::io` |
| ワイルドカード | ✅ | `import ./path/*` |
| 循環依存検出 | ✅ | |

### バックエンド

| バックエンド | 状態 | 備考 |
|-------------|------|------|
| MIRインタプリタ | ✅ | 全機能対応 |
| LLVM Native | ✅ | x86_64, ARM64 |
| WASM | ✅ | ブラウザ/Node.js |

### テスト状況

| テスト種別 | 数 | 状態 |
|-----------|-----|------|
| C++ユニットテスト | 45+ | ✅ |
| インタプリタテスト | 203 | ✅ 191 passed, 12 skipped |
| LLVMテスト | 165+ | ✅ |
| WASMテスト | 165+ | ✅ |

## 🚧 開発中機能 (v0.11.0予定)

- ラムダ式 `|args| expr`
- 動的メモリ確保 `new/delete`
- Vec\<T\>型
- 配列高階関数 `.map()`, `.filter()`
- Debug/Display自動実装

## 📝 言語構文サンプル

```cm
// 構造体とwith自動実装
struct Point with Eq, Clone {
    int x;
    int y;
}

// ジェネリック関数
<T: Ord> T max(T a, T b) {
    return a > b ? a : b;
}

// モジュールインポート
import std::io;

int main() {
    Point p1 = {x: 10, y: 20};
    Point p2 = p1.clone();
    
    if (p1 == p2) {
        println("Equal: ({p1.x}, {p1.y})");
    }
    
    int result = max(10, 20);
    return 0;
}
```

## 📖 関連ドキュメント

- [クイックスタート](QUICKSTART.md)
- [ロードマップ](../ROADMAP.md)
- [言語仕様](spec/)
- [設計文書](design/)
