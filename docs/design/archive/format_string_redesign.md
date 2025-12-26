# フォーマット文字列処理の再設計

## 現在の問題
- フォーマット指定子の処理が各コードジェンに分散している
- 言語ごとに異なるフォーマット仕様への対応が複雑
- MIRレベルでフォーマット指定子がスキップされている

## 提案する新設計

### 1. MIRレベルでのフォーマット処理

#### フォーマット指定子のパース
```cpp
struct FormatSpec {
    enum Type {
        Default,     // {}
        Hex,         // {:x}
        HexUpper,    // {:X}
        Binary,      // {:b}
        Octal,       // {:o}
        Decimal,     // {:.N}
        Scientific,  // {:e}
        ScientificUpper, // {:E}
        LeftAlign,   // {:<N}
        RightAlign,  // {:>N}
        CenterAlign, // {:^N}
        ZeroPad      // {:0>N}
    };

    Type type = Default;
    int precision = -1;  // 小数点以下桁数
    int width = -1;      // 幅指定
    char fill = ' ';     // 埋め文字
};
```

#### MIR変換での処理
```cpp
// println("Hex: {:x}", n) を以下に変換:
//
// _tmp1 = format_hex(n)
// _tmp2 = concat("Hex: ", _tmp1)
// println(_tmp2)
```

### 2. MIR組み込み関数の追加

```cpp
enum MirBuiltin {
    FormatInt,       // 整数フォーマット
    FormatFloat,     // 浮動小数点フォーマット
    FormatString,    // 文字列フォーマット
    StringConcat,    // 文字列連結
    ToString         // 任意の型を文字列化
};
```

### 3. コードジェンの簡略化

各言語のコードジェンは、MIRの組み込み関数を対応する言語の機能にマップするだけ：

- **Rust**: `format!("{:x}", n)` → そのまま
- **TypeScript**: `n.toString(16)` → 変換
- **C++**: `std::format("{:x}", n)` または手動実装

## 実装ステップ

1. **FormatSpecパーサーの実装**
   - mir_lowering.hppでフォーマット指定子を完全にパース

2. **MIR組み込み関数の追加**
   - フォーマット操作を表現する新しいMIRノード

3. **MIR変換の改善**
   - フォーマット文字列を一連のフォーマット操作に分解

4. **コードジェンの更新**
   - 各言語でMIR組み込み関数を実装

## 利点

1. **一貫性**: すべてのバックエンドで同じフォーマット動作
2. **保守性**: フォーマット処理が一箇所に集約
3. **拡張性**: 新しいフォーマット指定子の追加が容易
4. **最適化**: MIRレベルでの最適化が可能

## 例

入力:
```cm
println("Binary: {:b}, Hex: {:x}", 255, 42);
```

MIR（現在）:
```
_1 = const "Binary: {:b}, Hex: {:x}"
_2 = const 255
_3 = const 42
call println(_1, _2, _3)
```

MIR（提案）:
```
_1 = const 255
_2 = format_binary(_1)        // "11111111"
_3 = const "0b"
_4 = concat(_3, _2)           // "0b11111111"
_5 = const "Binary: "
_6 = concat(_5, _4)           // "Binary: 0b11111111"
_7 = const ", Hex: "
_8 = concat(_6, _7)           // "Binary: 0b11111111, Hex: "
_9 = const 42
_10 = format_hex(_9)          // "2a"
_11 = concat(_8, _10)         // "Binary: 0b11111111, Hex: 2a"
call println(_11)
```