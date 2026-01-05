#pragma once

#include <sstream>
#include <string>

namespace cm::codegen::js {

// JavaScript出力用のエミッター
class JSEmitter {
   public:
    explicit JSEmitter(int indentSpaces = 4) : indent_spaces_(indentSpaces) {}

    // 出力ストリームを取得
    std::stringstream& stream() { return output_; }

    // 生成されたコードを取得
    std::string getCode() const { return output_.str(); }

    // 出力をクリア
    void clear() {
        output_.str("");
        output_.clear();
        indent_level_ = 0;
    }

    // コードを出力
    void emit(const std::string& code) { output_ << code; }

    // 行を出力（インデント付き）
    void emitLine(const std::string& code = "") {
        if (!code.empty()) {
            emitIndent();
            output_ << code;
        }
        output_ << "\n";
    }

    // インデントを出力
    void emitIndent() {
        for (int i = 0; i < indent_level_ * indent_spaces_; ++i) {
            output_ << " ";
        }
    }

    // インデントを増減
    void increaseIndent() { indent_level_++; }
    void decreaseIndent() {
        if (indent_level_ > 0)
            indent_level_--;
    }

    // インデントレベル取得
    int indentLevel() const { return indent_level_; }

   private:
    std::stringstream output_;
    int indent_level_ = 0;
    int indent_spaces_ = 4;
};

}  // namespace cm::codegen::js
