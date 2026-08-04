#include "../../src/internal/hir/lowering/lowering.hpp"
#include "../../src/internal/hir/type_audit.hpp"
#include "../../src/internal/syntax/lexer/lexer.hpp"
#include "../../src/internal/syntax/parser/parser.hpp"
#include "../../src/internal/types/checking/checker.hpp"

#include <gtest/gtest.h>
#include <sstream>
#include <string>

using namespace cm;

// ============================================================
// HIR型不変条件の回帰（typed-hir-single-source 第2段）
// ============================================================
// 「型検査成功後のHIRは全HirExpr.typeが非null・非error」を代表的な構文で常時検証する。
// 違反はaudit_typesの集計として検出され、新規コードが不変条件を破った時点でこのテストが落ちる
class HirTypeAuditTest : public ::testing::Test {
   protected:
    // parse→型検査→HIR loweringを通し、監査結果を返す
    hir::TypeAuditResult check_and_audit(const std::string& code) {
        Lexer lex(code);
        auto tokens = lex.tokenize();
        Parser p(std::move(tokens));
        auto ast = p.parse();
        EXPECT_FALSE(p.has_errors()) << "パースエラー";

        TypeChecker checker;
        const bool ok = checker.check(ast);
        EXPECT_TRUE(ok) << "型検査エラー";

        hir::HirLowering lowering;
        auto hir = lowering.lower(ast);
        return hir::audit_types(hir);
    }

    void expect_clean(const std::string& code) {
        auto audit = check_and_audit(code);
        EXPECT_EQ(audit.null_types, 0u) << "type=nullのHirExprが存在";
        EXPECT_EQ(audit.error_types, 0u) << "error型のHirExprが存在";
        EXPECT_GT(audit.total_exprs, 0u);
        if (!audit.ok()) {
            for (const auto& s : audit.samples) {
                ADD_FAILURE() << "違反: " << s;
            }
        }
    }
};

// 基本式・制御フロー
TEST_F(HirTypeAuditTest, BasicExpressions) {
    expect_clean(R"(
int add(int a, int b) {
    return a + b;
}
int main() {
    int x = add(1, 2);
    int y = x * 3 - 4 / 2;
    bool b = x > y && y != 0;
    return b ? x : y;
}
)");
}

// for-in（インデックス脱糖の合成ノード）
TEST_F(HirTypeAuditTest, ForInDesugar) {
    expect_clean(R"(
int main() {
    int[3] arr;
    arr[0] = 1;
    arr[1] = 2;
    arr[2] = 3;
    int sum = 0;
    for (int v in arr) {
        sum = sum + v;
    }
    return sum;
}
)");
}

// switch caseパターン値（単一値・範囲・OR）
TEST_F(HirTypeAuditTest, SwitchPatterns) {
    expect_clean(R"(
int classify(int x) {
    switch (x) {
        case (1) {
            return 10;
        }
        case (2 | 3) {
            return 20;
        }
        case (4...6) {
            return 30;
        }
        else {
            return 0;
        }
    }
    return -1;
}
int main() {
    return classify(2);
}
)");
}

// 演算子impl本体（self/other/メンバアクセス）
TEST_F(HirTypeAuditTest, OperatorImplBody) {
    expect_clean(R"(
struct P {
    int x;
    int y;
}
impl P for Eq {
    operator bool ==(P other) {
        return self.x == other.x && self.y == other.y;
    }
}
int main() {
    P a = { x: 1, y: 2 };
    P b = { x: 1, y: 2 };
    if (a == b) {
        return 1;
    }
    return 0;
}
)");
}

// デフォルト引数式
TEST_F(HirTypeAuditTest, DefaultArgs) {
    expect_clean(R"(
int scale(int v, int factor = 10, int offset = 3) {
    return v * factor + offset;
}
int main() {
    return scale(2) + scale(2, 5) + scale(1, 2, 0);
}
)");
}

// 構造体・メンバ・メソッド
TEST_F(HirTypeAuditTest, StructAndMethods) {
    expect_clean(R"(
struct Counter {
    int value;
}
impl Counter {
    void inc() {
        self.value = self.value + 1;
    }
    int get() {
        return self.value;
    }
}
int main() {
    Counter c = { value: 0 };
    c.inc();
    c.inc();
    return c.get();
}
)");
}

// 三項・キャスト・単項
TEST_F(HirTypeAuditTest, TernaryCastUnary) {
    expect_clean(R"(
int main() {
    long v = 40 as long;
    int w = -3;
    long r = v + (w > 0 ? w : -w) as long;
    return r as int;
}
)");
}
