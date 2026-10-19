#include "fc/api.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>

#include "fc/chart.hpp"
#include "fc/geo.hpp"
#include "fc/json.hpp"
#include "fc/site_check.hpp"
#include "fc/stats.hpp"
#include "fc/term.hpp"
#include "fc/weather.hpp"

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
  o.excessHeight = number("excessHeight", 0, 0, 300);
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

// The shell keeps its filesystem for the life of the page, so it is built once by term.init.
std::unique_ptr<Term>& shell() {
  static std::unique_ptr<Term> instance;
  return instance;
}

Term& requireShell() {
  if (!shell()) throw ApiError{"call term.init first"};
  return *shell();
}

// input: {site, modes?: ["finance", ...], metars?: {SABE:{raw,observedAt,sample}}, files?: bool}
Json opTermInit(const Json& in) {
  if (!in.at("site").isObject()) throw ApiError{"term.init needs a 'site' object"};
  TermOptions options;
  for (const Json& m : in.at("modes").items()) options.modes.push_back(m.asString());
  for (const auto& m : in.at("metars").members()) {
    MetarSource src;
    src.raw = m.second.at("raw").asString();
    src.observedAt = m.second.at("observedAt").asString();
    src.sample = m.second.at("sample").asBool(false);
    if (!src.raw.empty()) options.metars[m.first] = src;
  }
  shell() = std::make_unique<Term>(in.at("site"), options);
  Json out = Json::object();
  if (in.at("files").asBool(false)) out.set("files", shell()->files());
  return out;
}

// input: {line, cwd?, history?: [string], now?: unix seconds}
Json opTermExec(const Json& in) {
  std::vector<std::string> history;
  for (const Json& h : in.at("history").items()) history.push_back(h.asString());
  std::string cwd = in.at("cwd").isString() ? in.at("cwd").asString() : "~";
  return requireShell().exec(in.at("line").asString(), cwd, history, static_cast<long long>(in.at("now").asNumber(0)));
}

// input: {line, cwd?}
Json opTermComplete(const Json& in) {
  std::string cwd = in.at("cwd").isString() ? in.at("cwd").asString() : "~";
  return requireShell().complete(in.at("line").asString(), cwd);
}

// input: {stats: <payload of /stats> | null, trackingSince, history?: <payload of /stats/history>,
//         placeholder?: bool, pad?: bool}
Json opTermStats(const Json& in) {
  Term& t = requireShell();
  std::string since = in.at("trackingSince").asString();
  bool pad = in.at("pad").asBool(false);
  if (in.at("placeholder").asBool(false)) return t.statsPlaceholder(since, pad);
  return t.statsOutput(in.at("stats"), since, in.at("history"), pad);
}

// input: {raw, observedAt?}
Json opMetarDecode(const Json& in) { return decodeMetar(in.at("raw").asString(), in.at("observedAt").asString()); }

// input: {raw, monthDays?, width?, idPrefix?}
Json opTafDecode(const Json& in) { return decodeTaf(in); }

// input: {dir, speed, gust?, runway, variation?, idPrefix?}
Json opWindComponents(const Json& in) { return windComponents(in); }

// input: {airports:[{icao,name,lat,lon}], width?, height?, idPrefix?}
Json opGeoRoute(const Json& in) {
  if (!in.at("airports").isArray() || in.at("airports").items().size() < 2) throw ApiError{"geo.route needs at least two airports"};
  return routeMap(in);
}

const std::map<std::string, Handler>& handlers() {
  static const std::map<std::string, Handler> table = {
      {"version", opVersion},
      {"json.format", opJsonFormat},
      {"site.check", opSiteCheck},
      {"chart.illustrative", opChartIllustrative},
      {"chart.live", opChartLive},
      {"stats.normalize", opStatsNormalize},
      {"term.init", opTermInit},
      {"term.exec", opTermExec},
      {"term.complete", opTermComplete},
      {"term.stats", opTermStats},
      {"metar.decode", opMetarDecode},
      {"taf.decode", opTafDecode},
      {"wind.components", opWindComponents},
      {"geo.route", opGeoRoute},
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
