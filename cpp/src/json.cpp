#include "fc/json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace fc {

namespace {

const Json kNull;
const std::string kEmptyString;
const Json::Array kEmptyArray;
const Json::Object kEmptyObject;

constexpr int kMaxDepth = 256;

class Parser {
 public:
  explicit Parser(const std::string& text) : s_(text) {}

  bool run(Json& out, std::string& error) {
    skipSpace();
    if (!value(out, 0)) {
      error = error_;
      return false;
    }
    skipSpace();
    if (pos_ != s_.size()) {
      fail("unexpected trailing characters");
      error = error_;
      return false;
    }
    return true;
  }

 private:
  const std::string& s_;
  std::size_t pos_ = 0;
  std::string error_;

  bool fail(const std::string& message) {
    if (error_.empty()) error_ = message + " at offset " + std::to_string(pos_);
    return false;
  }

  void skipSpace() {
    while (pos_ < s_.size()) {
      char c = s_[pos_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++pos_;
      } else {
        break;
      }
    }
  }

  bool literal(const char* word) {
    std::size_t i = 0;
    while (word[i] != '\0') {
      if (pos_ + i >= s_.size() || s_[pos_ + i] != word[i]) return false;
      ++i;
    }
    pos_ += i;
    return true;
  }

  bool value(Json& out, int depth) {
    if (depth > kMaxDepth) return fail("nesting too deep");
    if (pos_ >= s_.size()) return fail("unexpected end of input");
    char c = s_[pos_];
    switch (c) {
      case '{': return object(out, depth);
      case '[': return array(out, depth);
      case '"': {
        std::string text;
        if (!string(text)) return false;
        out = Json(std::move(text));
        return true;
      }
      case 't':
        if (literal("true")) { out = Json(true); return true; }
        return fail("invalid literal");
      case 'f':
        if (literal("false")) { out = Json(false); return true; }
        return fail("invalid literal");
      case 'n':
        if (literal("null")) { out = Json(); return true; }
        return fail("invalid literal");
      default:
        return number(out);
    }
  }

  bool number(Json& out) {
    std::size_t start = pos_;
    if (pos_ < s_.size() && s_[pos_] == '-') ++pos_;
    std::size_t digits = 0;
    while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') { ++pos_; ++digits; }
    if (digits == 0) { pos_ = start; return fail("invalid value"); }
    if (pos_ < s_.size() && s_[pos_] == '.') {
      ++pos_;
      std::size_t frac = 0;
      while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') { ++pos_; ++frac; }
      if (frac == 0) return fail("digits expected after decimal point");
    }
    if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
      ++pos_;
      if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
      std::size_t exp = 0;
      while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') { ++pos_; ++exp; }
      if (exp == 0) return fail("digits expected in exponent");
    }
    out = Json(std::strtod(s_.substr(start, pos_ - start).c_str(), nullptr));
    return true;
  }

  static void appendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }

  bool hex4(unsigned& out) {
    if (pos_ + 4 > s_.size()) return fail("truncated \\u escape");
    unsigned v = 0;
    for (int i = 0; i < 4; ++i) {
      char c = s_[pos_ + i];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
      else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
      else return fail("invalid \\u escape");
    }
    pos_ += 4;
    out = v;
    return true;
  }

  bool string(std::string& out) {
    ++pos_;  // opening quote
    while (true) {
      if (pos_ >= s_.size()) return fail("unterminated string");
      unsigned char c = static_cast<unsigned char>(s_[pos_]);
      if (c == '"') { ++pos_; return true; }
      if (c < 0x20) return fail("control character in string");
      if (c != '\\') { out += static_cast<char>(c); ++pos_; continue; }
      ++pos_;
      if (pos_ >= s_.size()) return fail("unterminated escape");
      char e = s_[pos_++];
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          unsigned cp = 0;
          if (!hex4(cp)) return false;
          if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (pos_ + 1 < s_.size() && s_[pos_] == '\\' && s_[pos_ + 1] == 'u') {
              pos_ += 2;
              unsigned lo = 0;
              if (!hex4(lo)) return false;
              if (lo < 0xDC00 || lo > 0xDFFF) return fail("invalid surrogate pair");
              cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            } else {
              return fail("lone high surrogate");
            }
          } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            return fail("lone low surrogate");
          }
          appendUtf8(out, cp);
          break;
        }
        default: return fail("invalid escape");
      }
    }
  }

  bool array(Json& out, int depth) {
    ++pos_;  // [
    Json result = Json::array();
    skipSpace();
    if (pos_ < s_.size() && s_[pos_] == ']') { ++pos_; out = std::move(result); return true; }
    while (true) {
      skipSpace();
      Json item;
      if (!value(item, depth + 1)) return false;
      result.push(std::move(item));
      skipSpace();
      if (pos_ >= s_.size()) return fail("unterminated array");
      if (s_[pos_] == ',') { ++pos_; continue; }
      if (s_[pos_] == ']') { ++pos_; break; }
      return fail("expected ',' or ']'");
    }
    out = std::move(result);
    return true;
  }

  bool object(Json& out, int depth) {
    ++pos_;  // {
    Json result = Json::object();
    skipSpace();
    if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; out = std::move(result); return true; }
    while (true) {
      skipSpace();
      if (pos_ >= s_.size() || s_[pos_] != '"') return fail("object key expected");
      std::string key;
      if (!string(key)) return false;
      skipSpace();
      if (pos_ >= s_.size() || s_[pos_] != ':') return fail("expected ':'");
      ++pos_;
      skipSpace();
      Json item;
      if (!value(item, depth + 1)) return false;
      result.set(key, std::move(item));
      skipSpace();
      if (pos_ >= s_.size()) return fail("unterminated object");
      if (s_[pos_] == ',') { ++pos_; continue; }
      if (s_[pos_] == '}') { ++pos_; break; }
      return fail("expected ',' or '}'");
    }
    out = std::move(result);
    return true;
  }
};

void escapeInto(std::string& out, const std::string& s) {
  out += '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  out += '"';
}

std::string numberText(double v) {
  if (!std::isfinite(v)) return "null";  // JSON has no NaN or Infinity
  if (v == std::floor(v) && std::fabs(v) < 1e15) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.0f", v);
    return buf;
  }
  char buf[40];
  std::snprintf(buf, sizeof buf, "%.15g", v);
  if (std::strtod(buf, nullptr) != v) std::snprintf(buf, sizeof buf, "%.17g", v);
  return buf;
}

void dumpInto(std::string& out, const Json& v, int indent, int level) {
  auto newline = [&](int lvl) {
    if (indent < 0) return;
    out += '\n';
    out.append(static_cast<std::size_t>(indent * lvl), ' ');
  };
  switch (v.type()) {
    case Json::Type::Null: out += "null"; break;
    case Json::Type::Bool: out += v.asBool() ? "true" : "false"; break;
    case Json::Type::Number: out += numberText(v.asNumber()); break;
    case Json::Type::String: escapeInto(out, v.asString()); break;
    case Json::Type::Array: {
      if (v.items().empty()) { out += "[]"; break; }
      out += '[';
      bool first = true;
      for (const Json& item : v.items()) {
        if (!first) out += ',';
        first = false;
        newline(level + 1);
        dumpInto(out, item, indent, level + 1);
      }
      newline(level);
      out += ']';
      break;
    }
    case Json::Type::Object: {
      if (v.members().empty()) { out += "{}"; break; }
      out += '{';
      bool first = true;
      for (const auto& m : v.members()) {
        if (!first) out += ',';
        first = false;
        newline(level + 1);
        escapeInto(out, m.first);
        out += indent < 0 ? ":" : ": ";
        dumpInto(out, m.second, indent, level + 1);
      }
      newline(level);
      out += '}';
      break;
    }
  }
}

}  // namespace

Json::Json() = default;
Json::Json(std::nullptr_t) {}
Json::Json(bool value) : type_(Type::Bool), bool_(value) {}
Json::Json(double value) : type_(Type::Number), number_(value) {}
Json::Json(int value) : type_(Type::Number), number_(value) {}
Json::Json(const char* value) : type_(Type::String), string_(value ? value : "") {}
Json::Json(std::string value) : type_(Type::String), string_(std::move(value)) {}
Json::Json(Array value) : type_(Type::Array), array_(std::move(value)) {}
Json::Json(Object value) : type_(Type::Object), object_(std::move(value)) {}

Json Json::array() { return Json(Array{}); }
Json Json::object() { return Json(Object{}); }

bool Json::parse(const std::string& text, Json& out, std::string& error) {
  Parser parser(text);
  Json result;
  if (!parser.run(result, error)) return false;
  out = std::move(result);
  return true;
}

std::string Json::dump(int indent) const {
  std::string out;
  dumpInto(out, *this, indent, 0);
  return out;
}

bool Json::asBool(bool fallback) const { return type_ == Type::Bool ? bool_ : fallback; }
double Json::asNumber(double fallback) const { return type_ == Type::Number ? number_ : fallback; }
const std::string& Json::asString() const { return type_ == Type::String ? string_ : kEmptyString; }
const Json::Array& Json::items() const { return type_ == Type::Array ? array_ : kEmptyArray; }
const Json::Object& Json::members() const { return type_ == Type::Object ? object_ : kEmptyObject; }

const Json* Json::find(const std::string& key) const {
  if (type_ != Type::Object) return nullptr;
  for (const auto& m : object_) {
    if (m.first == key) return &m.second;
  }
  return nullptr;
}

const Json& Json::at(const std::string& key) const {
  const Json* found = find(key);
  return found ? *found : kNull;
}

const Json& Json::at(std::size_t index) const {
  if (type_ != Type::Array || index >= array_.size()) return kNull;
  return array_[index];
}

Json& Json::set(const std::string& key, Json value) {
  if (type_ != Type::Object) {
    *this = Json::object();
  }
  for (auto& m : object_) {
    if (m.first == key) {
      m.second = std::move(value);
      return *this;
    }
  }
  object_.emplace_back(key, std::move(value));
  return *this;
}

Json& Json::push(Json value) {
  if (type_ != Type::Array) {
    *this = Json::array();
  }
  array_.push_back(std::move(value));
  return *this;
}

}  // namespace fc
