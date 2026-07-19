#include "../../src/internal/base/error.hpp"

#include <gtest/gtest.h>
#include <sstream>

using namespace cm;

class ErrorTest : public ::testing::Test {
   protected:
    void SetUp() override { collector_.clear(); }

    ErrorCollector collector_;
};

// Error型の生成テスト
TEST_F(ErrorTest, CreateParseError) {
    auto err = Error::parse("unexpected token", Span{10, 15});
    EXPECT_EQ(err.kind, ErrorKind::Parse);
    EXPECT_EQ(err.code, "P001");
    EXPECT_EQ(err.message, "unexpected token");
    EXPECT_EQ(err.span.start, 10u);
    EXPECT_EQ(err.span.end, 15u);
}

TEST_F(ErrorTest, CreateTypeError) {
    auto err = Error::type("type mismatch", Span{20, 30});
    EXPECT_EQ(err.kind, ErrorKind::Type);
    EXPECT_EQ(err.code, "T001");
    EXPECT_EQ(err.message, "type mismatch");
}

TEST_F(ErrorTest, CreateCodegenError) {
    auto err = Error::codegen("SV002", "unsupported operation");
    EXPECT_EQ(err.kind, ErrorKind::Codegen);
    EXPECT_EQ(err.code, "SV002");
    EXPECT_EQ(err.message, "unsupported operation");
    EXPECT_TRUE(err.span.is_empty());
}

TEST_F(ErrorTest, CreateIOError) {
    auto err = Error::io("file not found");
    EXPECT_EQ(err.kind, ErrorKind::IO);
    EXPECT_EQ(err.message, "file not found");
}

TEST_F(ErrorTest, CreateInternalError) {
    auto err = Error::internal("assertion failed");
    EXPECT_EQ(err.kind, ErrorKind::Internal);
    EXPECT_EQ(err.message, "assertion failed");
}

// Error::kind_string テスト
TEST_F(ErrorTest, KindString) {
    EXPECT_EQ(Error::parse("", Span::empty()).kind_string(), "parse");
    EXPECT_EQ(Error::type("", Span::empty()).kind_string(), "type");
    EXPECT_EQ(Error::codegen("", "").kind_string(), "codegen");
    EXPECT_EQ(Error::io("").kind_string(), "io");
    EXPECT_EQ(Error::internal("").kind_string(), "internal");
}

// Result型テスト
TEST_F(ErrorTest, ResultSuccess) {
    Result<int> result = 42;
    EXPECT_FALSE(is_error(result));
    EXPECT_EQ(std::get<int>(result), 42);
    EXPECT_EQ(get_error(result), nullptr);
}

TEST_F(ErrorTest, ResultError) {
    Result<int> result = Error::parse("error", Span::empty());
    EXPECT_TRUE(is_error(result));
    EXPECT_NE(get_error(result), nullptr);
    EXPECT_EQ(get_error(result)->message, "error");
}

TEST_F(ErrorTest, UnwrapOrSuccess) {
    Result<int> result = 42;
    EXPECT_EQ(unwrap_or(std::move(result), -1), 42);
}

TEST_F(ErrorTest, UnwrapOrError) {
    Result<int> result = Error::parse("error", Span::empty());
    EXPECT_EQ(unwrap_or(std::move(result), -1), -1);
}

// ErrorCollectorテスト
TEST_F(ErrorTest, CollectorEmpty) {
    EXPECT_FALSE(collector_.has_errors());
    EXPECT_EQ(collector_.error_count(), 0u);
    EXPECT_EQ(collector_.warning_count(), 0u);
}

TEST_F(ErrorTest, CollectorAddError) {
    collector_.add(Error::parse("error1", Span::empty()));
    collector_.add(Error::type("error2", Span::empty()));

    EXPECT_TRUE(collector_.has_errors());
    EXPECT_EQ(collector_.error_count(), 2u);
    EXPECT_EQ(collector_.errors()[0].message, "error1");
    EXPECT_EQ(collector_.errors()[1].message, "error2");
}

TEST_F(ErrorTest, CollectorAddWarning) {
    collector_.add_warning(Error::parse("warning1", Span::empty()));

    EXPECT_FALSE(collector_.has_errors());
    EXPECT_EQ(collector_.warning_count(), 1u);
    EXPECT_EQ(collector_.warnings()[0].message, "warning1");
}

TEST_F(ErrorTest, CollectorInternalAsWarning) {
    // Internal errors are treated as warnings
    collector_.add(Error::internal("internal issue"));

    EXPECT_FALSE(collector_.has_errors());
    EXPECT_EQ(collector_.warning_count(), 1u);
}

TEST_F(ErrorTest, CollectorReportAll) {
    collector_.add(Error::parse("parse error", Span{10, 20}));
    collector_.add_warning(Error::type("type warning", Span::empty()));

    std::stringstream ss;
    collector_.report_all(ss);

    std::string output = ss.str();
    EXPECT_NE(output.find("error[P001]: parse error"), std::string::npos);
    EXPECT_NE(output.find("warning[T001]: type warning"), std::string::npos);
}

TEST_F(ErrorTest, CollectorClear) {
    collector_.add(Error::parse("error", Span::empty()));
    collector_.add_warning(Error::type("warning", Span::empty()));

    EXPECT_TRUE(collector_.has_errors());

    collector_.clear();

    EXPECT_FALSE(collector_.has_errors());
    EXPECT_EQ(collector_.error_count(), 0u);
    EXPECT_EQ(collector_.warning_count(), 0u);
}
