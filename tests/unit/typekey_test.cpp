// 型キー可逆エンコーディング（typekey.cpp）の単体テスト
// 手組みの ast::Type ツリーを入力に、往復不変（decode(encode(t)) の構造一致）と
// フラット__連結で縮退していた組が異なるキーへ分離されること（C7/C8）を検証する。

#include "internal/syntax/ast/typekey.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

using namespace cm;
using namespace cm::ast::typekey;

namespace {

// 名前付き型（type_args付き）を構築するヘルパー
ast::TypePtr named(const std::string& name, std::vector<ast::TypePtr> args = {}) {
    auto t = ast::make_named(name);
    t->type_args = std::move(args);
    return t;
}

// 2つの型ツリーの構造一致を再帰判定する
bool structurally_equal(const ast::TypePtr& a, const ast::TypePtr& b) {
    if (!a || !b)
        return a == b;
    if (a->kind != b->kind)
        return false;
    if (a->kind == ast::TypeKind::Struct && a->name != b->name)
        return false;
    if (a->array_size != b->array_size)
        return false;
    if (!!a->element_type != !!b->element_type)
        return false;
    if (a->element_type && !structurally_equal(a->element_type, b->element_type))
        return false;
    if (a->type_args.size() != b->type_args.size())
        return false;
    for (size_t i = 0; i < a->type_args.size(); ++i) {
        if (!structurally_equal(a->type_args[i], b->type_args[i]))
            return false;
    }
    return true;
}

// 往復不変を検証する（文字列往復と構造往復の両方）
void expect_roundtrip(const ast::TypePtr& t) {
    std::string key = encode_type_key(t);
    auto decoded = decode_type_key(key);
    ASSERT_TRUE(decoded) << "decode failed for key: " << key;
    EXPECT_TRUE(structurally_equal(t, decoded)) << "structure mismatch for key: " << key;
    EXPECT_EQ(key, encode_type_key(decoded)) << "re-encode mismatch for key: " << key;
}

TEST(TypeKeyTest, PrimitiveRoundtrip) {
    auto int_t = ast::make_int();
    EXPECT_EQ(encode_type_key(int_t), "int");
    expect_roundtrip(int_t);

    auto str_t = ast::make_string();
    EXPECT_EQ(encode_type_key(str_t), "string");
    expect_roundtrip(str_t);

    auto short_t = ast::make_short();
    expect_roundtrip(short_t);
}

TEST(TypeKeyTest, PlainNamedRoundtrip) {
    auto t = named("Point");
    EXPECT_EQ(encode_type_key(t), "Point");
    expect_roundtrip(t);
}

TEST(TypeKeyTest, SingleArgGeneric) {
    auto t = named("Box", {ast::make_int()});
    EXPECT_EQ(encode_type_key(t), "Box$1$3$int");
    expect_roundtrip(t);
}

TEST(TypeKeyTest, MultiArgGeneric) {
    auto t = named("Pair", {ast::make_int(), ast::make_string()});
    EXPECT_EQ(encode_type_key(t), "Pair$2$3$int6$string");
    expect_roundtrip(t);
}

TEST(TypeKeyTest, NestedGenericRoundtrip) {
    // Box<Pair<int,string>>（C7の代表例）
    auto inner = named("Pair", {ast::make_int(), ast::make_string()});
    auto t = named("Box", {inner});
    expect_roundtrip(t);

    // Box<Box<int>>
    auto bb = named("Box", {named("Box", {ast::make_int()})});
    expect_roundtrip(bb);

    // Pair<Box<int>, Box<string>>
    auto pboxes =
        named("Pair", {named("Box", {ast::make_int()}), named("Box", {ast::make_string()})});
    expect_roundtrip(pboxes);
}

TEST(TypeKeyTest, PointerAndArrayRoundtrip) {
    // *int
    auto p = ast::make_pointer(ast::make_int());
    EXPECT_EQ(encode_type_key(p), "$Pint");
    expect_roundtrip(p);

    // int[4]
    auto arr = ast::make_array(ast::make_int(), 4);
    EXPECT_EQ(encode_type_key(arr), "$A4$int");
    expect_roundtrip(arr);

    // 可変長 int[]
    auto slice = ast::make_array(ast::make_int());
    EXPECT_EQ(encode_type_key(slice), "$A$int");
    expect_roundtrip(slice);

    // Box<*int> / Box<int[8]> / Box<Box<short>[3]>
    expect_roundtrip(named("Box", {p}));
    expect_roundtrip(named("Box", {ast::make_array(ast::make_int(), 8)}));
    expect_roundtrip(named("Box", {ast::make_array(named("Box", {ast::make_short()}), 3)}));
}

TEST(TypeKeyTest, FlatNameCollisionsAreSeparated) {
    // フラット__連結ではこの3組が全て "Box__Box__int" に縮退していた（C7/C8）
    auto nested = named("Box", {named("Box", {ast::make_int()})});  // Box<Box<int>>
    auto two_args = named("Box", {named("Box"), ast::make_int()});  // Box<Box, int>
    auto user_type = named("Box__Box__int");                        // ユーザー定義

    std::string k1 = encode_type_key(nested);
    std::string k2 = encode_type_key(two_args);
    std::string k3 = encode_type_key(user_type);

    EXPECT_NE(k1, k2);
    EXPECT_NE(k1, k3);
    EXPECT_NE(k2, k3);

    expect_roundtrip(nested);
    expect_roundtrip(two_args);
    expect_roundtrip(user_type);
}

TEST(TypeKeyTest, UserIdentifierNamespaceSeparation) {
    // ユーザー定義 Box__int とジェネリック Box<int> のキーは一致しない（C8）
    auto user_type = named("Box__int");
    auto generic = named("Box", {ast::make_int()});
    EXPECT_NE(encode_type_key(user_type), encode_type_key(generic));
    // エンコード済みキーには必ず '$' が含まれ、ユーザー識別子には含まれない
    EXPECT_TRUE(is_encoded_key(encode_type_key(generic)));
    EXPECT_FALSE(is_encoded_key(encode_type_key(user_type)));
}

TEST(TypeKeyTest, AngleBracketNameIsNormalized) {
    // name に "Box<int>" 表記が残っていても type_args を真実として基底名を用いる
    auto t = std::make_shared<ast::Type>(ast::TypeKind::Struct);
    t->name = "Box<int>";
    t->type_args = {ast::make_int()};
    EXPECT_EQ(encode_type_key(t), "Box$1$3$int");
}

TEST(TypeKeyTest, MakeStructKeyMatchesEncode) {
    auto args = std::vector<ast::TypePtr>{ast::make_int(), ast::make_string()};
    EXPECT_EQ(make_struct_key("Pair", args), encode_type_key(named("Pair", args)));
    EXPECT_EQ(make_struct_key("Plain", {}), "Plain");
}

TEST(TypeKeyTest, BaseNameAndArgsExtraction) {
    auto t = named("Pair", {ast::make_int(), ast::make_string()});
    std::string key = encode_type_key(t);
    EXPECT_EQ(base_name_of(key), "Pair");
    auto args = decode_type_args(key);
    ASSERT_EQ(args.size(), 2u);
    EXPECT_EQ(args[0]->kind, ast::TypeKind::Int);
    EXPECT_EQ(args[1]->kind, ast::TypeKind::String);

    // 非エンコード名はそのまま
    EXPECT_EQ(base_name_of("Point"), "Point");
    EXPECT_TRUE(decode_type_args("Point").empty());
}

TEST(TypeKeyTest, DisplayNameIsHumanReadable) {
    auto inner = named("Pair", {ast::make_int(), ast::make_string()});
    auto t = named("Box", {inner});
    EXPECT_EQ(display_name(t), "Box<Pair<int, string>>");
    EXPECT_EQ(display_name(encode_type_key(t)), "Box<Pair<int, string>>");
    EXPECT_EQ(display_name(std::string("Point")), "Point");
}

TEST(TypeKeyTest, UnionRoundtripAndTypedefConvergence) {
    // int | string -> "$U2$3$int6$string"（変種順=タグ順のため並べ替えない）
    auto make_union = [](std::vector<ast::TypePtr> variants, const std::string& name) {
        auto t = std::make_shared<ast::Type>(ast::TypeKind::Union);
        t->type_args = std::move(variants);
        t->name = name;
        return t;
    };
    auto anon = make_union({ast::make_int(), ast::make_string()}, "int | string");
    EXPECT_EQ(encode_type_key(anon), "$U2$3$int6$string");
    expect_roundtrip(anon);

    // typedef名（IU）を持つ同一構造のユニオンは同じキーへ収束する（typedef同一視）
    auto aliased = make_union({ast::make_int(), ast::make_string()}, "IU");
    EXPECT_EQ(encode_type_key(aliased), encode_type_key(anon));

    // 変種順が違えば別キー（タグ値の意味が異なる）
    auto reordered = make_union({ast::make_string(), ast::make_int()}, "");
    EXPECT_NE(encode_type_key(reordered), encode_type_key(anon));

    // ジェネリック型引数位置のユニオン: Box<int | string>
    auto boxed = named("Box", {anon});
    expect_roundtrip(boxed);
    EXPECT_TRUE(is_encoded_key(encode_type_key(boxed)));

    // arg_key_from_treeも$U規約（typedef名の素通しをしない）
    EXPECT_EQ(arg_key_from_tree(aliased), "$U2$3$int6$string");

    // 表示名: typedef名があれば優先、無ければ " | " 連結
    EXPECT_EQ(display_name(aliased), "IU");
    EXPECT_EQ(display_name(make_union({ast::make_int(), ast::make_string()}, "")), "int | string");
}

TEST(TypeKeyTest, MalformedKeysReturnNull) {
    EXPECT_EQ(decode_type_key(""), nullptr);
    EXPECT_EQ(decode_type_key("Box$"), nullptr);
    EXPECT_EQ(decode_type_key("Box$1$"), nullptr);
    EXPECT_EQ(decode_type_key("Box$1$99$int"), nullptr);
    EXPECT_EQ(decode_type_key("Box$1$3$intX"), nullptr);
    EXPECT_EQ(decode_type_key("$Q"), nullptr);
}

}  // namespace
