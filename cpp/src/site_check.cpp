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

// Placeholder text: "TODO..." in any case, optionally after spaces or a "[", or the bare word TBD.
bool isPlaceholder(const std::string& s) {
  std::size_t from = s.find_first_not_of(" \t[");
  if (from == std::string::npos) return false;
  std::string rest = lower(s.substr(from));
  if (rest.compare(0, 4, "todo") == 0) return true;
  std::size_t to = rest.find_last_not_of(" \t]");
  return rest.substr(0, to + 1) == "tbd";
}

// A "$" followed by an optional space and a digit, which reads as a money amount.
bool hasDollarAmount(const std::string& s) {
  for (std::size_t i = s.find('$'); i != std::string::npos; i = s.find('$', i + 1)) {
    std::size_t j = i + 1;
    while (j < s.size() && (s[j] == ' ' || s[j] == '\t')) ++j;
    if (j < s.size() && s[j] >= '0' && s[j] <= '9') return true;
  }
  return false;
}

// Matches ^[a-z0-9][a-z0-9-]*$.
bool isSlug(const std::string& s) {
  if (s.empty() || s[0] == '-') return false;
  for (char ch : s) {
    if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-')) return false;
  }
  return true;
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

  // Errors on any string value under `node` that holds a dollar amount (the text is never echoed).
  void noDollars(const Json& node, const std::string& path) {
    if (node.isString()) {
      if (hasDollarAmount(node.asString())) error(path, "dollar amounts must not appear here");
      return;
    }
    std::size_t i = 0;
    for (const Json& item : node.items()) noDollars(item, path + "[" + std::to_string(i++) + "]");
    for (const auto& m : node.members()) noDollars(m.second, path + "." + m.first);
  }

  // A required string that must also start with `prefix`.
  void needPrefix(const Json& parent, const std::string& path, const char* key, const char* prefix) {
    const Json* v = need(parent, path, key, Json::Type::String);
    if (v && !startsWith(v->asString(), prefix)) {
      error(path.empty() ? key : path + "." + key, std::string("must start with ") + prefix);
    }
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
        if (isPlaceholder(s)) warn(path, "placeholder still present: " + s);
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
  static const char* const names[] = {"positions", "holdings", "shares",  "quantity", "balance",  "equity",
                                      "cash",      "orders",   "symbols", "tickers",  "notional", "position",
                                      "pnl",       "amount",   "holding"};
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
          if (here == "bot.exampleDecision") continue;  // an illustration of the model's reply, with its own keys
          if (isForbiddenBotKey(m.first)) {
            c.error(here, "positions, amounts and symbols must not appear in bot data");
          }
          walk(m.second, here);
        }
        std::size_t i = 0;
        for (const Json& item : node.items()) walk(item, path + "[" + std::to_string(i++) + "]");
      };
  walk(bot, "bot");
  for (const auto& m : bot.members()) {
    if (m.first != "exampleDecision") c.noDollars(m.second, "bot." + m.first);
  }

  c.need(bot, "bot", "trackingSince", Json::Type::String);
  c.needPrefix(bot, "bot", "apiBase", "https://");
  c.need(bot, "bot", "statsPath", Json::Type::String);
  c.need(bot, "bot", "historyPath", Json::Type::String);
  c.need(bot, "bot", "historyLive", Json::Type::Bool);
  c.need(bot, "bot", "benchmarks", Json::Type::Array);
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
  if (const Json* resume = site.find("resume")) {
    // A path on this site; a TODO placeholder is already a warning.
    const std::string& r = resume->asString();
    if (resume->isString() && !r.empty() && !isPlaceholder(r) && (r[0] != '/' || startsWith(r, "//"))) {
      c.error("resume", "must be a path on this site, starting with a single /");
    }
  }
  for (const char* key : {"technicalSkills", "coursework"}) {
    if (const Json* list = c.need(site, "", key, Json::Type::Array)) {
      if (list->items().empty()) c.error(key, "must not be empty");
      std::size_t i = 0;
      for (const Json& item : list->items()) {
        if (!item.isString() || item.asString().empty()) c.error(std::string(key) + "[" + std::to_string(i) + "]", "must be a non-empty string");
        ++i;
      }
    }
  }
  if (const Json* links = c.need(site, "", "links", Json::Type::Object)) {
    c.needPrefix(*links, "links", "github", "https://");
    c.needPrefix(*links, "links", "linkedin", "https://");
  }

  std::vector<std::string> projectIds;
  checkArrayOfObjects(c, site, "projects", [&](const Json& p, const std::string& path) {
    if (const Json* id = c.need(p, path, "id", Json::Type::String)) {
      if (!isSlug(id->asString())) c.error(path + ".id", "must be lowercase letters, digits and hyphens, not starting with a hyphen");
      if (std::find(projectIds.begin(), projectIds.end(), id->asString()) != projectIds.end()) c.error(path + ".id", "must be unique");
      projectIds.push_back(id->asString());
    }
    if (const Json* repo = p.find("repo")) {
      // null means no repository yet; anything else must be a string, empty or an https link.
      bool ok = repo->isNull() || (repo->isString() && (repo->asString().empty() || startsWith(repo->asString(), "https://")));
      if (!ok) c.error(path + ".repo", "must be empty or start with https://");
    }
    c.need(p, path, "name", Json::Type::String);
    c.need(p, path, "tag", Json::Type::String);
    c.need(p, path, "summary", Json::Type::String);
    if (const Json* stack = c.need(p, path, "stack", Json::Type::Array)) {
      std::size_t i = 0;
      for (const Json& item : stack->items()) {
        if (!item.isString()) c.error(path + ".stack[" + std::to_string(i) + "]", "must be a string");
        ++i;
      }
    }
    c.oneOf(p, path, "status", {"live", "shipped", "next", "planned"});
    c.need(p, path, "statusLabel", Json::Type::String);
  });

  checkArrayOfObjects(c, site, "experience", [&](const Json& e, const std::string& path) {
    for (const char* k : {"id", "role", "org", "start", "end", "description"}) c.need(e, path, k, Json::Type::String);
  });

  checkArrayOfObjects(c, site, "education", [&](const Json& e, const std::string& path) {
    for (const char* k : {"degree", "school", "period"}) c.need(e, path, k, Json::Type::String);
  });

  checkArrayOfObjects(c, site, "certifications", [&](const Json& e, const std::string& path) {
    c.need(e, path, "name", Json::Type::String);
    c.oneOf(e, path, "status", {"active", "in-progress", "complete"});
    c.oneOf(e, path, "track", {"aviation", "finance"});
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

  if (const Json* av = c.need(site, "", "aviation", Json::Type::Object)) {
    if (const Json* taf = c.need(*av, "aviation", "tafSample", Json::Type::Object)) {
      c.need(*taf, "aviation.tafSample", "raw", Json::Type::String);
      c.need(*taf, "aviation.tafSample", "label", Json::Type::String);
      c.need(*taf, "aviation.tafSample", "monthDays", Json::Type::Number);
    }
    if (const Json* ex = c.need(*av, "aviation", "examples", Json::Type::Array)) {
      if (ex->items().empty()) c.error("aviation.examples", "must not be empty");
      std::size_t i = 0;
      for (const Json& e : ex->items()) {
        std::string path = "aviation.examples[" + std::to_string(i++) + "]";
        c.need(e, path, "label", Json::Type::String);
        c.need(e, path, "raw", Json::Type::String);
      }
    }
    if (const Json* mc = c.need(*av, "aviation", "meetsCode", Json::Type::Array)) {
      std::size_t i = 0;
      for (const Json& e : mc->items()) {
        std::string path = "aviation.meetsCode[" + std::to_string(i++) + "]";
        for (const char* k : {"habit", "inFlight", "inCode"}) c.need(e, path, k, Json::Type::String);
      }
    }
  }

  // Finance view copy and the figures in its numbers row. Each figure points at the entry it comes from.
  checkArrayOfObjects(c, site, "highlights", [&](const Json& h, const std::string& path) {
    for (const char* k : {"id", "value", "label", "source"}) c.need(h, path, k, Json::Type::String);
    for (const char* k : {"value", "label"}) {
      if (const Json* v = h.find(k)) c.noDollars(*v, path + "." + k);
    }
  });
  if (const Json* fin = c.need(site, "", "finance", Json::Type::Object)) {
    c.noDollars(*fin, "finance");
    c.need(*fin, "finance", "salutation", Json::Type::String);
    c.need(*fin, "finance", "letter", Json::Type::String);
    const Json* quote = c.need(*fin, "finance", "quote", Json::Type::String);
    if (const Json* words = c.need(*fin, "finance", "headline", Json::Type::Array)) {
      if (words->items().size() != 3) c.error("finance.headline", "must have exactly 3 words");
      std::size_t i = 0;
      for (const Json& w : words->items()) {
        if (!w.isString() || w.asString().empty()) c.error("finance.headline[" + std::to_string(i) + "]", "must be a non-empty string");
        ++i;
      }
    }
    // The pull quote is one of the bot's risk controls, not a separate claim.
    const Json* bot = site.find("bot");
    const Json* controls = bot ? bot->find("riskControls") : nullptr;
    if (quote && controls && controls->isArray() && !controls->items().empty()) {
      bool found = false;
      for (const Json& r : controls->items()) found = found || (r.isString() && r.asString() == quote->asString());
      if (!found) c.error("finance.quote", "must match one of bot.riskControls");
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
