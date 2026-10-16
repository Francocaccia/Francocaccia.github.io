#include "fc/api.hpp"

#include <functional>
#include <map>

#include "fc/json.hpp"
#include "fc/site_check.hpp"

namespace fc {

namespace {

using Handler = std::function<Json(const Json&)>;

struct ApiError {
  std::string message;
};

Json opVersion(const Json&) {
  Json out = Json::object();
  out.set("core", kCoreVersion);
  return out;
}

// Parses and re-emits JSON; used as a boundary round-trip check.
Json opJsonFormat(const Json& in) { return in; }

// input: {"data": <site json>, "forbidden": ["term", ...]}
Json opSiteCheck(const Json& in) {
  if (!in.find("data")) throw ApiError{"site.check needs a 'data' field"};
  std::vector<std::string> terms;
  for (const Json& t : in.at("forbidden").items()) terms.push_back(t.asString());
  return issuesToJson(checkSite(in.at("data"), terms));
}

const std::map<std::string, Handler>& handlers() {
  static const std::map<std::string, Handler> table = {
      {"version", opVersion},
      {"json.format", opJsonFormat},
      {"site.check", opSiteCheck},
  };
  return table;
}

std::string failure(const std::string& message) {
  Json out = Json::object();
  out.set("ok", false);
  out.set("error", message);
  return out.dump();
}

}  // namespace

std::string call(const std::string& op, const std::string& inputJson) {
  auto it = handlers().find(op);
  if (it == handlers().end()) return failure("unknown operation '" + op + "'");

  Json input;
  if (!inputJson.empty()) {
    std::string error;
    if (!Json::parse(inputJson, input, error)) return failure("invalid input JSON: " + error);
  }

  try {
    Json out = Json::object();
    out.set("ok", true);
    out.set("result", it->second(input));
    return out.dump();
  } catch (const ApiError& e) {
    return failure(e.message);
  }
}

}  // namespace fc
