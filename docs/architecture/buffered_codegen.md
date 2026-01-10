[English](buffered_codegen.en.html)

# バッファベース・コード生成アーキテクチャ

## 問題：ストリーミング方式の欠点

### 従来の方式（ストリーミング）
```cpp
// 問題のあるコード
void generate_code(std::ostream& out) {
    out << "function main() {\n";

    // 無限ループの可能性！
    while (has_more_instructions()) {
        out << generate_instruction();  // IO待機が発生
    }

    out << "}\n";
}
```

### 問題点
1. **無限生成の検出不可** - IOに直接書き込むため、サイズ制限が困難
2. **IO待機時間** - 各書き込みでIOブロッキングが発生
3. **ロールバック不可** - 一度書き込んだデータは取り消せない
4. **検証の困難** - 全体を見てからの検証ができない

## 解決：バッファリング方式

### 新しいアーキテクチャ
```cpp
// 改善されたコード
std::string generate_code() {
    BufferedCodeGenerator gen;
    gen.begin_generation();

    // サイズ制限付きで生成
    gen.append_line("function main() {");

    while (has_more_instructions() && gen.check_limits()) {
        gen.append_line(generate_instruction());
    }

    gen.append_line("}");

    // 一括でIOに出力
    return gen.end_generation();
}
```

## 3層アーキテクチャ

### Layer 1: BufferedCodeGenerator
基本的なバッファリング機能
- サイズ制限（行数、バイト数）
- 時間制限（生成時間）
- 統計情報収集

### Layer 2: TwoPhaseCodeGenerator
構造化された生成
- Phase 1: ブロック構築（メモリ上）
- Phase 2: 検証と生成（文字列化）
- 必須/非必須ブロックの管理

### Layer 3: ScopedCodeSection
RAIIベースの安全な生成
- 自動的なセクション管理
- エラー時の自動ロールバック（概念的）
- ネストしたセクション対応

## 実装の利点

### 1. 無限ループ対策
```cpp
// 制限により自動停止
if (stats.total_bytes > limits.max_bytes) {
    set_error("サイズ超過");
    return false;  // 生成を停止
}
```

### 2. パフォーマンス向上
```cpp
// Before: 1000回のIO操作
for (int i = 0; i < 1000; i++) {
    file << "line " << i << "\n";  // 各行でIO
}

// After: 1回のIO操作
buffer.generate_1000_lines();
file << buffer.get_all();  // 一括IO
```

### 3. 事前検証
```cpp
// 生成前にサイズを推定
if (!generator.validate_size()) {
    // 生成せずにエラー処理
    return handle_size_error();
}
```

## 実装例：LLVM IR生成

### フェーズ分割
```cpp
enum Phase {
    SETUP,      // ターゲット設定
    GLOBALS,    // グローバル変数
    FUNCTIONS,  // 関数定義
    METADATA,   // デバッグ情報
    FINALIZE    // 最終処理
};
```

### 巨大関数の分割処理
```cpp
if (inst_count > 10000) {
    // 基本ブロックごとに分割
    for (auto& bb : function) {
        add_block(bb_name, bb_content, /*critical=*/true);
    }
}
```

## パフォーマンス比較

| 指標 | ストリーミング | バッファリング |
|-----|--------------|--------------|
| IO回数 | O(n) | O(1) |
| メモリ使用 | 最小 | O(n) |
| 無限ループ検出 | 不可 | 可能 |
| ロールバック | 不可 | 可能 |
| 事前検証 | 不可 | 可能 |
| 生成速度 | 遅い | 高速 |

## 制限設定の目安

### 小規模プロジェクト
```cpp
limits.max_bytes = 10 * 1024 * 1024;   // 10MB
limits.max_lines = 100000;             // 10万行
limits.max_generation_time = 5s;       // 5秒
```

### 中規模プロジェクト
```cpp
limits.max_bytes = 100 * 1024 * 1024;  // 100MB
limits.max_lines = 1000000;            // 100万行
limits.max_generation_time = 30s;      // 30秒
```

### 大規模プロジェクト
```cpp
limits.max_bytes = 1024 * 1024 * 1024; // 1GB
limits.max_lines = 10000000;           // 1000万行
limits.max_generation_time = 300s;     // 5分
```

## エラーハンドリング

### 段階的エラー処理
```cpp
class ErrorRecovery {
    enum Strategy {
        ABORT,          // 即座に中断
        SKIP_BLOCK,     // ブロックをスキップ
        REDUCE_SIZE,    // サイズを削減
        SPLIT_OUTPUT    // 出力を分割
    };
};
```

### 警告システム
```cpp
if (size > warning_threshold) {
    warn("生成サイズが50MBを超えています");
    // 継続するが警告を出力
}
```

## ベストプラクティス

### DO
- ✅ 生成前にサイズを推定
- ✅ 必須/非必須を区別
- ✅ 段階的に生成
- ✅ 統計情報を記録

### DON'T
- ❌ 無制限の再帰生成
- ❌ 巨大な単一ブロック
- ❌ 検証なしの生成
- ❌ エラー時の継続

## 移行ガイド

### Step 1: BufferedCodeGeneratorを継承
```cpp
class MyGenerator : public BufferedCodeGenerator {
    // ...
};
```

### Step 2: 生成ロジックを変更
```cpp
// Before
void generate(std::ostream& out) {
    out << content;
}

// After
std::string generate() {
    append(content);
    return get_generated_code();
}
```

### Step 3: 制限を設定
```cpp
generator.set_limits({
    .max_bytes = 100 * MB,
    .max_lines = 1000000,
    .warning_threshold_bytes = 50 * MB
});
```

## まとめ

バッファベースのコード生成により：
- **安全性向上** - 無限ループや巨大出力を防止
- **性能向上** - IO回数を最小化
- **保守性向上** - 事前検証とエラー処理が容易

「生成してから出力」という単純な原則により、
より堅牢で効率的なコンパイラを実現します。