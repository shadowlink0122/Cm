// MIRモジュール分割（mir_splitter.cpp）の単体テスト
// モジュール名のファイルシステム安全化（"<generated>"等）と、グローバル変数の所有モジュール一本化
// （所有モジュールのみglobal_vars、他はextern_global_vars）、全型定義の全モジュール配布を検証する（H14）。

#include "internal/mir/mir_splitter.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <string>

using namespace cm;
using namespace cm::mir;

namespace {

// source_file付きの空関数を作るヘルパー
MirFunctionPtr make_func(const std::string& name, const std::string& source_file) {
    auto f = std::make_unique<MirFunction>();
    f->name = name;
    f->source_file = source_file;
    return f;
}

// source_file付きの構造体を作るヘルパー
MirStructPtr make_struct(const std::string& name, const std::string& source_file) {
    auto s = std::make_unique<MirStruct>();
    s->name = name;
    s->source_file = source_file;
    return s;
}

TEST(MirSplitterTest, SanitizesPseudoSourceNames) {
    // "<generated>"のような擬似ソース名は.oファイル名・シェルコマンドとして安全な名前に正規化される
    EXPECT_EQ(MirSplitter::source_file_to_module_name("<generated>"), "generated_");
    // 通常のパスは従来どおり
    EXPECT_EQ(MirSplitter::source_file_to_module_name("libs/efi_core.cm"), "libs_efi_core");
    EXPECT_EQ(MirSplitter::source_file_to_module_name("main.cm"), "main");
    EXPECT_EQ(MirSplitter::source_file_to_module_name(""), "main");
}

TEST(MirSplitterTest, GlobalsOwnedBySingleModule) {
    MirProgram program;
    program.functions.push_back(make_func("main", "main.cm"));
    program.functions.push_back(make_func("helper", "libs/util.cm"));

    auto gv = std::make_unique<MirGlobalVar>();
    gv->name = "counter";
    gv->type = hir::make_named("int");
    program.global_vars.push_back(std::move(gv));

    auto modules = MirSplitter::split_by_module(program);
    ASSERT_EQ(modules.size(), 2u);

    // mainモジュールだけが定義を持ち、他モジュールはextern宣言のみ
    const auto& main_mod = modules.at("main");
    EXPECT_EQ(main_mod.global_vars.size(), 1u);
    EXPECT_EQ(main_mod.extern_global_vars.size(), 0u);

    const auto& util_mod = modules.at("libs_util");
    EXPECT_EQ(util_mod.global_vars.size(), 0u);
    ASSERT_EQ(util_mod.extern_global_vars.size(), 1u);
    EXPECT_EQ(util_mod.extern_global_vars[0]->name, "counter");
}

TEST(MirSplitterTest, AllStructsVisibleToAllModules) {
    // 型定義はオブジェクトコードを生成しないため全モジュールへ配布される。
    // 参照収集ベースだとフィールド経由・自己参照ポインタの間接参照を見落とし、
    // 未登録名がタグ付きユニオン互換のフォールバックレイアウトへ落ちてモジュール間でGEPがずれる
    MirProgram program;
    program.functions.push_back(make_func("main", "main.cm"));
    program.functions.push_back(make_func("helper", "libs/util.cm"));
    program.structs.push_back(make_struct("Node", "main.cm"));
    program.structs.push_back(make_struct("Item", "libs/util.cm"));

    auto modules = MirSplitter::split_by_module(program);
    for (const auto& [name, mod] : modules) {
        // 自モジュール定義 + extern の合計が全構造体数に一致する
        EXPECT_EQ(mod.structs.size() + mod.extern_structs.size(), 2u) << "module: " << name;
    }
}

}  // namespace
