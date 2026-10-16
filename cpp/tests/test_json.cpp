#include "fc/json.hpp"
#include "test.hpp"

using fc::Json;

namespace {

Json parseOk(const std::string& text) {
  Json out;
  std::string error;
  bool ok = Json::parse(text, out, error);
  CHECK(ok);
  return out;
}

bool parseFails(const std::string& text) {
  Json out;
  std::string error;
  return !Json::parse(text, out, error);
}

}  // namespace

TEST(json_scalars) {
  CHECK(parseOk("null").isNull());
  CHECK(parseOk("true").asBool());
  CHECK(!parseOk("false").asBool(true));
  CHECK_EQ(parseOk("-12.5e1").asNumber(), -125.0);
  CHECK_EQ(parseOk("\"hi\"").asString(), std::string("hi"));
}

TEST(json_nested_lookup) {
  Json v = parseOk(R"({"a":{"b":[1,2,{"c":"x"}]},"d":null})");
  CHECK_EQ(v.at("a").at("b").at(2).at("c").asString(), std::string("x"));
  CHECK(v.at("missing").at("deeper").isNull());
  CHECK(v.find("d") != nullptr);
  CHECK(v.find("zzz") == nullptr);
}

TEST(json_string_escapes_and_unicode) {
  CHECK_EQ(parseOk(R"("a\nb\t\"q\"\\")").asString(), std::string("a\nb\t\"q\"\\"));
  CHECK_EQ(parseOk(R"("é")").asString(), std::string("\xC3\xA9"));
  CHECK_EQ(parseOk(R"("😀")").asString(), std::string("\xF0\x9F\x98\x80"));
  CHECK(parseFails(R"("\ud83d")"));
}

TEST(json_rejects_malformed) {
  CHECK(parseFails(""));
  CHECK(parseFails("{"));
  CHECK(parseFails("[1,]"));
  CHECK(parseFails("{\"a\" 1}"));
  CHECK(parseFails("01x"));
  CHECK(parseFails("1 2"));
  CHECK(parseFails("\"line\nbreak\""));
  CHECK(parseFails(std::string(1000, '[')));
}

TEST(json_error_reports_offset) {
  Json out;
  std::string error;
  CHECK(!Json::parse("[1, ?]", out, error));
  CHECK(error.find("offset 4") != std::string::npos);
}

TEST(json_dump_roundtrip) {
  const std::string text = R"({"n":1.5,"i":42,"s":"a\"b","l":[true,null],"o":{}})";
  CHECK_EQ(parseOk(text).dump(), text);
  Json pretty = parseOk(parseOk(text).dump(2));
  CHECK_EQ(pretty.dump(), text);
}

TEST(json_keeps_insertion_order) {
  Json o = Json::object();
  o.set("z", 1);
  o.set("a", 2);
  o.set("z", 3);
  CHECK_EQ(o.dump(), std::string(R"({"z":3,"a":2})"));
}
