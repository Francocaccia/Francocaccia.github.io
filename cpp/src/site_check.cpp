#include "fc/site_check.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <initializer_list>

namespace fc {

namespace {

using Severity = Issue::Severity;

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

bool startsWith(const std::string& s, const char* prefix) {
  std::string p(prefix);
  return s.compare(0, p.size(), p) == 0;
}

class Checker {
 public:
  explicit Checker(const std::vector<std::string>& terms) {
    for (const auto& t : terms) {
      if (!t.empty()) terms_.push_back(lower(t));
    }
  }

  std::vector<Issue> issues;

  void error(const std::string& path, const std::string& message) {
    issues.push_back({Severity::Error, path, message});
  }
  void warn(const std::string& path, const std::string& message) {
    issues.push_back({Severity::Warning, path, message});
  }

  const Json* need(const Json& parent, const std::string& path, const char* key, Json::Type type) {
    const Json* v = parent.find(key);
    std::string here = path.empty() ? key : path + "." + key;
    if (!v) {
      error(here, "missing required field");
      return nullptr;
    }
    if (v->type() != type) {
      error(here, "wrong type");
      return nullptr;
    }
    if (type == Json::Type::String && v->asString().empty()) {
      error(here, "must not be empty");
      return nullptr;
    }
    return v;
  }

  void oneOf(const Json& node, const std::string& path, const char* key,
             std::initializer_list<const char*> allowed) {
    const Json* v = need(node, path, key, Json::Type::String);
    if (!v) return;
    for (const char* a : allowed) {
      if (v->asString() == a) return;
    }
    error(path + "." + key, "unexpected value '" + v->asString() + "'");
  }

  // Walks every key and string once for TODO markers and forbidden terms.
  void scan(const Json& node, const std::string& path) {
    switch (node.type()) {
      case Json::Type::String: {
        const std::string& s = node.asString();
        if (startsWith(s, "TODO")) warn(path, "placeholder still present: " + s);
        scanText(s, path);
        break;
      }
      case Json::Type::Array: {
        std::size_t i = 0;
        for (const Json& item : node.items()) {
          scan(item, path + "[" + std::to_string(i++) + "]");
        }
        break;
      }
      case Json::Type::Object:
        for (const auto& m : node.members()) {
          std::string here = path.empty() ? m.first : path + "." + m.first;
          scanText(m.first, here + " (key)");
          scan(m.second, here);
        }
        break;
      default:
        break;
    }
  }

 private:
  std::vector<std::string> terms_;

  void scanText(const std::string& text, const std::string& path) {
    if (terms_.empty()) return;
    std::string hay = lower(text);
    for (const auto& t : terms_) {
      if (hay.find(t) != std::string::npos) {
        error(path, "contains a forbidden term");  // never echo the term itself
        return;
      }
    }
  }
};

void checkArrayOfObjects(Checker& c, const Json& site, const char* key,
                         const std::function<void(const Json&, const std::string&)>& each) {
  const Json* arr = c.need(site, "", key, Json::Type::Array);
  if (!arr) return;
  if (arr->items().empty()) c.error(key, "must not be empty");
  std::size_t i = 0;
  for (const Json& item : arr->items()) {
    std::string path = std::string(key) + "[" + std::to_string(i++) + "]";
    if (!item.isObject()) {
      c.error(path, "must be an object");
      continue;
    }
    each(item, path);
  }
}

// Keys that would reveal positions or amounts, which the bot section must never carry.
bool isForbiddenBotKey(const std::string& key) {
  static const char* const names[] = {"positions", "holdings", "shares",  "quantity",
                                      "balance",   "equity",   "cash",    "orders",
                                      "symbols",   "tickers",  "notional"};
  std::string k = lower(key);
  for (const char* n : names) {
    if (k == n) return true;
  }
  return false;
}

void checkBot(Checker& c, const Json& bot) {
  std::function<void(const Json&, const std::string&)> walk =
      [&](const Json& node, const std::string& path) {
        for (const auto& m : node.members()) {
          std::string here = path + "." + m.first;
          if (isForbiddenBotKey(m.first)) {
            c.error(here, "positions, amounts and symbols must not appear in bot data");
          }
          walk(m.second, here);
        }
        std::size_t i = 0;
        for (const Json& item : node.items()) walk(item, path + "[" + std::to_string(i++) + "]");
      };
  walk(bot, "bot");

  c.need(bot, "bot", "trackingSince", Json::Type::String);
  c.need(bot, "bot", "apiBase", Json::Type::String);
  c.need(bot, "bot", "statsPath", Json::Type::String);
  c.need(bot, "bot", "historyPath", Json::Type::String);
  const Json* timeout = c.need(bot, "bot", "timeoutMs", Json::Type::Number);
  if (timeout && (timeout->asNumber() < 1000 || timeout->asNumber() > 300000)) {
    c.error("bot.timeoutMs", "must be between 1000 and 300000");
  }
  if (const Json* controls = c.need(bot, "bot", "riskControls", Json::Type::Array)) {
    if (controls->items().empty()) c.error("bot.riskControls", "must not be empty");
    std::size_t i = 0;
    for (const Json& item : controls->items()) {
      if (!item.isString() || item.asString().empty()) {
        c.error("bot.riskControls[" + std::to_string(i) + "]", "must be a non-empty string");
      }
      ++i;
    }
  }
  if (const Json* decision = c.need(bot, "bot", "exampleDecision", Json::Type::Object)) {
    c.need(*decision, "bot.exampleDecision", "action", Json::Type::String);
  }
  if (const Json* flow = c.need(bot, "bot", "decisionFlow", Json::Type::Array)) {
    std::size_t i = 0;
    for (const Json& step : flow->items()) {
      std::string path = "bot.decisionFlow[" + std::to_string(i++) + "]";
      c.need(step, path, "step", Json::Type::String);
      c.need(step, path, "text", Json::Type::String);
    }
  }
  const Json* bands = c.need(bot, "bot", "policyBands", Json::Type::Array);
  if (bands) {
    std::size_t i = 0;
    for (const Json& band : bands->items()) {
      std::string path = "bot.policyBands[" + std::to_string(i++) + "]";
      c.need(band, path, "label", Json::Type::String);
      const Json* lo = c.need(band, path, "min", Json::Type::Number);
      const Json* hi = c.need(band, path, "max", Json::Type::Number);
      if (lo && hi && !(lo->asNumber() >= 0 && lo->asNumber() <= hi->asNumber() && hi->asNumber() <= 100)) {
        c.error(path, "band must satisfy 0 <= min <= max <= 100");
      }
    }
  }
}

}  // namespace

std::vector<Issue> checkSite(const Json& site, const std::vector<std::string>& forbiddenTerms) {
  Checker c(forbiddenTerms);
  if (!site.isObject()) {
    c.error("", "site data must be a JSON object");
    return c.issues;
  }

  for (const char* key : {"name", "location", "positioning", "openTo", "email", "resume", "photo", "footer"}) {
    c.need(site, "", key, Json::Type::String);
  }
  if (const Json* links = c.need(site, "", "links", Json::Type::Object)) {
    c.need(*links, "links", "github", Json::Type::String);
    c.need(*links, "links", "linkedin", Json::Type::String);
  }

  checkArrayOfObjects(c, site, "projects", [&](const Json& p, const std::string& path) {
    c.need(p, path, "id", Json::Type::String);
    c.need(p, path, "name", Json::Type::String);
    c.need(p, path, "tag", Json::Type::String);
    c.need(p, path, "summary", Json::Type::String);
    c.need(p, path, "stack", Json::Type::Array);
    c.oneOf(p, path, "status", {"live", "shipped", "next", "planned"});
    c.need(p, path, "statusLabel", Json::Type::String);
  });

  checkArrayOfObjects(c, site, "experience", [&](const Json& e, const std::string& path) {
    for (const char* k : {"role", "org", "start", "description"}) c.need(e, path, k, Json::Type::String);
  });

  checkArrayOfObjects(c, site, "education", [&](const Json& e, const std::string& path) {
    for (const char* k : {"degree", "school", "period"}) c.need(e, path, k, Json::Type::String);
  });

  checkArrayOfObjects(c, site, "certifications", [&](const Json& e, const std::string& path) {
    c.need(e, path, "name", Json::Type::String);
    c.oneOf(e, path, "status", {"active", "in-progress", "complete"});
  });

  checkArrayOfObjects(c, site, "languages", [&](const Json& e, const std::string& path) {
    c.need(e, path, "name", Json::Type::String);
    c.need(e, path, "level", Json::Type::String);
  });

  checkArrayOfObjects(c, site, "skills", [&](const Json& e, const std::string& path) {
    c.need(e, path, "name", Json::Type::String);
    c.need(e, path, "group", Json::Type::String);
    c.need(e, path, "usedIn", Json::Type::Array);
  });

  checkArrayOfObjects(c, site, "airports", [&](const Json& a, const std::string& path) {
    for (const char* k : {"icao", "name", "role"}) c.need(a, path, k, Json::Type::String);
    const Json* lat = c.need(a, path, "lat", Json::Type::Number);
    const Json* lon = c.need(a, path, "lon", Json::Type::Number);
    if (lat && (lat->asNumber() < -90 || lat->asNumber() > 90)) c.error(path + ".lat", "out of range");
    if (lon && (lon->asNumber() < -180 || lon->asNumber() > 180)) c.error(path + ".lon", "out of range");
  });

  if (const Json* weather = c.need(site, "", "weather", Json::Type::Object)) {
    for (const auto& m : weather->members()) {
      std::string path = "weather." + m.first;
      c.need(m.second, path, "raw", Json::Type::String);
      c.need(m.second, path, "observedAt", Json::Type::String);
      if (!m.second.at("sample").isBool()) c.error(path + ".sample", "must be true or false");
    }
  }

  if (const Json* bot = c.need(site, "", "bot", Json::Type::Object)) checkBot(c, *bot);

  c.scan(site, "");
  return c.issues;
}

Json issuesToJson(const std::vector<Issue>& issues) {
  Json out = Json::array();
  for (const Issue& i : issues) {
    Json item = Json::object();
    item.set("severity", i.severity == Severity::Error ? "error" : "warning");
    item.set("path", i.path);
    item.set("message", i.message);
    out.push(std::move(item));
  }
  return out;
}

}  // namespace fc
