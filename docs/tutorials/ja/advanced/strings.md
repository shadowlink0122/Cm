---
title: 文字列処理
parent: Tutorials
---

[English](../../en/advanced/strings.html)

# 高度な機能編 - 文字列操作

**難易度:** 🟡 中級  
**所要時間:** 20分

## 文字列メソッド

```cm
int main() {
    string str = "Hello, World!";

    int len = str.len();
    char first = str.charAt(0)  // コードポイント添字（ASCIIのみ値を返す。生バイトはbyte_at）;
    string sub1 = str.substring(0, 5);
    int pos = str.indexOf("World");
    string upper = str.toUpperCase();
    string lower = str.toLowerCase();
    string trimmed = "  text  ".trim();
    bool starts = str.startsWith("Hello");
    bool ends = str.endsWith("!");
    bool contains = str.contains("World");
    string repeated = "Ha".repeat(3);
    string replaced = str.replace("World", "Cm");
    return 0;
}
```

## 文字列スライス

```cm
int main() {
    string s = "Hello, World!";

    string sub1 = s[0:5];
    string sub2 = s[7:12];
    string tail = s[7:];
    string head = s[:5];
    string copy = s[:];
    string last3 = s[-3:];
    return 0;
}
```

## エスケープシーケンスとraw文字列

文字列・文字リテラルは次のエスケープシーケンスに対応しています（v0.17.0で`\x`/`\u`/`\U`のデコードを実装。未知のエスケープは従来のバックスラッシュ黙殺でなくコンパイルエラーになります）:

| エスケープ | 意味 |
|---|---|
| `\n` `\t` `\r` `\b` `\f` `\v` `\a` `\0` | 制御文字 |
| `\\` `\"` `\'` | バックスラッシュ・引用符 |
| `\{` `\}` `\$` | 補間のエスケープ（リテラルの`{` `}` `$`） |
| `\xHH` | 1バイト（16進2桁） |
| `\uHHHH` / `\UHHHHHHHH` | UnicodeコードポイントをUTF-8へエンコード |

```cm
string a = "\x41";        // "A"（len=1）
string e = "\u00e9";      // "é"（len=1・byte_len=2）
string g = "\U0001F600";  // "😀"
char c = '\x41';          // 'A'（charリテラルも同じエスケープ表）
```

raw文字列（バッククォート）はエスケープを解釈せず、バックスラッシュをそのまま保持します（Windowsパスや正規表現に便利）。唯一の例外はデリミタのエスケープ`` \` ``で、補間は`${expr}`のみ有効です。

```cm
string path = `C:\path\n`;   // 9文字のリテラル（\nは改行にならない）
string tick = `a\`b`;         // "a`b"
```

---

---

**前の章:** [関数ポインタ](function-pointers.html)  
**次の章:** [コンパイラの使い方](../compiler/common/usage.html)

---

**最終更新:** 2026-02-08

---

<!-- nav -->
← 前: [ラムダ式](lambda.html) ｜ [目次](index.html) ｜ 次: [スライス型](slices.html) →
