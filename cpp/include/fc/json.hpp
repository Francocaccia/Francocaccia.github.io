// Minimal JSON value, parser and serializer.
// Objects keep insertion order so output is deterministic.
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace fc {

class Json {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };
  using Array = std::vector<Json>;
  using Member = std::pair<std::string, Json>;
  using Object = std::vector<Member>;

  Json();
  Json(std::nullptr_t);
  Json(bool value);
  Json(double value);
  Json(int value);
  Json(const char* value);
  Json(std::string value);
  Json(Array value);
  Json(Object value);

  static Json array();
  static Json object();

  // Parses `text`. On failure returns false and sets `error` to a message
  // that includes the byte offset of the problem.
  static bool parse(const std::string& text, Json& out, std::string& error);

  // indent < 0 gives compact output, otherwise pretty-printed.
  std::string dump(int indent = -1) const;

  Type type() const { return type_; }
  bool isNull() const { return type_ == Type::Null; }
  bool isBool() const { return type_ == Type::Bool; }
  bool isNumber() const { return type_ == Type::Number; }
  bool isString() const { return type_ == Type::String; }
  bool isArray() const { return type_ == Type::Array; }
  bool isObject() const { return type_ == Type::Object; }

  bool asBool(bool fallback = false) const;
  double asNumber(double fallback = 0.0) const;
  const std::string& asString() const;  // empty string when not a string
  const Array& items() const;           // empty when not an array
  const Object& members() const;        // empty when not an object

  // Object lookup. find() returns nullptr when missing; at() returns a shared
  // null value so chained lookups are safe.
  const Json* find(const std::string& key) const;
  const Json& at(const std::string& key) const;
  const Json& at(std::size_t index) const;

  // Mutation (used when building responses).
  Json& set(const std::string& key, Json value);  // object
  Json& push(Json value);                         // array

 private:
  Type type_ = Type::Null;
  bool bool_ = false;
  double number_ = 0.0;
  std::string string_;
  Array array_;
  Object object_;
};

}  // namespace fc
