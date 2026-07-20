// 型キー可逆エンコーディング（typekey.cpp）の単体テスト
// 手組みの hir::Type ツリーを入力に、往復不変（decode(encode(t)) の構造一致）と
// フラット__連結で縮退していた組が異なるキーへ分離されること（C7/C8）を検証する。

#include "internal/mir/lowering/mono/typekey.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

using namespace cm;
using namespace cm::mir::typekey;

namespace {

// 名前付き型（type_args付き）を構築するヘルパー
hir::TypePtr named(const std::string& name, std::vector<hir::TypePtr> args = {}) {
    auto t = hir::make_named(name);
    t->type_args = std::move(args);
    return t;
}

// 2つの型ツリーの構造一致を再帰判定する
bool structurally_equal(const hir::TypePtr& a, const hir::TypePtr& b) {
    if (!a || !b)
        return a == b;
    if (a->kind != b->kind)
        return false;
    if (a->kind == hir::TypeKind::Struct && a->name != b->name)
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
void expect_roundtrip(const hir::TypePtr& t) {
    std::string key = encode_type_key(t);
    auto decoded = decode_type_key(key);
    ASSERT_TRUE(decoded) << "decode failed for key: " << key;
    EXPECT_TRUE(structurally_equal(t, decoded)) << "structure mismatch for key: " << key;
    EXPECT_EQ(key, encode_type_key(decoded)) << "re-encode mismatch for key: " << key;
}

TEST(TypeKeyTest, PrimitiveRoundtrip) {
    auto int_t = hir::make_int();
    EXPECT_EQ(encode_type_key(int_t), "int");
    expect_roundtrip(int_t);

    auto str_t = hir::make_string();
    EXPECT_EQ(encode_type_key(str_t), "string");
    expect_roundtrip(str_t);

    auto short_t = hir::make_short();
    expect_roundtrip(short_t);
}

TEST(TypeKeyTest, PlainNamedRoundtrip) {
    auto t = named("Point");
    EXPECT_EQ(encode_type_key(t), "Point");
    expect_roundtrip(t);
}

TEST(TypeKeyTest, SingleArgGeneric) {
    auto t = named("Box", {hir::make_int()});
    EXPECT_EQ(encode_type_key(t), "Box$1$3$int");
    expect_roundtrip(t);
}

TEST(TypeKeyTest, MultiArgGeneric) {
    auto t = named("Pair", {hir::make_int(), hir::make_string()});
    EXPECT_EQ(encode_type_key(t), "Pair$2$3$int6$string");
    expect_roundtrip(t);
}

TEST(TypeKeyTest, NestedGenericRoundtrip) {
    // Box<Pair<int,string>>（C7の代表例）
    auto inner = named("Pair", {hir::make_int(), hir::make_string()});
    auto t = named("Box", {inner});
    expect_roundtrip(t);

    // Box<Box<int>>
    auto bb = named("Box", {named("Box", {hir::make_int()})});
    expect_roundtrip(bb);

    // Pair<Box<int>, Box<string>>
    auto pboxes =
        named("Pair", {named("Box", {hir::make_int()}), named("Box", {hir::make_string()})});
    expect_roundtrip(pboxes);
}

TEST(TypeKeyTest, PointerAndArrayRoundtrip) {
    // *int
    auto p = hir::make_pointer(hir::make_int());
    EXPECT_EQ(encode_type_key(p), "$Pint");
    expect_roundtrip(p);

    // int[4]
    auto arr = hir::make_array(hir::make_int(), 4);
    EXPECT_EQ(encode_type_key(arr), "$A4$int");
    expect_roundtrip(arr);

    // 可変長 int[]
    auto slice = hir::make_array(hir::make_int());
    EXPECT_EQ(encode_type_key(slice), "$A$int");
    expect_roundtrip(slice);

    // Box<*int> / Box<int[8]> / Box<Box<short>[3]>
    expect_roundtrip(named("Box", {p}));
    expect_roundtrip(named("Box", {hir::make_array(hir::make_int(), 8)}));
    expect_roundtrip(named("Box", {hir::make_array(named("Box", {hir::make_short()}), 3)}));
}

TEST(TypeKeyTest, FlatNameCollisionsAreSeparated) {
    // フラット__連結ではこの3組が全て "Box__Box__int" に縮退していた（C7/C8）
    auto nested = named("Box", {named("Box", {hir::make_int()})});  // Box<Box<int>>
    auto two_args = named("Box", {named("Box"), hir::make_int()});  // Box<Box, int>
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
    auto generic = named("Box", {hir::make_int()});
    EXPECT_NE(encode_type_key(user_type), encode_type_key(generic));
    // エンコード済みキーには必ず '$' が含まれ、ユーザー識別子には含まれない
    EXPECT_TRUE(is_encoded_key(encode_type_key(generic)));
    EXPECT_FALSE(is_encoded_key(encode_type_key(user_type)));
}

TEST(TypeKeyTest, AngleBracketNameIsNormalized) {
    // name に "Box<int>" 表記が残っていても type_args を真実として基底名を用いる
    auto t = std::make_shared<hir::Type>(hir::TypeKind::Struct);
    t->name = "Box<int>";
    t->type_args = {hir::make_int()};
    EXPECT_EQ(encode_type_key(t), "Box$1$3$int");
}

TEST(TypeKeyTest, MakeStructKeyMatchesEncode) {
    auto args = std::vector<hir::TypePtr>{hir::make_int(), hir::make_string()};
    EXPECT_EQ(make_struct_key("Pair", args), encode_type_key(named("Pair", args)));
    EXPECT_EQ(make_struct_key("Plain", {}), "Plain");
}

TEST(TypeKeyTest, BaseNameAndArgsExtraction) {
    auto t = named("Pair", {hir::make_int(), hir::make_string()});
    std::string key = encode_type_key(t);
    EXPECT_EQ(base_name_of(key), "Pair");
    auto args = decode_type_args(key);
    ASSERT_EQ(args.size(), 2u);
    EXPECT_EQ(args[0]->kind, hir::TypeKind::Int);
    EXPECT_EQ(args[1]->kind, hir::TypeKind::String);

    // 非エンコード名はそのまま
    EXPECT_EQ(base_name_of("Point"), "Point");
    EXPECT_TRUE(decode_type_args("Point").empty());
}

TEST(TypeKeyTest, DisplayNameIsHumanReadable) {
    auto inner = named("Pair", {hir::make_int(), hir::make_string()});
    auto t = named("Box", {inner});
    EXPECT_EQ(display_name(t), "Box<Pair<int, string>>");
    EXPECT_EQ(display_name(encode_type_key(t)), "Box<Pair<int, string>>");
    EXPECT_EQ(display_name(std::string("Point")), "Point");
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
