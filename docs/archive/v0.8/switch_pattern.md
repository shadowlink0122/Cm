[English](switch_pattern.en.html)

# Switch文パターンマッチング仕様

## 構文

```ebnf
switch_stmt = "switch" "(" expr ")" "{" case_clause* else_clause? "}"
case_clause = "case" "(" pattern ")" block
else_clause = "else" block
pattern = or_pattern
or_pattern = range_pattern ("|" range_pattern)*
range_pattern = value_pattern ("..." value_pattern)?
value_pattern = literal | ident
```

## パターンの種類

### 1. 単一値パターン
```cm
case(5) {
    // x == 5
}
```

### 2. ORパターン（複数値）
```cm
case(1 | 3 | 5) {
    // x == 1 || x == 3 || x == 5
}
```

### 3. 範囲パターン
```cm
case(1...10) {
    // 1 <= x && x <= 10 (inclusive)
}
```

### 4. 複合パターン
```cm
case(1...5 | 10 | 20...30) {
    // (1 <= x && x <= 5) || x == 10 || (20 <= x && x <= 30)
}
```

### 5. else節（デフォルトケース）
```cm
else {
    // その他すべての値
}
```

## サポートする型

すべてのプリミティブ型をサポート：
- 整数型: `int`, `uint`, `tiny`, `short`, `long`, etc.
- 浮動小数点型: `float`, `double`
- 文字型: `char`
- 論理型: `bool`
- 文字列型: `string`（将来的に）

## セマンティクス

1. **フォールスルーなし**: 各caseは独立したブロックスコープを持ち、自動的にbreakされる
2. **評価順序**: 上から順に評価され、最初にマッチしたケースが実行される
3. **網羅性**: else節がない場合、マッチしなければ何も実行されない（エラーにはならない）
4. **型の一致**: すべてのパターンは switch式と同じ型でなければならない

## 実装例

```cm
int categorize(int score) {
    switch(score) {
        case(100) {
            println("Perfect!");
            return 1;
        }
        case(90...99) {
            println("Excellent");
            return 2;
        }
        case(70...89) {
            println("Good");
            return 3;
        }
        case(60...69) {
            println("Pass");
            return 4;
        }
        case(0...59) {
            println("Fail");
            return 5;
        }
        else {
            println("Invalid score");
            return -1;
        }
    }
}

// 複雑なパターンの例
char getGrade(int score) {
    switch(score) {
        case(95...100 | 0) {  // 特別なケース：満点または0点
            return 'S';
        }
        case(80...94) {
            return 'A';
        }
        case(70...79) {
            return 'B';
        }
        case(60...69) {
            return 'C';
        }
        else {
            return 'F';
        }
    }
}
```

## HIR/MIRへの変換戦略

### HIRレベル
```
HirSwitch {
    expr: HirExpr,
    cases: Vec<HirCase>,
    else_block: Option<Vec<HirStmt>>
}

HirCase {
    patterns: Vec<HirPattern>,  // ORパターンは複数のパターン
    body: Vec<HirStmt>
}

HirPattern {
    kind: enum {
        Value(HirExpr),
        Range(HirExpr, HirExpr)
    }
}
```

### MIR変換
範囲やORパターンは、条件分岐の連鎖に変換：

```
// case(1...5 | 10) の場合
if (1 <= x && x <= 5) goto case_block;
if (x == 10) goto case_block;
goto next_case;
```

## 実装優先順位

1. **Phase 1**: 単一値パターンとelse節の実装
2. **Phase 2**: ORパターンの実装
3. **Phase 3**: 範囲パターンの実装
4. **Phase 4**: 型チェックと最適化