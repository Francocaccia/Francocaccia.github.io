#include "fc/api.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <map>

#include "fc/chart.hpp"
#include "fc/json.hpp"
#include "fc/site_check.hpp"
#include "fc/stats.hpp"

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

ChartOptions chartOptions(const Json& in) {
  ChartOptions o;
  auto number = [&](const char* key, int fallback, int lo, int hi) {
    const Json& v = in.at(key);
    if (!v.isNumber()) return fallback;
    return static_cast<int>(std::max<double>(lo, std::min<double>(hi, v.asNumber())));
  };
  o.width = number("width", o.width, 120, 1600);
  o.height = number("height", o.height, 48, 1200);
  o.compact = in.at("compact").asBool(false);
  if (in.at("idPrefix").isString()) {
    // Ids are interpolated into markup: keep only safe characters.
    std::string id;
    for (char c : in.at("idPrefix").asString()) {
      if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') id += c;
    }
    if (!id.empty()) o.idPrefix = id;
  }
  if (in.at("title").isString() && !in.at("title").asString().empty()) o.title = in.at("title").asString();
  return o;
}

// input: {seed?, days?, width?, height?, compact?, idPrefix?, title?}
Json opChartIllustrative(const Json& in) {
  ChartOptions o = chartOptions(in);
  o.illustrative = true;
  int days = in.at("days").isNumber() ? static_cast<int>(in.at("days").asNumber()) : 30;
  auto seed = static_cast<std::uint32_t>(in.at("seed").asNumber(7));
  return renderChart(illustrativeSeries(seed, days), o);
}

// input: {series: [{date,o,h,l,c,spy,mix_60_40}], width?, height?, idPrefix?, title?}
Json opChartLive(const Json& in) {
  std::vector<Candle> series;
  std::string error;
  if (!parseHistory(in.at("series"), series, error)) throw ApiError{error};
  return renderChart(series, chartOptions(in));
}

// input: {stats: <payload of /stats>, trackingSince: "YYYY-MM-DD"}
Json opStatsNormalize(const Json& in) {
  if (!in.at("stats").isObject()) throw ApiError{"stats.normalize needs a 'stats' object"};
  return normalizeStats(in.at("stats"), in.at("trackingSince").asString());
}

const std::map<std::string, Handler>& handlers() {
  static const std::map<std::string, Handler> table = {
      {"version", opVersion},
      {"json.format", opJsonFormat},
      {"site.check", opSiteCheck},
      {"chart.illustrative", opChartIllustrative},
      {"chart.live", opChartLive},
      {"stats.normalize", opStatsNormalize},
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
